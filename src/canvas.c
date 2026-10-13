/*
 * canvas.c - 连续纵向滚动预览、坐标换算、鼠标交互、滚动条
 */
#include <windows.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "redact.h"
#include "render_pool.h"
#include "canvas.h"
#include "ui.h"

#define CMIN(a,b) ((a) < (b) ? (a) : (b))
#define CMAX(a,b) ((a) > (b) ? (a) : (b))

/* 复用的预览表面 (32bpp DIB, 便于像素级打码) */
static HDC      s_mem  = NULL;
static HBITMAP  s_surf = NULL;
static HBITMAP  s_prev = NULL;
static int      s_sw = 0, s_sh = 0;
static void    *s_bits = NULL;

static float page_gap(void) { return S(20); }
static float page_pad(void) { return S(12); }

static void ensure_surf(HDC dc, int w, int h)
{
    BITMAPINFO bi;
    if (!s_mem) s_mem = CreateCompatibleDC(dc);
    if (s_surf && (s_sw != w || s_sh != h)) {
        SelectObject(s_mem, s_prev);
        DeleteObject(s_surf);
        s_surf = NULL;
    }
    if (!s_surf) {
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;    /* top-down */
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        s_surf = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &s_bits, NULL, 0);
        if (s_surf) s_prev = (HBITMAP)SelectObject(s_mem, s_surf);
        s_sw = w; s_sh = h;
    }
}

void canvas_shutdown(void)
{
    if (s_surf && s_mem) { SelectObject(s_mem, s_prev); DeleteObject(s_surf); }
    s_surf = NULL;
    if (s_mem) { DeleteDC(s_mem); s_mem = NULL; }
}

/* 预览区: 客户区去掉顶部工具栏与右侧滚动条 */
RECT canvas_rect(void)
{
    RECT rc;
    int top = ui_toolbar_height();
    int sbw = GetSystemMetrics(SM_CXVSCROLL);
    GetClientRect(g_app.hWnd, &rc);
    if (rc.top < top) rc.top = top;
    rc.right -= sbw;
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

static void canvas_invalidate(App *a)
{
    RECT cr = canvas_rect();
    InvalidateRect(a->hWnd, &cr, FALSE);
}

void canvas_refresh(App *a)
{
    canvas_invalidate(a);
}

int canvas_hit(const App *a, int x, int y)
{
    RECT cr = canvas_rect();
    (void)a;
    return x >= cr.left && x < cr.right && y >= cr.top && y < cr.bottom;
}

/* ------------------------------ 布局 ------------------------------ */

static void origins(App *a, float *ox, float *oy)
{
    RECT cr = canvas_rect();
    float vw = (float)(cr.right - cr.left);
    float vh = (float)(cr.bottom - cr.top);
    float cx = (vw > a->contentW) ? (vw - a->contentW) * 0.5f : 0.0f;
    float cy = (vh > a->contentH) ? (vh - a->contentH) * 0.5f : 0.0f;
    *ox = (float)cr.left + cx - a->scrollX;
    *oy = (float)cr.top  + cy - a->scrollY;
}

static void clamp_scroll(App *a)
{
    RECT cr = canvas_rect();
    float vw = (float)(cr.right - cr.left);
    float vh = (float)(cr.bottom - cr.top);
    float maxY = a->contentH - vh; if (maxY < 0) maxY = 0;
    float maxX = a->contentW - vw; if (maxX < 0) maxX = 0;
    if (a->scrollY < 0) a->scrollY = 0;
    if (a->scrollY > maxY) a->scrollY = maxY;
    if (a->scrollX < 0) a->scrollX = 0;
    if (a->scrollX > maxX) a->scrollX = maxX;
}

void canvas_relayout(App *a)
{
    float maxw = 0, y = page_pad();
    float gap = page_gap();
    int i;

    if (!a->doc || a->npages <= 0 || !a->pageX || !a->pageY) {
        a->contentW = a->contentH = 0;
        a->layoutValid = 1;
        return;
    }
    for (i = 0; i < a->npages; i++) {
        float w = (a->bounds[i].x1 - a->bounds[i].x0) * a->zoom;
        if (w > maxw) maxw = w;
    }
    a->contentW = maxw;
    for (i = 0; i < a->npages; i++) {
        float w = (a->bounds[i].x1 - a->bounds[i].x0) * a->zoom;
        float h = (a->bounds[i].y1 - a->bounds[i].y0) * a->zoom;
        a->pageX[i] = (maxw - w) * 0.5f;
        a->pageY[i] = y;
        y += h + gap;
    }
    if (a->npages > 0) y -= gap;
    y += page_pad();
    a->contentH = (y < 0) ? 0 : y;
    clamp_scroll(a);
    a->layoutValid = 1;
}

void canvas_sync_scrollbar(App *a)
{
    HWND sb = GetDlgItem(a->hWnd, IDC_VSCROLL);
    RECT cr;
    SCROLLINFO si;

    if (!sb) return;
    if (!a->layoutValid) canvas_relayout(a);
    cr = canvas_rect();
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (int)a->contentH - 1;
    if (si.nMax < 0) si.nMax = 0;
    si.nPage  = (UINT)(cr.bottom - cr.top);
    si.nPos   = (int)(a->scrollY + 0.5f);
    SetScrollInfo(sb, SB_CTL, &si, TRUE);
}

/* 按视口中线判定当前页:
 * 取“页顶 <= 中线”的最后一页 —— 即分隔间隔已越过中线则显示下一页。 */
void canvas_update_current(App *a)
{
    RECT cr;
    float vh, ox, oy, midContent;
    int i, cur = 0;

    if (!a->doc || a->npages <= 0 || !a->pageY) return;
    if (!a->layoutValid) canvas_relayout(a);
    cr = canvas_rect();
    vh = (float)(cr.bottom - cr.top);
    origins(a, &ox, &oy);
    midContent = ((float)cr.top + vh * 0.5f) - oy;
    for (i = 0; i < a->npages; i++) {
        if (a->pageY[i] <= midContent) cur = i;
        else break;
    }
    if (cur != a->cur_page) {
        a->cur_page = cur;
        ui_update(a->hWnd);
    }
}

void canvas_vscroll(App *a, int code, int pos)
{
    RECT cr;
    float vh, maxY, line = S(60);
    HWND sb = GetDlgItem(a->hWnd, IDC_VSCROLL);
    SCROLLINFO si;

    (void)pos;
    if (!a->doc || a->npages <= 0) return;
    if (!a->layoutValid) canvas_relayout(a);
    cr = canvas_rect();
    vh = (float)(cr.bottom - cr.top);
    maxY = a->contentH - vh; if (maxY < 0) maxY = 0;

    switch (code) {
    case SB_LINEUP:      a->scrollY -= line; break;
    case SB_LINEDOWN:    a->scrollY += line; break;
    case SB_PAGEUP:      a->scrollY -= vh;   break;
    case SB_PAGEDOWN:    a->scrollY += vh;   break;
    case SB_THUMBTRACK:
        /* WM_VSCROLL 的 HIWORD 位置是 16 位的, 范围大时会截断;
         * 必须用 GetScrollInfo 的 32 位 nTrackPos。 */
        if (sb) {
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(sb, SB_CTL, &si))
                a->scrollY = (float)si.nTrackPos;
        }
        break;
    case SB_THUMBPOSITION:
        if (sb) {
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_POS;
            if (GetScrollInfo(sb, SB_CTL, &si))
                a->scrollY = (float)si.nPos;
        }
        break;
    case SB_TOP:         a->scrollY = 0;     break;
    case SB_BOTTOM:      a->scrollY = maxY;  break;
    default: return;
    }
    if (a->scrollY < 0) a->scrollY = 0;
    if (a->scrollY > maxY) a->scrollY = maxY;
    canvas_sync_scrollbar(a);
    canvas_update_current(a);
    canvas_invalidate(a);
}

void canvas_scroll_delta(App *a, float dy)
{
    RECT cr;
    float maxY;

    if (!a->doc || a->npages <= 0) return;
    if (!a->layoutValid) canvas_relayout(a);
    cr = canvas_rect();
    maxY = a->contentH - (float)(cr.bottom - cr.top);
    if (maxY < 0) maxY = 0;
    a->scrollY += dy;
    if (a->scrollY < 0) a->scrollY = 0;
    if (a->scrollY > maxY) a->scrollY = maxY;
    canvas_sync_scrollbar(a);
    canvas_update_current(a);
    canvas_invalidate(a);
}

void canvas_scroll_to_page(App *a, int page)
{
    RECT cr;
    float maxY, target;

    if (!a->doc || a->npages <= 0 || !a->pageY) return;
    if (!a->layoutValid) canvas_relayout(a);
    if (page < 0) page = 0;
    if (page >= a->npages) page = a->npages - 1;
    cr = canvas_rect();
    target = a->pageY[page] - page_pad();
    if (target < 0) target = 0;
    maxY = a->contentH - (float)(cr.bottom - cr.top);
    if (maxY < 0) maxY = 0;
    if (target > maxY) target = maxY;
    a->scrollY = target;
    if (a->cur_page != page) {
        a->cur_page = page;
        ui_update(a->hWnd);
    }
    canvas_sync_scrollbar(a);
    canvas_invalidate(a);
}

/* ------------------------------ 绘制 ------------------------------ */

static void draw_page_surface(App *a, int i, float mx, float my,
                              int aw, int ah)
{
    PageRect b = a->bounds[i];
    float pw = b.x1 - b.x0, ph = b.y1 - b.y0;
    int W = (int)ceilf(pw * a->zoom);
    int H = (int)ceilf(ph * a->zoom);
    int maxtx, maxty, tx0, ty0, tx1, ty1, tx, ty;

    if (W <= 0 || H <= 0) return;

    /* 白色页面 + 边框 */
    {
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
        HPEN   pen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
        HPEN   op = (HPEN)SelectObject(s_mem, pen);
        HBRUSH ob = (HBRUSH)SelectObject(s_mem, wb);
        Rectangle(s_mem, (int)mx, (int)my, (int)mx + W, (int)my + H);
        SelectObject(s_mem, op);
        SelectObject(s_mem, ob);
        DeleteObject(pen);
        DeleteObject(wb);
    }

    /* 可见瓦片: mem(0,0) 对应图像坐标 (-mx,-my) */
    maxtx = (W - 1) / POOL_TILE;
    maxty = (H - 1) / POOL_TILE;
    tx0 = (int)floorf((-mx) / POOL_TILE);
    ty0 = (int)floorf((-my) / POOL_TILE);
    tx1 = (int)floorf((-mx + aw - 1) / POOL_TILE);
    ty1 = (int)floorf((-my + ah - 1) / POOL_TILE);
    tx0 = CMAX(tx0, 0); ty0 = CMAX(ty0, 0);
    tx1 = CMIN(tx1, maxtx); ty1 = CMIN(ty1, maxty);

    /*
     * 只请求/绘制真正可见的瓦片, 不做额外预取。
     * 预取会放大工作集, 一旦“可见 + 预取”超过 LRU 容量, 就会出现
     * “渲染 → 淘汰 → 再请求”的死循环, 长期占满 CPU。
     */
    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            int x = tx * POOL_TILE, y = ty * POOL_TILE;
            int tw = CMIN(POOL_TILE, W - x), th = CMIN(POOL_TILE, H - y);
            int cw = 0, ch = 0;
            HBITMAP tile;
            if (tw <= 0 || th <= 0) continue;
            tile = pool_lookup(a->pool, i, a->zoom, x, y, &cw, &ch);
            if (tile) {
                HDC tdc = CreateCompatibleDC(s_mem);
                HBITMAP ob = (HBITMAP)SelectObject(tdc, tile);
                BitBlt(s_mem, (int)(mx + x + 0.5f), (int)(my + y + 0.5f),
                       cw, ch, tdc, 0, 0, SRCCOPY);
                SelectObject(tdc, ob);
                DeleteDC(tdc);
                continue;
            }
            pool_request(a->pool, i, a->zoom, x, y, tw, th);
        }
    }
}

void canvas_paint(App *a, HDC dc)
{
    RECT cr = canvas_rect();
    int aw = cr.right - cr.left;
    int ah = cr.bottom - cr.top;
    int i;

    if (aw <= 0 || ah <= 0) return;
    ensure_surf(dc, aw, ah);
    if (!s_surf) return;

    {
        HBRUSH bg = CreateSolidBrush(RGB(240, 240, 240));
        RECT r = { 0, 0, aw, ah };
        FillRect(s_mem, &r, bg);
        DeleteObject(bg);
    }

    if (a->doc && a->npages > 0 && a->pageX && a->pageY) {
        float ox, oy;
        if (!a->layoutValid) canvas_relayout(a);
        origins(a, &ox, &oy);

        /* 第一遍: GDI 绘制页面与瓦片 */
        for (i = 0; i < a->npages; i++) {
            float w = (a->bounds[i].x1 - a->bounds[i].x0) * a->zoom;
            float h = (a->bounds[i].y1 - a->bounds[i].y0) * a->zoom;
            float mx = ox + a->pageX[i] - cr.left;
            float my = oy + a->pageY[i] - cr.top;
            if (mx + w < -1 || mx > aw || my + h < -1 || my > ah) continue;
            draw_page_surface(a, i, mx, my, aw, ah);
        }

        GdiFlush();

        /* 第二遍: 直接写像素应用打码 */
        {
            uint32_t *bits = (uint32_t *)s_bits;
            for (i = 0; i < a->npages; i++) {
                float w = (a->bounds[i].x1 - a->bounds[i].x0) * a->zoom;
                float h = (a->bounds[i].y1 - a->bounds[i].y0) * a->zoom;
                float mx = ox + a->pageX[i] - cr.left;
                float my = oy + a->pageY[i] - cr.top;
                if (mx + w < -1 || mx > aw || my + h < -1 || my > ah) continue;
                rd_apply_bits(&a->rd, i, a->zoom, a->bounds[i].x0, a->bounds[i].y0,
                              mx, my, bits, aw, ah);
            }
            if (a->liveActive && a->live.page >= 0 && a->live.page < a->npages) {
                int lp = a->live.page;
                float mx = ox + a->pageX[lp] - cr.left;
                float my = oy + a->pageY[lp] - cr.top;
                rd_apply_op_bits(&a->live, a->zoom,
                                 a->bounds[lp].x0, a->bounds[lp].y0,
                                 mx, my, bits, aw, ah);
            }
        }
    } else {
        HFONT f = CreateFontW(S(-16), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                              L"Microsoft YaHei");
        HFONT of = (HFONT)SelectObject(s_mem, f);
        RECT tr = { 0, ah / 2 - 20, aw, ah / 2 + 20 };
        SetTextColor(s_mem, RGB(120, 120, 120));
        SetBkMode(s_mem, TRANSPARENT);
        DrawTextW(s_mem, L"点击【打开 PDF】选择文件，然后用笔刷 / 矩形 / 马赛克遮盖敏感信息",
                  -1, &tr, DT_CENTER | DT_SINGLELINE);
        SelectObject(s_mem, of);
        DeleteObject(f);
    }

    BitBlt(dc, cr.left, cr.top, aw, ah, s_mem, 0, 0, SRCCOPY);
}

/* ------------------------------ 视图 ------------------------------ */

void canvas_fit_width(App *a)
{
    RECT cr;
    PageRect b;
    float pw, zmin, zmax;

    if (!a->doc || a->npages <= 0) return;
    cr = canvas_rect();
    b = a->bounds[a->cur_page];
    pw = b.x1 - b.x0;
    if (pw < 1) return;
    zmin = 0.25f * (float)g_dpi / 72.0f;   /* 25%  */
    zmax = 5.00f * (float)g_dpi / 72.0f;   /* 500% */
    a->zoom = ((float)(cr.right - cr.left) - S(24)) / pw;
    if (a->zoom < zmin) a->zoom = zmin;
    if (a->zoom > zmax) a->zoom = zmax;
    a->fitWidth = 1;
    a->layoutValid = 0;
    canvas_relayout(a);
}

void canvas_zoom_set(App *a, float zoom)
{
    float zmin = 0.25f * (float)g_dpi / 72.0f;   /*  25% */
    float zmax = 5.00f * (float)g_dpi / 72.0f;   /* 500% */
    if (zoom < zmin) zoom = zmin;
    if (zoom > zmax) zoom = zmax;
    a->zoom = zoom;
    a->fitWidth = 0;
    a->gen++;
    if (a->pool) { pool_set_gen(a->pool, a->gen); pool_clear(a->pool); }
    a->layoutValid = 0;
    canvas_relayout(a);
    canvas_scroll_to_page(a, a->cur_page);
    ui_update(a->hWnd);
    canvas_invalidate(a);
}

/* ------------------------------ 交互 ------------------------------ */

static int page_from_screen(App *a, int x, int y, float *px, float *py)
{
    float ox, oy;
    int i;
    if (!a->layoutValid) canvas_relayout(a);
    origins(a, &ox, &oy);
    for (i = 0; i < a->npages; i++) {
        float w = (a->bounds[i].x1 - a->bounds[i].x0) * a->zoom;
        float h = (a->bounds[i].y1 - a->bounds[i].y0) * a->zoom;
        float sx = ox + a->pageX[i];
        float sy = oy + a->pageY[i];
        if ((float)x >= sx && (float)x <= sx + w &&
            (float)y >= sy && (float)y <= sy + h) {
            *px = a->bounds[i].x0 + ((float)x - sx) / a->zoom;
            *py = a->bounds[i].y0 + ((float)y - sy) / a->zoom;
            return i;
        }
    }
    return -1;
}

void canvas_mouse_down(App *a, int x, int y)
{
    float px, py;
    int page;

    if (!a->doc || a->npages <= 0) return;
    if (!canvas_hit(a, x, y)) return;

    page = page_from_screen(a, x, y, &px, &py);
    if (page < 0) return;

    rd_op_free(&a->live);
    rd_op_init(&a->live);
    a->live.kind = (RedactKind)a->tool;
    a->live.page = page;
    a->live.color[0] = a->color[0];
    a->live.color[1] = a->color[1];
    a->live.color[2] = a->color[2];
    a->live.width = a->brushWidth;
    a->live.block = a->mosaicBlock;

    if (a->tool == ROP_STROKE)
        rd_op_add_point(&a->live, px, py);
    else
        rd_op_set_rect(&a->live, px, py, px, py);

    a->liveActive = 1;
    a->dragging = 1;
    SetCapture(a->hWnd);
    canvas_invalidate(a);
}

void canvas_mouse_move(App *a, int x, int y)
{
    float ox, oy;

    if (a->panning) {
        a->scrollX = a->panScrollX0 - (float)(x - a->panStart.x);
        a->scrollY = a->panScrollY0 - (float)(y - a->panStart.y);
        clamp_scroll(a);
        canvas_sync_scrollbar(a);
        canvas_update_current(a);
        canvas_invalidate(a);
        return;
    }
    if (!a->liveActive) return;

    if (!a->layoutValid) canvas_relayout(a);
    origins(a, &ox, &oy);

    {
        int lp = a->live.page;
        float sx = ox + a->pageX[lp];
        float sy = oy + a->pageY[lp];
        float px = a->bounds[lp].x0 + ((float)x - sx) / a->zoom;
        float py = a->bounds[lp].y0 + ((float)y - sy) / a->zoom;

        if (a->live.kind == ROP_STROKE) {
            int n = a->live.npts;
            if (n > 0) {
                float lx = a->live.pts[(n - 1) * 2 + 0];
                float ly = a->live.pts[(n - 1) * 2 + 1];
                float dx = px - lx, dy = py - ly;
                float minD = 1.0f / a->zoom;
                if (dx * dx + dy * dy < minD * minD) return;
            }
            rd_op_add_point(&a->live, px, py);
        } else {
            if (a->live.npts >= 2) {
                a->live.pts[2] = px;
                a->live.pts[3] = py;
            }
        }
    }
    canvas_invalidate(a);
}

void canvas_mouse_up(App *a, int x, int y)
{
    (void)x; (void)y;

    if (a->panning) {
        a->panning = 0;
        ReleaseCapture();
        return;
    }
    if (!a->liveActive) return;

    a->liveActive = 0;
    a->dragging = 0;
    ReleaseCapture();

    {
        int ok = 0;
        if (a->live.kind == ROP_STROKE) {
            ok = a->live.npts >= 1;
        } else if (a->live.npts >= 2) {
            float dx = a->live.pts[2] - a->live.pts[0];
            float dy = a->live.pts[3] - a->live.pts[1];
            float minD = 0.5f / a->zoom;
            ok = (dx * dx + dy * dy) > (minD * minD);
        }
        if (ok) rd_push(&a->rd, &a->live);
    }
    rd_op_free(&a->live);
    rd_op_init(&a->live);

    ui_update(a->hWnd);
    canvas_invalidate(a);
}

void canvas_mouse_capture_lost(App *a)
{
    if (a->liveActive) {
        a->liveActive = 0;
        rd_op_free(&a->live);
        rd_op_init(&a->live);
        canvas_invalidate(a);
    }
    a->panning = 0;
    a->dragging = 0;
}
