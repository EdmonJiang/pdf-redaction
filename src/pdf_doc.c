/*
 * pdf_doc.c - MuPDF 封装: 打开 / 页信息 / 高分辨率展平导出
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include "pdf_doc.h"
#include "render_pool.h"

/* ------------------------- 内置字体 ------------------------- */

static char g_fontDir[MAX_PATH] = "";
static unsigned char *g_fontCache[128];
static int g_fontCacheN = 0;

static const char *FORGES[] = { "urw", "droid", "noto", "sil", "han", NULL };

static const unsigned char *loadBuiltinFont(const char *name, int *size)
{
    char path[MAX_PATH * 2];
    int i;
    FILE *f;
    long n;
    unsigned char *b;

    *size = 0;
    if (!name || !g_fontDir[0]) return NULL;
    for (i = 0; FORGES[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s/%s", g_fontDir, FORGES[i], name);
        f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n <= 0) { fclose(f); continue; }
        b = (unsigned char *)malloc((size_t)n);
        if (!b) { fclose(f); continue; }
        if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); continue; }
        fclose(f);
        *size = (int)n;
        if (g_fontCacheN < (int)(sizeof(g_fontCache) / sizeof(g_fontCache[0])))
            g_fontCache[g_fontCacheN++] = b;
        return b;
    }
    return NULL;
}

/* noto_sumatra.c 提供 */
extern void fz_set_builtin_font_loader(
    const unsigned char *(*loader)(const char *file_name, int *size));

int pdf_doc_global_init(const char *fontDir)
{
    fz_set_builtin_font_loader(loadBuiltinFont);
    if (fontDir) {
        strncpy(g_fontDir, fontDir, sizeof(g_fontDir) - 1);
        g_fontDir[sizeof(g_fontDir) - 1] = 0;
    }
    return 0;
}

void pdf_doc_global_shutdown(void)
{
    int i;
    for (i = 0; i < g_fontCacheN; i++) free(g_fontCache[i]);
    g_fontCacheN = 0;
}

/* ------------------------- 错误信息 ------------------------- */

static void set_err(wchar_t *err, size_t n, const wchar_t *msg)
{
    if (!err || !n) return;
    wcsncpy(err, msg, n - 1);
    err[n - 1] = 0;
}

static void set_err_utf8(wchar_t *err, size_t n, fz_context *ctx)
{
    if (!err || !n) return;
    err[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, fz_caught_message(ctx), -1, err, (int)n);
    err[n - 1] = 0;
}

/* ------------------------- 打开 / 关闭 ------------------------- */

int pdf_doc_open(App *app, const wchar_t *path, wchar_t *err, size_t errMax)
{
    HANDLE fh;
    DWORD size, got;
    unsigned char *buf = NULL;
    fz_context *ctx = NULL;
    fz_document *doc = NULL;
    fz_stream *stm = NULL;
    int npages = 0;
    PageRect *bounds = NULL;
    int i;

    fz_var(stm);
    fz_var(doc);
    fz_var(npages);

    if (err && errMax) err[0] = 0;

    fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        set_err(err, errMax, L"无法打开文件。");
        return -1;
    }
    size = GetFileSize(fh, NULL);
    if (size == INVALID_FILE_SIZE || size < 16 ||
        size > 1000u * 1024u * 1024u) {
        CloseHandle(fh);
        set_err(err, errMax, L"文件大小异常或无法读取。");
        return -1;
    }
    buf = (unsigned char *)malloc(size);
    if (!buf) {
        CloseHandle(fh);
        set_err(err, errMax, L"内存不足。");
        return -1;
    }
    if (!ReadFile(fh, buf, size, &got, NULL) || got != size) {
        CloseHandle(fh);
        free(buf);
        set_err(err, errMax, L"读取文件失败。");
        return -1;
    }
    CloseHandle(fh);

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx) {
        free(buf);
        set_err(err, errMax, L"MuPDF 初始化失败。");
        return -1;
    }
    fz_register_document_handlers(ctx);

    fz_try(ctx) {
        stm = fz_open_memory(ctx, buf, size);
        doc = fz_open_document_with_stream(ctx, "application/pdf", stm);
        fz_drop_stream(ctx, stm);
        stm = NULL;
        npages = fz_count_pages(ctx, doc);
        if (npages < 1)
            fz_throw(ctx, FZ_ERROR_FORMAT, "PDF has no pages");
    }
    fz_catch(ctx) {
        set_err_utf8(err, errMax, ctx);
        if (stm) fz_drop_stream(ctx, stm);
        if (doc) fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        free(buf);
        return -1;
    }

    bounds = (PageRect *)calloc((size_t)npages, sizeof(PageRect));
    if (!bounds) {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        free(buf);
        set_err(err, errMax, L"内存不足。");
        return -1;
    }
    app->pageX = (float *)calloc((size_t)npages, sizeof(float));
    app->pageY = (float *)calloc((size_t)npages, sizeof(float));
    if (!app->pageX || !app->pageY) {
        free(app->pageX); app->pageX = NULL;
        free(app->pageY); app->pageY = NULL;
        free(bounds);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        free(buf);
        set_err(err, errMax, L"内存不足。");
        return -1;
    }

    for (i = 0; i < npages; i++) {
        fz_page *pg = NULL;
        fz_var(pg);
        fz_try(ctx) {
            fz_rect b;
            pg = fz_load_page(ctx, doc, i);
            b = fz_bound_page(ctx, pg);
            bounds[i].x0 = b.x0; bounds[i].y0 = b.y0;
            bounds[i].x1 = b.x1; bounds[i].y1 = b.y1;
        }
        fz_catch(ctx) {
            bounds[i].x0 = 0; bounds[i].y0 = 0;
            bounds[i].x1 = 612; bounds[i].y1 = 792;
        }
        if (pg) fz_drop_page(ctx, pg);
    }

    app->ctx = ctx;
    app->data = buf;
    app->len = size;
    app->doc = doc;
    app->npages = npages;
    app->bounds = bounds;
    wcsncpy(app->srcPath, path, MAX_PATH - 1);
    app->srcPath[MAX_PATH - 1] = 0;
    app->cur_page = 0;
    app->scrollX = 0;
    app->scrollY = 0;
    app->contentW = app->contentH = 0;
    app->layoutValid = 0;

    app->gen = 1;
    if (app->hWnd)
        app->pool = pool_create(app->hWnd, buf, size, 4);
    if (app->pool) pool_set_gen(app->pool, app->gen);
    return 0;
}

void pdf_doc_close(App *app)
{
    if (!app) return;
    if (app->pool) { pool_destroy(app->pool); app->pool = NULL; }
    if (app->doc && app->ctx) { fz_drop_document(app->ctx, app->doc); app->doc = NULL; }
    if (app->ctx) { fz_drop_context(app->ctx); app->ctx = NULL; }
    free(app->data); app->data = NULL; app->len = 0;
    free(app->bounds); app->bounds = NULL; app->npages = 0;
    free(app->pageX); app->pageX = NULL;
    free(app->pageY); app->pageY = NULL;
    app->contentW = app->contentH = 0;
    app->layoutValid = 0;
    app->cur_page = 0;
}

/* ------------------------- 导出 ------------------------- */

static uint32_t *pix_to_buf(fz_context *ctx, fz_pixmap *pix)
{
    int W = pix->w, H = pix->h, n = pix->n, stride = pix->stride, x, y;
    const unsigned char *s = fz_pixmap_samples(ctx, pix);
    uint32_t *buf = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)W * (size_t)H);
    if (!buf) return NULL;
    for (y = 0; y < H; y++) {
        const unsigned char *sp = s + (size_t)y * (size_t)stride;
        uint32_t *row = buf + (size_t)y * (size_t)W;
        for (x = 0; x < W; x++) {
            unsigned char r, g, b;
            if (n >= 3) { r = sp[x*n]; g = sp[x*n+1]; b = sp[x*n+2]; }
            else        { r = g = b = sp[x*n]; }
            row[x] = 0xFF000000u | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return buf;
}

static void buf_to_pix(fz_context *ctx, fz_pixmap *pix, const uint32_t *buf)
{
    int W = pix->w, H = pix->h, n = pix->n, stride = pix->stride, x, y;
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    for (y = 0; y < H; y++) {
        unsigned char *sp = s + (size_t)y * (size_t)stride;
        const uint32_t *row = buf + (size_t)y * (size_t)W;
        for (x = 0; x < W; x++) {
            uint32_t c = row[x];
            if (n >= 3) {
                sp[x*n+0] = (unsigned char)((c >> 16) & 255);
                sp[x*n+1] = (unsigned char)((c >> 8) & 255);
                sp[x*n+2] = (unsigned char)(c & 255);
            } else {
                sp[x*n] = (unsigned char)((c >> 16) & 255);
            }
        }
    }
}

int pdf_doc_export(App *app, const wchar_t *outPath, float exportZoom,
                   int jpeg, wchar_t *err, size_t errMax)
{
    fz_context *ctx;
    fz_document *fzdoc;
    pdf_document *src;
    pdf_document *dst = NULL;
    pdf_graft_map *map = NULL;
    pdf_write_options wopts;
    char path8[MAX_PATH * 3];
    int i, rc = -1;

    if (err && errMax) err[0] = 0;
    if (!app || !app->doc || !app->ctx) {
        set_err(err, errMax, L"\u6ca1\u6709\u5df2\u6253\u5f00\u7684 PDF\u3002");
        return -1;
    }
    ctx = app->ctx;
    fzdoc = app->doc;
    src = pdf_specifics(ctx, fzdoc);   /* PDF 源才有原页可直接拷贝 */

    if (WideCharToMultiByte(CP_UTF8, 0, outPath, -1, path8, sizeof(path8),
                            NULL, NULL) == 0) {
        set_err(err, errMax, L"\u8f93\u51fa\u8def\u5f84\u8f6c\u6362\u5931\u8d25\u3002");
        return -1;
    }

    wopts = pdf_default_write_options;
    wopts.do_compress = 1;
    wopts.do_garbage = 1;

    fz_try(ctx) {
        dst = pdf_create_document(ctx);
        if (src)
            map = pdf_new_graft_map(ctx, dst);

        for (i = 0; i < app->npages; i++) {
            int modified = (rd_op_count(&app->rd, i) > 0);

            if (!modified && src) {
                /*
                 * 未修改页: 直接把原页 (含字体/图像/内容流) 深拷贝进输出。
                 * 不重新渲染, 因此速度快、内存小、输出体积小, 也不会
                 * 触发本构建里不支持的“字体替换”。
                 */
                pdf_graft_mapped_page(ctx, map, -1, src, i);
            } else {
                /*
                 * 被修改页: 高分辨率展平为整页图像, 被遮盖处的原始内容
                 * 不进入输出文件 (真遮盖)。
                 */
                fz_page *page = NULL;
                fz_pixmap *pix = NULL;
                fz_image *img = NULL;
                fz_device *dev = NULL;
                pdf_obj *res = NULL;
                fz_buffer *cont = NULL;
                uint32_t *buf = NULL;
                fz_rect b;
                float bw, bh;

                fz_var(page); fz_var(pix); fz_var(img); fz_var(dev);
                fz_var(res); fz_var(cont); fz_var(buf);

                fz_try(ctx) {
                    page = fz_load_page(ctx, fzdoc, i);
                    b = fz_bound_page(ctx, page);
                    bw = b.x1 - b.x0;
                    bh = b.y1 - b.y0;
                    if (bw < 1) bw = 1;
                    if (bh < 1) bh = 1;

                    pix = fz_new_pixmap_from_page(ctx, page,
                            fz_scale(exportZoom, exportZoom),
                            fz_device_rgb(ctx), 0);
                    buf = pix_to_buf(ctx, pix);
                    if (!buf) fz_throw(ctx, FZ_ERROR_SYSTEM, "out of memory");
                    rd_apply_bits(&app->rd, i, exportZoom, b.x0, b.y0,
                                  0.0f, 0.0f, buf, pix->w, pix->h);
                    buf_to_pix(ctx, pix, buf);
                    free(buf);
                    buf = NULL;

                    if (jpeg) {
                        fz_buffer *jb = fz_new_buffer_from_pixmap_as_jpeg(
                            ctx, pix, fz_default_color_params, 90, 0);
                        img = fz_new_image_from_buffer(ctx, jb);
                        fz_drop_buffer(ctx, jb);
                    } else {
                        img = fz_new_image_from_pixmap(ctx, pix, NULL);
                    }

                    dev = pdf_page_write(ctx, dst, fz_make_rect(0, 0, bw, bh),
                                         &res, &cont);
                    fz_fill_image(ctx, dev, img, fz_scale(bw, bh),
                                  1.0f, fz_default_color_params);
                    fz_close_device(ctx, dev);
                    fz_drop_device(ctx, dev);
                    dev = NULL;

                    {
                        pdf_obj *pageobj = pdf_add_page(ctx, dst,
                                fz_make_rect(0, 0, bw, bh), 0, res, cont);
                        pdf_drop_obj(ctx, res);
                        res = NULL;
                        fz_drop_buffer(ctx, cont);
                        cont = NULL;
                        pdf_insert_page(ctx, dst, -1, pageobj);
                        pdf_drop_obj(ctx, pageobj);
                    }
                }
                fz_always(ctx) {
                    if (dev) fz_drop_device(ctx, dev);
                    if (res) pdf_drop_obj(ctx, res);
                    if (cont) fz_drop_buffer(ctx, cont);
                    if (img) fz_drop_image(ctx, img);
                    if (buf) free(buf);
                    if (pix) fz_drop_pixmap(ctx, pix);
                    if (page) fz_drop_page(ctx, page);
                }
                fz_catch(ctx) {
                    fz_rethrow(ctx);
                }
            }
        }

        pdf_save_document(ctx, dst, path8, &wopts);
        rc = 0;
    }
    fz_catch(ctx) {
        set_err_utf8(err, errMax, ctx);
        rc = -1;
    }

    if (map) pdf_drop_graft_map(ctx, map);
    if (dst) pdf_drop_document(ctx, dst);
    return rc;
}
