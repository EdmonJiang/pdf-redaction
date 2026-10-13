/*
 * tests/test_export.c - 无界面自测: 打开 PDF -> 施加一个矩形打码 -> 展平导出
 *                        -> 重新打开导出文件渲染 PNG 供人工核对方向/遮盖。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "mupdf/fitz.h"

#include "app.h"
#include "pdf_doc.h"

App g_app;
int g_dpi = 96;

static void render_png(const wchar_t *path, const char *out)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_document *doc = NULL;
    fz_page *page = NULL;
    fz_pixmap *pix = NULL;
    fz_buffer *buf = NULL;
    char p8[MAX_PATH * 3];

    if (!ctx) return;
    fz_register_document_handlers(ctx);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, p8, sizeof(p8), NULL, NULL);
    fz_try(ctx) {
        doc = fz_open_document(ctx, p8);
        page = fz_load_page(ctx, doc, 0);
        pix = fz_new_pixmap_from_page(ctx, page, fz_scale(2, 2),
                                      fz_device_rgb(ctx), 0);
        buf = fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params);
        fz_save_buffer(ctx, buf, out);
    }
    fz_catch(ctx) {
        printf("render_png error: %s\n", fz_caught_message(ctx));
    }
    if (buf) fz_drop_buffer(ctx, buf);
    if (pix) fz_drop_pixmap(ctx, pix);
    if (page) fz_drop_page(ctx, page);
    if (doc) fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
}

static void check_pixel(const wchar_t *path)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_document *doc = NULL;
    fz_page *page = NULL;
    fz_pixmap *pix = NULL;
    char p8[MAX_PATH * 3];
    if (!ctx) return;
    fz_register_document_handlers(ctx);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, p8, sizeof(p8), NULL, NULL);
    fz_try(ctx) {
        int pts[5][2] = { {100, 320}, {100, 50}, {300, 30}, {300, 300}, {300, 550} };
        int i;
        const unsigned char *s;
        doc = fz_open_document(ctx, p8);
        page = fz_load_page(ctx, doc, 0);
        pix = fz_new_pixmap_from_page(ctx, page, fz_scale(72.0f / 72.0f, 72.0f / 72.0f),
                                      fz_device_rgb(ctx), 0);
        s = fz_pixmap_samples(ctx, pix);
        for (i = 0; i < 5; i++) {
            int x = pts[i][0], y = pts[i][1];
            printf("pixel(%d,%d) = %d,%d,%d\n", x, y,
                   s[(y * pix->stride) + x * pix->n + 0],
                   s[(y * pix->stride) + x * pix->n + 1],
                   s[(y * pix->stride) + x * pix->n + 2]);
        }
    }
    fz_catch(ctx) {
        printf("check_pixel error: %s\n", fz_caught_message(ctx));
    }
    if (pix) fz_drop_pixmap(ctx, pix);
    if (page) fz_drop_page(ctx, page);
    if (doc) fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t err[256] = L"";
    RedactOp op;
    FILE *log;

    log = fopen("test_log.txt", "w");
    if (log) { fprintf(log, "start argc=%d\n", argc); fflush(log); }

    if (argc < 3) {
        if (log) { fprintf(log, "usage\n"); fclose(log); }
        return 2;
    }

    pdf_doc_global_init("mupdf_fonts");
    g_app.hWnd = NULL;

    if (pdf_doc_open(&g_app, argv[1], err, 256) != 0) {
        if (log) { fprintf(log, "open failed\n"); fflush(log); fclose(log); }
        return 1;
    }
    if (log) {
        fprintf(log, "opened pages=%d p0=[%.1f,%.1f,%.1f,%.1f]\n",
                g_app.npages, g_app.bounds[0].x0, g_app.bounds[0].y0,
                g_app.bounds[0].x1, g_app.bounds[0].y1);
        fflush(log);
    }

    rd_op_init(&op);
    op.kind = ROP_RECT;
    op.page = 0;
    op.color[0] = 1.0f; op.color[1] = 0.0f; op.color[2] = 0.0f;
    rd_op_set_rect(&op, 50, 250, 250, 400);
    rd_push(&g_app.rd, &op);
    rd_op_free(&op);

    if (pdf_doc_export(&g_app, argv[2], 150.0f / 72.0f,
                        argc > 3 && argv[3][0] == L'j', err, 256) != 0) {
        char ebuf[512] = "";
        WideCharToMultiByte(CP_UTF8, 0, err, -1, ebuf, sizeof(ebuf), NULL, NULL);
        if (log) { fprintf(log, "export failed: %s\n", ebuf); fflush(log); fclose(log); }
        pdf_doc_close(&g_app);
        return 1;
    }
    if (log) { fprintf(log, "export ok\n"); fflush(log); }

    render_png(argv[1], "test_in.png");
    if (log) { fprintf(log, "rendered in\n"); fflush(log); }
    render_png(argv[2], "test_out.png");
    if (log) { fprintf(log, "rendered out\n"); fflush(log); }
    check_pixel(argv[2]);
    if (log) { fprintf(log, "checked\n"); fflush(log); fclose(log); }

    pdf_doc_close(&g_app);
    pdf_doc_global_shutdown();
    return 0;
}
