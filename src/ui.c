/*
 * ui.c - 控件创建 / 布局 / 状态 / 命令处理
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "canvas.h"
#include "pdf_doc.h"
#include "ui.h"
#include "version.h"

#define TOOLBAR_H 150

/* ------------------------------------------------------------------ */
/* 缩放换算                                                            */
/* ------------------------------------------------------------------ */
static int zoom_to_pct(float z)
{
    int p = (int)(z * 72.0f / (float)g_dpi * 100.0f + 0.5f);
    if (p < 25) p = 25;
    if (p > 500) p = 500;
    return p;
}

static float pct_to_zoom(int p)
{
    return (float)p / 100.0f * (float)g_dpi / 72.0f;
}

int ui_toolbar_height(void)
{
    return S(TOOLBAR_H);
}

/* 把纵向滚动条摆到预览区右侧 */
static void ui_layout_scrollbar(HWND hWnd)
{
    RECT rc, cr;
    int sbw = GetSystemMetrics(SM_CXVSCROLL);
    HWND sb = GetDlgItem(hWnd, IDC_VSCROLL);
    GetClientRect(hWnd, &rc);
    cr = canvas_rect();
    if (sb)
        MoveWindow(sb, rc.right - sbw, cr.top, sbw, rc.bottom - cr.top, TRUE);
}

/* ------------------------------------------------------------------ */
/* 控件创建                                                            */
/* ------------------------------------------------------------------ */
static void setup_track(HWND hWnd, int id, int lo, int hi, int val)
{
    HWND t = GetDlgItem(hWnd, id);
    if (!t) return;
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(t, TBM_SETPOS, TRUE, (LPARAM)val);
}

static void ui_create(HWND hWnd)
{
    HFONT hFont = CreateFontW(S(-14), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                              L"Microsoft YaHei");
    App *a = &g_app;
    int y1 = S(12), y2 = S(54), y3 = S(90), y4 = S(120);

    a->hWnd = hWnd;

    a->tool = ROP_STROKE;
    a->color[0] = 1.0f; a->color[1] = 0.0f; a->color[2] = 0.0f;
    a->brushWidth = 8.0f;
    a->mosaicBlock = 12.0f;
    a->zoom = 1.333f;
    a->exportDpi = 300;
    a->exportJpeg = 0;
    a->cur_page = 0;
    a->fitWidth = 1;
    a->gen = 1;

    #define MKCLS(cls, txt, id, x, y, w, h, style) \
        do { HWND _c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, \
                            x, y, w, h, hWnd, (HMENU)(INT_PTR)id, g_app.hInst, NULL); \
             if (_c) SendMessageW(_c, WM_SETFONT, (WPARAM)hFont, TRUE); } while (0)

    /* 第一行: 文件 / 翻页 / 撤销 */
    MKCLS(L"BUTTON", L"打开 PDF", IDC_OPEN,       S(12),  y1, S(96), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"保存 PDF", IDC_SAVE,       S(116), y1, S(96), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"◀ 上一页", IDC_PREV,       S(222), y1, S(64), S(30), BS_PUSHBUTTON);
    MKCLS(L"STATIC", L"0 / 0",   IDC_PAGELABEL,   S(290), y1, S(68), S(30), SS_CENTER | SS_CENTERIMAGE);
    MKCLS(L"BUTTON", L"下一页 ▶", IDC_NEXT,       S(362), y1, S(64), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"撤销",     IDC_UNDO,       S(434), y1, S(56), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"重做",     IDC_REDO,       S(494), y1, S(56), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"清除本页", IDC_CLEARPAGE,  S(558), y1, S(76), S(30), BS_PUSHBUTTON);
    MKCLS(L"BUTTON", L"清除全部", IDC_CLEARALL,   S(640), y1, S(76), S(30), BS_PUSHBUTTON);

    /* 第二行: 工具 / 颜色 / 粗细 */
    MKCLS(L"STATIC", L"工具:", 0,                 S(12),  y2 + 2, S(36), S(22), 0);
    MKCLS(L"BUTTON", L"自由笔刷", IDC_TOOL_STROKE, S(52),  y2, S(92), S(26), BS_AUTORADIOBUTTON | WS_GROUP);
    MKCLS(L"BUTTON", L"矩形填充", IDC_TOOL_RECT,   S(148), y2, S(92), S(26), BS_AUTORADIOBUTTON);
    MKCLS(L"BUTTON", L"马赛克",   IDC_TOOL_MOSAIC, S(244), y2, S(72), S(26), BS_AUTORADIOBUTTON);
    MKCLS(L"BUTTON", L"颜色...",  IDC_COLOR,       S(324), y2, S(72), S(26), BS_PUSHBUTTON);
    MKCLS(L"STATIC", L"",         IDC_ST_SWATCH,   S(402), y2, S(24), S(24), SS_OWNERDRAW);
    MKCLS(L"STATIC", L"粗细:",    0,               S(436), y2 + 2, S(56), S(22), 0);
    MKCLS(TRACKBAR_CLASSW, NULL,  IDC_TB_WIDTH,    S(496), y2, S(140), S(24), 0);
    MKCLS(L"STATIC", L"8 pt",     IDC_ST_WIDTH,    S(640), y2 + 2, S(56), S(22), 0);

    /* 第三行: 马赛克块 / 缩放 / 导出 */
    MKCLS(L"STATIC", L"马赛克块:", 0,              S(12),  y3 + 2, S(62), S(22), 0);
    MKCLS(TRACKBAR_CLASSW, NULL,  IDC_TB_BLOCK,    S(76),  y3, S(140), S(24), 0);
    MKCLS(L"STATIC", L"12 pt",    IDC_ST_BLOCK,    S(220), y3 + 2, S(52), S(22), 0);
    MKCLS(L"STATIC", L"缩放:",     0,              S(278), y3 + 2, S(36), S(22), 0);
    MKCLS(TRACKBAR_CLASSW, NULL,  IDC_TB_ZOOM,     S(318), y3, S(140), S(24), 0);
    MKCLS(L"STATIC", L"100 %",    IDC_ST_ZOOM,     S(462), y3 + 2, S(56), S(22), 0);
    MKCLS(L"BUTTON", L"适应宽度", IDC_FITWIDTH,    S(524), y3, S(96), S(24), BS_AUTOCHECKBOX);
    MKCLS(L"BUTTON", L"JPEG 压缩", IDC_CB_JPEG,    S(624), y3, S(120), S(24), BS_AUTOCHECKBOX);
    MKCLS(L"STATIC", L"导出 DPI:", 0,              S(752), y3 + 2, S(58), S(22), 0);
    MKCLS(L"COMBOBOX", NULL,      IDC_CB_EXPORTDPI, S(812), y3, S(86), S(200),
          CBS_DROPDOWNLIST | WS_VSCROLL);

    /* 第四行: 状态 */
    MKCLS(L"STATIC", L"请先打开 PDF 文件。", IDC_ST_INFO, S(12), y4, S(950), S(22), 0);

    /* 右侧纵向滚动条 (仅覆盖预览区) */
    MKCLS(L"SCROLLBAR", NULL, IDC_VSCROLL, 0, 0, S(17), S(17), SBS_VERT);

    #undef MKCLS

    /* trackbar 范围 */
    setup_track(hWnd, IDC_TB_WIDTH, 1, 100, (int)a->brushWidth);
    setup_track(hWnd, IDC_TB_BLOCK, 2, 100, (int)a->mosaicBlock);
    setup_track(hWnd, IDC_TB_ZOOM, 25, 500, zoom_to_pct(a->zoom));

    /* 默认选择 */
    SendDlgItemMessageW(hWnd, IDC_TOOL_STROKE, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageW(hWnd, IDC_FITWIDTH, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageW(hWnd, IDC_CB_JPEG, BM_SETCHECK, BST_UNCHECKED, 0);

    /* 导出 DPI 下拉 */
    {
        HWND cb = GetDlgItem(hWnd, IDC_CB_EXPORTDPI);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"300 DPI");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"200 DPI");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"150 DPI");
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
    }
}

/* ------------------------------------------------------------------ */
/* 状态刷新                                                            */
/* ------------------------------------------------------------------ */
void ui_update(HWND hWnd)
{
    App *a = &g_app;
    wchar_t buf[256];

    if (a->doc && a->npages > 0) {
        const wchar_t *tool = (a->tool == ROP_RECT) ? L"矩形填充" :
                              (a->tool == ROP_MOSAIC) ? L"马赛克" : L"自由笔刷";
        wsprintfW(buf, L"%d / %d", a->cur_page + 1, a->npages);
        SetDlgItemTextW(hWnd, IDC_PAGELABEL, buf);
        wsprintfW(buf,
            L"共 %d 页 | 工具: %s | 本页打码 %d 个 | 保存导出为 原名_打码.pdf",
            a->npages, tool, rd_op_count(&a->rd, a->cur_page));
        SetDlgItemTextW(hWnd, IDC_ST_INFO, buf);

        EnableWindow(GetDlgItem(hWnd, IDC_SAVE), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_PREV), a->cur_page > 0);
        EnableWindow(GetDlgItem(hWnd, IDC_NEXT), a->cur_page < a->npages - 1);
        EnableWindow(GetDlgItem(hWnd, IDC_CLEARPAGE), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_CLEARALL), TRUE);
    } else {
        SetDlgItemTextW(hWnd, IDC_PAGELABEL, L"0 / 0");
        SetDlgItemTextW(hWnd, IDC_ST_INFO, L"请先打开 PDF 文件。");
        EnableWindow(GetDlgItem(hWnd, IDC_SAVE), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_PREV), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_NEXT), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_CLEARPAGE), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_CLEARALL), FALSE);
    }

    EnableWindow(GetDlgItem(hWnd, IDC_UNDO), rd_can_undo(&a->rd));
    EnableWindow(GetDlgItem(hWnd, IDC_REDO), rd_can_redo(&a->rd));

    wsprintfW(buf, L"%d pt", (int)(a->brushWidth + 0.5f));
    SetDlgItemTextW(hWnd, IDC_ST_WIDTH, buf);
    wsprintfW(buf, L"%d pt", (int)(a->mosaicBlock + 0.5f));
    SetDlgItemTextW(hWnd, IDC_ST_BLOCK, buf);
    wsprintfW(buf, L"%d %%", zoom_to_pct(a->zoom));
    SetDlgItemTextW(hWnd, IDC_ST_ZOOM, buf);

    SendDlgItemMessageW(hWnd, IDC_TB_WIDTH, TBM_SETPOS, TRUE, (LPARAM)(int)(a->brushWidth + 0.5f));
    SendDlgItemMessageW(hWnd, IDC_TB_BLOCK, TBM_SETPOS, TRUE, (LPARAM)(int)(a->mosaicBlock + 0.5f));
    SendDlgItemMessageW(hWnd, IDC_TB_ZOOM,  TBM_SETPOS, TRUE, (LPARAM)zoom_to_pct(a->zoom));
    SendDlgItemMessageW(hWnd, IDC_FITWIDTH, BM_SETCHECK,
                        a->fitWidth ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* ------------------------------------------------------------------ */
/* 打开 / 保存                                                         */
/* ------------------------------------------------------------------ */
void ui_open_file(HWND hWnd, const wchar_t *path)
{
    App *a = &g_app;
    wchar_t err[256] = L"";

    if (a->doc) pdf_doc_close(a);
    if (a->liveActive) { rd_op_free(&a->live); rd_op_init(&a->live); a->liveActive = 0; }
    rd_free(&a->rd);
    rd_init(&a->rd);
    a->cur_page = 0;
    a->fitWidth = 1;

    if (pdf_doc_open(a, path, err, 256) != 0) {
        MessageBoxW(hWnd, err[0] ? err : L"无法打开 PDF 文件。",
                    L"错误", MB_OK | MB_ICONERROR);
        ui_update(hWnd);
        canvas_refresh(a);
        return;
    }

    canvas_fit_width(a);          /* 置布局/缩放并 relayout */
    a->scrollX = a->scrollY = 0;
    a->cur_page = 0;
    canvas_relayout(a);
    canvas_scroll_to_page(a, 0);
    canvas_sync_scrollbar(a);
    canvas_update_current(a);
    ui_update(hWnd);
    canvas_refresh(a);
}

void ui_do_open(HWND hWnd)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of;

    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = hWnd;
    of.lpstrFilter = L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&of))
        ui_open_file(hWnd, file);
}

static void make_default_out(const wchar_t *src, wchar_t *out, size_t n)
{
    wchar_t base[MAX_PATH];
    wchar_t *e;
    wcsncpy(base, src, MAX_PATH - 1);
    base[MAX_PATH - 1] = 0;
    e = wcsrchr(base, L'.');
    if (e && e != base) *e = 0;
    wcsncpy(out, base, n - 1);
    out[n - 1] = 0;
    wcsncat(out, L"_打码.pdf", n - wcslen(out) - 1);
}

void ui_do_save(HWND hWnd)
{
    App *a = &g_app;
    wchar_t file[MAX_PATH];
    OPENFILENAMEW sf;
    wchar_t err[256] = L"";
    float ez;
    int rc;

    if (!a->doc) {
        MessageBoxW(hWnd, L"请先打开 PDF 文件。", L"提示",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    make_default_out(a->srcPath, file, MAX_PATH);

    memset(&sf, 0, sizeof(sf));
    sf.lStructSize = sizeof(sf);
    sf.hwndOwner = hWnd;
    sf.lpstrFilter = L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0";
    sf.lpstrFile = file;
    sf.nMaxFile = MAX_PATH;
    sf.lpstrDefExt = L"pdf";
    sf.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    sf.nFilterIndex = 1;
    if (!GetSaveFileNameW(&sf))
        return;

    ez = (float)a->exportDpi / 72.0f;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    rc = pdf_doc_export(a, file, ez, a->exportJpeg, err, 256);
    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (rc != 0) {
        MessageBoxW(hWnd, err[0] ? err : L"导出失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    {
        wchar_t msg[MAX_PATH + 32];
        wsprintfW(msg, L"导出成功：\n%s", file);
        MessageBoxW(hWnd, msg, L"提示", MB_OK | MB_ICONINFORMATION);
    }
}

/* ------------------------------------------------------------------ */
/* 翻页 / 缩放                                                         */
/* ------------------------------------------------------------------ */
void ui_goto_page(App *a, int page)
{
    if (!a->doc || a->npages <= 0) return;
    if (page < 0) page = 0;
    if (page >= a->npages) page = a->npages - 1;

    canvas_scroll_to_page(a, page);   /* 内部会更新 cur_page 与滚动条 */
    ui_update(a->hWnd);
    canvas_refresh(a);
}

void ui_zoom_step(App *a, int dir)
{
    float z = a->zoom * (dir > 0 ? 1.25f : 0.8f);
    canvas_zoom_set(a, z);
}

/* ------------------------------------------------------------------ */
/* 命令处理                                                            */
/* ------------------------------------------------------------------ */
static void set_tool(HWND hWnd, int tool)
{
    g_app.tool = tool;
    SendDlgItemMessageW(hWnd, IDC_TOOL_STROKE, BM_SETCHECK,
                        tool == ROP_STROKE ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hWnd, IDC_TOOL_RECT, BM_SETCHECK,
                        tool == ROP_RECT ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hWnd, IDC_TOOL_MOSAIC, BM_SETCHECK,
                        tool == ROP_MOSAIC ? BST_CHECKED : BST_UNCHECKED, 0);
    ui_update(hWnd);
}

static void choose_color(HWND hWnd)
{
    CHOOSECOLORW cc;
    static COLORREF custom[16];
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hWnd;
    cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    cc.rgbResult = app_colorref(&g_app);
    if (ChooseColorW(&cc)) {
        g_app.color[0] = GetRValue(cc.rgbResult) / 255.0f;
        g_app.color[1] = GetGValue(cc.rgbResult) / 255.0f;
        g_app.color[2] = GetBValue(cc.rgbResult) / 255.0f;
        /* 只重绘颜色色块子控件 */
        InvalidateRect(GetDlgItem(hWnd, IDC_ST_SWATCH), NULL, TRUE);
    }
}

LRESULT CALLBACK ui_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App *a = &g_app;

    switch (msg) {
    case WM_CREATE:
        ui_create(hWnd);
        ui_update(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OPEN:      ui_do_open(hWnd); return 0;
        case IDC_SAVE:      ui_do_save(hWnd); return 0;
        case IDC_PREV:      ui_goto_page(a, a->cur_page - 1); return 0;
        case IDC_NEXT:      ui_goto_page(a, a->cur_page + 1); return 0;
        case IDC_UNDO:
            rd_undo(&a->rd);
            ui_update(hWnd);
            canvas_refresh(a);
            return 0;
        case IDC_REDO:
            rd_redo(&a->rd);
            ui_update(hWnd);
            canvas_refresh(a);
            return 0;
        case IDC_CLEARPAGE:
            rd_clear_page(&a->rd, a->cur_page);
            ui_update(hWnd);
            canvas_refresh(a);
            return 0;
        case IDC_CLEARALL:
            if (MessageBoxW(hWnd, L"确定清除全部打码操作？", L"确认",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                rd_clear_all(&a->rd);
                ui_update(hWnd);
                canvas_refresh(a);
            }
            return 0;
        case IDC_TOOL_STROKE: set_tool(hWnd, ROP_STROKE); return 0;
        case IDC_TOOL_RECT:   set_tool(hWnd, ROP_RECT);   return 0;
        case IDC_TOOL_MOSAIC: set_tool(hWnd, ROP_MOSAIC); return 0;
        case IDC_COLOR:       choose_color(hWnd);         return 0;
        case IDC_ZOOM_IN:     ui_zoom_step(a, 1);         return 0;
        case IDC_ZOOM_OUT:    ui_zoom_step(a, -1);        return 0;
        case IDC_FITWIDTH:
            a->fitWidth = (int)SendDlgItemMessageW(hWnd, IDC_FITWIDTH,
                                                   BM_GETCHECK, 0, 0);
            if (a->fitWidth) {
                canvas_fit_width(a);
                a->gen++;
                if (a->pool) { pool_set_gen(a->pool, a->gen); pool_clear(a->pool); }
                canvas_scroll_to_page(a, a->cur_page);
            }
            ui_update(hWnd);
            canvas_refresh(a);
            return 0;
        case IDC_CB_JPEG:
            a->exportJpeg = (int)SendDlgItemMessageW(hWnd, IDC_CB_JPEG,
                                                     BM_GETCHECK, 0, 0);
            return 0;
        case IDC_CB_EXPORTDPI:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(hWnd, IDC_CB_EXPORTDPI,
                                                   CB_GETCURSEL, 0, 0);
                a->exportDpi = (sel == 1) ? 200 : (sel == 2) ? 150 : 300;
            }
            return 0;
        case IDCANCEL:
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_HSCROLL: {
        HWND tb = (HWND)lp;
        int id = GetDlgCtrlID(tb);
        int pos = (int)SendMessageW(tb, TBM_GETPOS, 0, 0);
        switch (id) {
        case IDC_TB_WIDTH:
            a->brushWidth = (float)pos;
            ui_update(hWnd);
            return 0;
        case IDC_TB_BLOCK:
            a->mosaicBlock = (float)pos;
            ui_update(hWnd);
            return 0;
        case IDC_TB_ZOOM:
            canvas_zoom_set(a, pct_to_zoom(pos));
            return 0;
        default:
            return 0;
        }
    }

    case WM_VSCROLL: {
        HWND sb = (HWND)lp;
        if (sb && GetDlgCtrlID(sb) == IDC_VSCROLL) {
            canvas_vscroll(a, LOWORD(wp), (int)(short)HIWORD(wp));
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        POINT pt;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(hWnd, &pt);
        if (!canvas_hit(a, pt.x, pt.y)) return 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            ui_zoom_step(a, delta > 0 ? 1 : -1);
        } else {
            float lines = (float)delta / 120.0f;
            canvas_scroll_delta(a, -lines * (float)S(60));
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        canvas_mouse_down(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        canvas_mouse_up(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE:
        canvas_mouse_move(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MBUTTONDOWN:
        if (a->doc && canvas_hit(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
            a->panning = 1;
            a->panStart.x = GET_X_LPARAM(lp);
            a->panStart.y = GET_Y_LPARAM(lp);
            a->panScrollX0 = a->scrollX;
            a->panScrollY0 = a->scrollY;
            SetCapture(hWnd);
        }
        return 0;

    case WM_MBUTTONUP:
        canvas_mouse_up(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_CAPTURECHANGED:
        canvas_mouse_capture_lost(a);
        return 0;

    case WM_APP_TILE:
        if (a->pool) pool_drain(a->pool);
        canvas_refresh(a);
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
        if (dis->CtlID == IDC_ST_SWATCH) {
            HBRUSH b = CreateSolidBrush(app_colorref(a));
            FillRect(dis->hDC, &dis->rcItem, b);
            DeleteObject(b);
            FrameRect(dis->hDC, &dis->rcItem,
                      (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        HBRUSH br;
        GetClientRect(hWnd, &rc);
        br = CreateSolidBrush(RGB(240, 240, 240));
        FillRect((HDC)wp, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        canvas_paint(a, dc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (a->doc && a->npages > 0) {
            if (a->fitWidth) {
                canvas_fit_width(a);
                a->gen++;
                if (a->pool) { pool_set_gen(a->pool, a->gen); pool_clear(a->pool); }
                canvas_scroll_to_page(a, a->cur_page);
            } else {
                a->layoutValid = 0;
                canvas_relayout(a);
            }
        }
        ui_layout_scrollbar(hWnd);
        canvas_sync_scrollbar(a);
        canvas_update_current(a);
        /* 强制整窗(含子控件)重绘, 避免最大化后工具栏出现黑块 */
        RedrawWindow(hWnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;

    case WM_DESTROY:
        canvas_shutdown();
        if (a->doc) pdf_doc_close(a);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}
