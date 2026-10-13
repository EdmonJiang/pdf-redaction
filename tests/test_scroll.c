/*
 * tests/test_scroll.c - 连续滚动 / 页码判定 / 最大化工具栏重绘 验证
 * 需先运行:  pdfredaction.exe test.pdf
 */
#include <windows.h>
#include <stdio.h>
#include <commctrl.h>

static void label(HWND hwnd, wchar_t *buf, int n)
{
    GetDlgItemTextW(hwnd, 1005, buf, n);   /* IDC_PAGELABEL */
}

static int toolbar_dark(HWND hwnd)
{
    RECT rc;
    int w, h, dpi, toolbar, x, y;
    long dark = 0;
    HDC wdc = GetDC(NULL);
    HDC mdc;
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP bmp, obm;
    unsigned char *p;

    dpi = GetDeviceCaps(wdc, LOGPIXELSX);
    ReleaseDC(NULL, wdc);
    GetClientRect(hwnd, &rc);
    w = rc.right; h = rc.bottom;
    toolbar = MulDiv(150, dpi, 96);

    mdc = CreateCompatibleDC(NULL);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    obm = (HBITMAP)SelectObject(mdc, bmp);
    PrintWindow(hwnd, mdc, 0x00000002);
    p = (unsigned char *)bits;
    for (y = 0; y < toolbar && y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *px = p + ((size_t)y * w + x) * 4;
            int lum = (px[2] * 30 + px[1] * 59 + px[0] * 11) / 100;
            if (lum < 100) dark++;
        }
    SelectObject(mdc, obm);
    DeleteObject(bmp);
    DeleteDC(mdc);
    return (int)dark;
}

int wmain(void)
{
    HWND hwnd = FindWindowW(L"PdfRedactionApp", NULL);
    HWND sb, lbl;
    SCROLLINFO si;
    wchar_t buf[64];
    int pos[] = { 0, 1000, 2100, 2200, 4700, 4800, 7400, 7500, 9800 };
    int i;
    int dpi = GetDeviceCaps(GetDC(NULL), LOGPIXELSX);
    RECT rc;

    if (!hwnd) { printf("window not found\n"); return 1; }
    GetClientRect(hwnd, &rc);
    printf("dpi=%d client=%ldx%ld\n", dpi, rc.right, rc.bottom);
    sb  = GetDlgItem(hwnd, 1027);
    lbl = GetDlgItem(hwnd, 1005);
    if (!sb || !lbl) { printf("controls not found sb=%p lbl=%p\n", (void*)sb, (void*)lbl); return 1; }

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    GetScrollInfo(sb, SB_CTL, &si);
    printf("scroll range: min=%d max=%d page=%u pos=%d\n",
           si.nMin, si.nMax, si.nPage, si.nPos);

    for (i = 0; i < (int)(sizeof(pos)/sizeof(pos[0])); i++) {
        SendMessageW(hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, pos[i]), (LPARAM)sb);
        Sleep(120);
        label(hwnd, buf, 64);
        GetScrollInfo(sb, SB_CTL, &si);
        printf("request pos=%5d -> scroll pos=%5d  page label=%ls\n", pos[i], si.nPos, buf);
    }

    printf("toolbar dark pixels (maximized): ");
    ShowWindow(hwnd, SW_MAXIMIZE);
    Sleep(1000);
    printf("%d\n", toolbar_dark(hwnd));

    {
        HWND tb = GetDlgItem(hwnd, 1018);   /* IDC_TB_ZOOM */
        wchar_t zbuf[32];
        SendMessageW(tb, TBM_SETPOS, TRUE, 500);
        SendMessageW(hwnd, WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, 0), (LPARAM)tb);
        Sleep(300);
        GetDlgItemTextW(hwnd, 1019, zbuf, 32);   /* IDC_ST_ZOOM */
        printf("zoom slider -> 500%%: label=%ls\n", zbuf);
    }

    return 0;
}
