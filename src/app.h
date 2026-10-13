/*
 * app.h - 应用全局状态
 */
#ifndef PDFRED_APP_H
#define PDFRED_APP_H

#include <windows.h>

#include "mupdf/fitz.h"

#include "redact.h"
#include "render_pool.h"

/* 页面几何 (页面坐标, 单位 point, 已应用 /Rotate 与 CropBox) */
typedef struct {
    float x0, y0, x1, y1;
} PageRect;

typedef struct App {
    HINSTANCE hInst;
    HWND      hWnd;

    /* MuPDF (主线程) */
    fz_context    *ctx;
    unsigned char *data;
    size_t         len;
    fz_document   *doc;
    int            npages;
    PageRect      *bounds;
    float         *pageX, *pageY;   /* 每页在内容坐标中的左上角 */
    wchar_t        srcPath[MAX_PATH];

    /* 编辑 */
    RedactDoc rd;
    int   cur_page;
    int   tool;             /* RedactKind */
    float color[3];         /* 0..1 RGB */
    float brushWidth;       /* point */
    float mosaicBlock;      /* point */

    /* 视图 (连续纵向滚动) */
    float zoom;             /* 物理像素 / point */
    float scrollX, scrollY; /* 内容坐标中视口左上角 */
    float contentW, contentH;
    int   layoutValid;
    int   fitWidth;

    /* 交互 */
    int   dragging;
    int   panning;
    POINT panStart;
    float panScrollX0, panScrollY0;
    RedactOp live;
    int   liveActive;

    /* 渲染 */
    RenderPool *pool;
    LONG  gen;

    /* 导出 */
    int   exportDpi;
    int   exportJpeg;
} App;

extern App g_app;
extern int g_dpi;

#define S(v) MulDiv((v), g_dpi, 96)

static __inline COLORREF app_colorref(const App *a)
{
    return RGB((int)(a->color[0] * 255.0f + 0.5f),
               (int)(a->color[1] * 255.0f + 0.5f),
               (int)(a->color[2] * 255.0f + 0.5f));
}

#endif /* PDFRED_APP_H */
