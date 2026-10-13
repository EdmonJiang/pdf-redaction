/*
 * main.c - Win32 入口、框架窗口、消息循环、快捷键
 */
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "app.h"
#include "pdf_doc.h"
#include "ui.h"
#include "version.h"

App g_app;
int g_dpi = 96;

static HACCEL make_accel(void)
{
    ACCEL a[12];
    int n = 0;
#define ADD(f, k, c) do { a[n].fVirt = (f); a[n].key = (k); a[n].cmd = (c); n++; } while (0)
    ADD(FVIRTKEY | FCONTROL, 'O', IDC_OPEN);
    ADD(FVIRTKEY | FCONTROL, 'S', IDC_SAVE);
    ADD(FVIRTKEY | FCONTROL, 'Z', IDC_UNDO);
    ADD(FVIRTKEY | FCONTROL, 'Y', IDC_REDO);
    ADD(FVIRTKEY | FCONTROL | FSHIFT, 'Z', IDC_REDO);
    ADD(FVIRTKEY, VK_PRIOR, IDC_PREV);
    ADD(FVIRTKEY, VK_NEXT, IDC_NEXT);
    ADD(FVIRTKEY, VK_ADD, IDC_ZOOM_IN);
    ADD(FVIRTKEY, VK_SUBTRACT, IDC_ZOOM_OUT);
    ADD(FVIRTKEY | FCONTROL, VK_ADD, IDC_ZOOM_IN);
    ADD(FVIRTKEY | FCONTROL, VK_SUBTRACT, IDC_ZOOM_OUT);
#undef ADD
    return CreateAcceleratorTableW(a, n);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                    LPWSTR cmdLine, int nShow)
{
    WNDCLASSEXW wc;
    MSG msg;
    HACCEL acc;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev;
    g_app.hInst = hInst;

    CoInitialize(NULL);

    /* DPI 感知 (Per-Monitor V2) */
    {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
        SetCtxFn f = u32 ? (SetCtxFn)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
        if (f) {
            f((HANDLE)-4);
        } else {
            typedef BOOL (WINAPI *SetAwareFn)(void);
            SetAwareFn g = u32 ? (SetAwareFn)(void *)GetProcAddress(u32, "SetProcessDPIAware") : NULL;
            if (g) g();
        }
        {
            HDC sdc = GetDC(NULL);
            g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
            ReleaseDC(NULL, sdc);
            if (g_dpi < 96) g_dpi = 96;
        }
    }

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    /* 定位内置字体目录 (exe 同级 mupdf_fonts) 并初始化 MuPDF 字体加载器 */
    {
        wchar_t exePath[MAX_PATH];
        char fontDir[MAX_PATH * 2];
        wchar_t *slash;
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        slash = wcsrchr(exePath, L'\\');
        if (slash) *slash = 0;
        wcscat(exePath, L"\\mupdf_fonts");
        if (WideCharToMultiByte(CP_UTF8, 0, exePath, -1, fontDir,
                                sizeof(fontDir), NULL, NULL) == 0)
            fontDir[0] = 0;
        pdf_doc_global_init(fontDir[0] ? fontDir : NULL);
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ui_wndproc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"PdfRedactionApp";
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    g_app.hWnd = CreateWindowW(L"PdfRedactionApp",
        APP_TITLE L" v" APP_VERSION L" " APP_BUILD_DATE,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, S(1000), S(760),
        NULL, NULL, hInst, NULL);
    if (!g_app.hWnd) {
        pdf_doc_global_shutdown();
        CoUninitialize();
        return 1;
    }
    ShowWindow(g_app.hWnd, nShow);
    UpdateWindow(g_app.hWnd);

    acc = make_accel();

    /* 命令行直接打开 PDF */
    if (cmdLine && *cmdLine) {
        wchar_t path[MAX_PATH];
        if (*cmdLine == L'"') {
            wchar_t *end = wcschr(cmdLine + 1, L'"');
            if (end) {
                wcsncpy(path, cmdLine + 1, (size_t)(end - cmdLine - 1));
                path[end - cmdLine - 1] = 0;
            } else {
                wcsncpy(path, cmdLine + 1, MAX_PATH - 1);
                path[MAX_PATH - 1] = 0;
            }
        } else {
            wcsncpy(path, cmdLine, MAX_PATH - 1);
            path[MAX_PATH - 1] = 0;
        }
        if (path[0]) ui_open_file(g_app.hWnd, path);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!acc || !TranslateAcceleratorW(g_app.hWnd, acc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (acc) DestroyAcceleratorTable(acc);
    pdf_doc_global_shutdown();
    CoUninitialize();
    return (int)msg.wParam;
}
