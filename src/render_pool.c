/*
 * render_pool.c - 多线程瓦片渲染线程池 + LRU 缓存
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mupdf/fitz.h"

#include "render_pool.h"

#define MAX_WORKERS   8
#define MAX_TILES     64      /* LRU 容量 (64 * 4MB ≈ 256MB 上限) */
#define MAX_PENDING   512

typedef struct {
    int   page;
    float zoom;
    int   x, y;
} Pending;

typedef struct ReqNode {
    int   page;
    float zoom;
    int   x, y, w, h;
    LONG  gen;
    struct ReqNode *next;
} ReqNode;

typedef struct ResNode {
    int   page;
    float zoom;
    int   x, y, w, h;
    LONG  gen;
    HBITMAP bmp;
    struct ResNode *next;
} ResNode;

typedef struct {
    int   page;
    float zoom;
    int   x, y, w, h;
    HBITMAP bmp;
    ULONGLONG lastUse;
} TileEntry;

typedef struct {
    RenderPool *pool;
    fz_context *ctx;
    fz_document *doc;
    fz_page    *page;
    int         page_no;
} Worker;

struct RenderPool {
    HWND hWnd;
    const unsigned char *data;
    size_t len;
    int    nthreads;
    HANDLE threads[MAX_WORKERS];
    HANDLE sem;

    CRITICAL_SECTION reqLock, resLock;
    ReqNode *reqHead, *reqTail;
    ResNode *resHead, *resTail;

    volatile LONG gen;
    volatile LONG quit;

    /* 以下仅在 UI 线程访问 */
    Pending *pending;
    int      npend, pendCap;
    TileEntry *cache;
    int      ncache;
    ULONGLONG tick;

    Worker workers[MAX_WORKERS];
};

static int zoom_eq(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

/* ------------------------- 队列 ------------------------- */

static int pool_pop(RenderPool *p, ReqNode **out)
{
    ReqNode *n;
    WaitForSingleObject(p->sem, INFINITE);
    EnterCriticalSection(&p->reqLock);
    n = p->reqHead;
    if (n) {
        p->reqHead = n->next;
        if (!p->reqHead) p->reqTail = NULL;
    }
    LeaveCriticalSection(&p->reqLock);
    *out = n;
    return n != NULL;
}

static void pool_push_result(RenderPool *p, const ReqNode *q, HBITMAP bmp)
{
    ResNode *r = (ResNode *)malloc(sizeof(ResNode));
    if (!r) { return; }
    r->page = q->page; r->zoom = q->zoom;
    r->x = q->x; r->y = q->y; r->w = q->w; r->h = q->h;
    r->gen = q->gen; r->bmp = bmp; r->next = NULL;

    EnterCriticalSection(&p->resLock);
    if (p->resTail) p->resTail->next = r; else p->resHead = r;
    p->resTail = r;
    LeaveCriticalSection(&p->resLock);

    if (p->hWnd) PostMessageW(p->hWnd, WM_APP_TILE, 0, 0);
}

/* ------------------------- 渲染 ------------------------- */

static int render_tile(Worker *w, const ReqNode *q, HBITMAP *out)
{
    fz_context *ctx = w->ctx;
    fz_pixmap  *pix = NULL;
    fz_device  *dev = NULL;
    HBITMAP     bmp = NULL;
    void       *bits = NULL;
    BITMAPINFO  bi;
    int         ok = -1;

    fz_try(ctx) {
        fz_rect  b;
        fz_matrix ctm;

        if (w->page_no != q->page) {
            if (w->page) fz_drop_page(ctx, w->page);
            w->page = fz_load_page(ctx, w->doc, q->page);
            w->page_no = q->page;
        }
        b = fz_bound_page(ctx, w->page);

        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx),
                                      fz_make_irect(q->x, q->y,
                                                    q->x + q->w, q->y + q->h),
                                      NULL, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);

        ctm = fz_scale(q->zoom, q->zoom);
        ctm = fz_pre_translate(ctm, -b.x0, -b.y0);
        dev = fz_new_draw_device(ctx, ctm, pix);
        fz_run_page(ctx, w->page, dev, fz_identity, NULL);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        dev = NULL;

        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = q->w;
        bi.bmiHeader.biHeight      = -q->h;   /* top-down */
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bmp && bits) {
            const unsigned char *s = fz_pixmap_samples(ctx, pix);
            int n = pix->n, stride = pix->stride, x, y;
            for (y = 0; y < q->h; y++) {
                const unsigned char *sp = s + (size_t)y * (size_t)stride;
                unsigned char *dp = (unsigned char *)bits + (size_t)y * q->w * 4;
                for (x = 0; x < q->w; x++) {
                    unsigned char r, g, b2;
                    if (n >= 3) { r = sp[x*n]; g = sp[x*n+1]; b2 = sp[x*n+2]; }
                    else        { r = g = b2 = sp[x*n]; }
                    dp[x*4+0] = b2;
                    dp[x*4+1] = g;
                    dp[x*4+2] = r;
                    dp[x*4+3] = 255;
                }
            }
            ok = 0;
        }
    }
    fz_catch(ctx) {
        ok = -1;
    }

    if (pix) fz_drop_pixmap(ctx, pix);
    if (dev) fz_drop_device(ctx, dev);
    if (ok != 0) {
        if (bmp) DeleteObject(bmp);
        return -1;
    }
    *out = bmp;
    return 0;
}

static DWORD WINAPI worker_main(LPVOID param)
{
    Worker *w = (Worker *)param;
    RenderPool *p = w->pool;

    w->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!w->ctx) return 0;
    fz_register_document_handlers(w->ctx);
    fz_set_aa_level(w->ctx, 8);
    w->doc = NULL; w->page = NULL; w->page_no = -1;

    {
        fz_stream *stm = NULL;
        fz_try(w->ctx) {
            stm = fz_open_memory(w->ctx, p->data, p->len);
            w->doc = fz_open_document_with_stream(w->ctx, "application/pdf", stm);
        }
        fz_catch(w->ctx) {
            w->doc = NULL;
        }
        if (stm) fz_drop_stream(w->ctx, stm);
    }

    if (w->doc) {
        for (;;) {
            ReqNode *n = NULL;
            if (!pool_pop(p, &n)) break;         /* 退出信号 */
            if (p->quit) { if (n) free(n); break; }
            if (n->gen == p->gen) {
                HBITMAP bmp = NULL;
                if (render_tile(w, n, &bmp) == 0 && bmp)
                    pool_push_result(p, n, bmp);
            }
            free(n);
        }
    }

    if (w->page) fz_drop_page(w->ctx, w->page);
    if (w->doc)  fz_drop_document(w->ctx, w->doc);
    fz_drop_context(w->ctx);
    return 0;
}

/* ------------------------- 生命周期 ------------------------- */

RenderPool *pool_create(HWND hWnd, const unsigned char *data, size_t len,
                        int nthreads)
{
    RenderPool *p;
    int i;

    p = (RenderPool *)calloc(1, sizeof(RenderPool));
    if (!p) return NULL;

    p->hWnd = hWnd;
    p->data = data;
    p->len = len;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > MAX_WORKERS) nthreads = MAX_WORKERS;
    p->nthreads = nthreads;
    p->gen = 1;
    p->quit = 0;

    p->cache = (TileEntry *)calloc(MAX_TILES, sizeof(TileEntry));
    p->pending = (Pending *)calloc(MAX_PENDING, sizeof(Pending));
    if (!p->cache || !p->pending) {
        free(p->cache); free(p->pending); free(p);
        return NULL;
    }
    p->pendCap = MAX_PENDING;

    InitializeCriticalSection(&p->reqLock);
    InitializeCriticalSection(&p->resLock);
    p->sem = CreateSemaphoreW(NULL, 0, 0x7fffffff, NULL);
    if (!p->sem) {
        DeleteCriticalSection(&p->reqLock);
        DeleteCriticalSection(&p->resLock);
        free(p->cache); free(p->pending); free(p);
        return NULL;
    }

    for (i = 0; i < nthreads; i++) {
        p->workers[i].pool = p;
        p->threads[i] = CreateThread(NULL, 0, worker_main, &p->workers[i], 0, NULL);
        if (!p->threads[i]) { p->nthreads = i; break; }
    }
    if (p->nthreads < 1) {
        pool_destroy(p);
        return NULL;
    }
    return p;
}

void pool_destroy(RenderPool *p)
{
    int i;
    ResNode *r;
    if (!p) return;

    InterlockedExchange(&p->quit, 1);
    for (i = 0; i < p->nthreads; i++)
        ReleaseSemaphore(p->sem, 1, NULL);

    if (p->nthreads > 0)
        WaitForMultipleObjects((DWORD)p->nthreads, p->threads, TRUE, INFINITE);
    for (i = 0; i < p->nthreads; i++)
        if (p->threads[i]) CloseHandle(p->threads[i]);

    /* 清结果队列 */
    r = p->resHead;
    while (r) {
        ResNode *nx = r->next;
        if (r->bmp) DeleteObject(r->bmp);
        free(r);
        r = nx;
    }
    /* 清请求队列 */
    {
        ReqNode *q = p->reqHead;
        while (q) { ReqNode *nx = q->next; free(q); q = nx; }
    }
    /* 清缓存 */
    for (i = 0; i < p->ncache; i++)
        if (p->cache[i].bmp) DeleteObject(p->cache[i].bmp);

    if (p->sem) CloseHandle(p->sem);
    DeleteCriticalSection(&p->reqLock);
    DeleteCriticalSection(&p->resLock);
    free(p->cache);
    free(p->pending);
    free(p);
}

/* ------------------------- UI 侧 ------------------------- */

void pool_set_gen(RenderPool *p, LONG gen)
{
    if (!p) return;
    InterlockedExchange(&p->gen, gen);
    p->npend = 0;   /* 作废所有待处理记录 */
}

void pool_clear(RenderPool *p)
{
    int i;
    if (!p) return;
    for (i = 0; i < p->ncache; i++)
        if (p->cache[i].bmp) DeleteObject(p->cache[i].bmp);
    p->ncache = 0;
    p->npend = 0;
}

static int pending_has(RenderPool *p, int page, float zoom, int x, int y)
{
    int i;
    for (i = 0; i < p->npend; i++)
        if (p->pending[i].page == page && zoom_eq(p->pending[i].zoom, zoom) &&
            p->pending[i].x == x && p->pending[i].y == y)
            return 1;
    return 0;
}

static void pending_remove(RenderPool *p, int page, float zoom, int x, int y)
{
    int i;
    for (i = 0; i < p->npend; i++) {
        if (p->pending[i].page == page && zoom_eq(p->pending[i].zoom, zoom) &&
            p->pending[i].x == x && p->pending[i].y == y) {
            p->pending[i] = p->pending[p->npend - 1];
            p->npend--;
            return;
        }
    }
}

void pool_request(RenderPool *p, int page, float zoom,
                  int x, int y, int w, int h)
{
    ReqNode *n;

    if (!p || p->quit || w <= 0 || h <= 0) return;
    if (pending_has(p, page, zoom, x, y)) return;
    if (p->npend >= p->pendCap) return;

    p->pending[p->npend].page = page;
    p->pending[p->npend].zoom = zoom;
    p->pending[p->npend].x = x;
    p->pending[p->npend].y = y;
    p->npend++;

    n = (ReqNode *)malloc(sizeof(ReqNode));
    if (!n) { p->npend--; return; }
    n->page = page; n->zoom = zoom;
    n->x = x; n->y = y; n->w = w; n->h = h;
    n->gen = p->gen; n->next = NULL;

    EnterCriticalSection(&p->reqLock);
    if (p->reqTail) p->reqTail->next = n; else p->reqHead = n;
    p->reqTail = n;
    LeaveCriticalSection(&p->reqLock);

    ReleaseSemaphore(p->sem, 1, NULL);
}

HBITMAP pool_lookup(RenderPool *p, int page, float zoom, int x, int y,
                    int *ow, int *oh)
{
    int i;
    if (!p) return NULL;
    p->tick++;
    for (i = 0; i < p->ncache; i++) {
        if (p->cache[i].page == page && zoom_eq(p->cache[i].zoom, zoom) &&
            p->cache[i].x == x && p->cache[i].y == y) {
            p->cache[i].lastUse = p->tick;
            if (ow) *ow = p->cache[i].w;
            if (oh) *oh = p->cache[i].h;
            return p->cache[i].bmp;
        }
    }
    return NULL;
}

static void cache_insert(RenderPool *p, const ResNode *r)
{
    int i, evict = -1;
    ULONGLONG oldest = 0;

    for (i = 0; i < p->ncache; i++) {
        if (p->cache[i].page == r->page && zoom_eq(p->cache[i].zoom, r->zoom) &&
            p->cache[i].x == r->x && p->cache[i].y == r->y) {
            if (p->cache[i].bmp) DeleteObject(p->cache[i].bmp);
            p->cache[i].bmp = r->bmp;
            p->cache[i].w = r->w;
            p->cache[i].h = r->h;
            p->cache[i].lastUse = ++p->tick;
            return;
        }
    }

    if (p->ncache >= MAX_TILES) {
        for (i = 0; i < p->ncache; i++) {
            if (evict < 0 || p->cache[i].lastUse < oldest) {
                oldest = p->cache[i].lastUse;
                evict = i;
            }
        }
        if (evict >= 0) {
            if (p->cache[evict].bmp) DeleteObject(p->cache[evict].bmp);
            p->cache[evict] = p->cache[p->ncache - 1];
            p->ncache--;
        }
    }
    if (p->ncache >= MAX_TILES) {   /* 仍满则放弃 */
        DeleteObject(r->bmp);
        return;
    }

    p->cache[p->ncache].page = r->page;
    p->cache[p->ncache].zoom = r->zoom;
    p->cache[p->ncache].x = r->x;
    p->cache[p->ncache].y = r->y;
    p->cache[p->ncache].w = r->w;
    p->cache[p->ncache].h = r->h;
    p->cache[p->ncache].bmp = r->bmp;
    p->cache[p->ncache].lastUse = ++p->tick;
    p->ncache++;
}

void pool_drain(RenderPool *p)
{
    ResNode *list;
    if (!p) return;

    EnterCriticalSection(&p->resLock);
    list = p->resHead;
    p->resHead = p->resTail = NULL;
    LeaveCriticalSection(&p->resLock);

    while (list) {
        ResNode *nx = list->next;
        pending_remove(p, list->page, list->zoom, list->x, list->y);
        if (list->gen == p->gen)
            cache_insert(p, list);
        else if (list->bmp)
            DeleteObject(list->bmp);
        free(list);
        list = nx;
    }
}
