/* tests/make_synth.c - 生成一个用于方向校验的合成 PDF:
 * 页面上方 100pt 为黑色矩形, 其余为白色。 */
#include <windows.h>
#include <stdio.h>
#include "mupdf/fitz.h"

int wmain(void)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_document_writer *wri = NULL;
    fz_document *doc = NULL;
    fz_device *dev = NULL;
    fz_path *path = NULL;
    fz_rect mb = fz_make_rect(0, 0, 400, 600);
    float black[3] = { 0, 0, 0 };
    float white[3] = { 1, 1, 1 };

    fz_register_document_handlers(ctx);
    fz_try(ctx) {
        wri = fz_new_document_writer(ctx, "synth.pdf", "pdf", "compress");
        dev = fz_begin_page(ctx, wri, mb);
        /* 整页白 */
        path = fz_new_path(ctx);
        fz_moveto(ctx, path, 0, 0);
        fz_lineto(ctx, path, 400, 0);
        fz_lineto(ctx, path, 400, 600);
        fz_lineto(ctx, path, 0, 600);
        fz_closepath(ctx, path);
        fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), white, 1.0f,
                     fz_default_color_params);
        fz_drop_path(ctx, path);
        path = NULL;
        /* 顶部黑带 (page y 0..100) */
        path = fz_new_path(ctx);
        fz_moveto(ctx, path, 0, 0);
        fz_lineto(ctx, path, 400, 0);
        fz_lineto(ctx, path, 400, 100);
        fz_lineto(ctx, path, 0, 100);
        fz_closepath(ctx, path);
        fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), black, 1.0f,
                     fz_default_color_params);
        fz_drop_path(ctx, path);
        path = NULL;
        fz_end_page(ctx, wri);
        fz_close_document_writer(ctx, wri);
        fz_drop_document_writer(ctx, wri);
        wri = NULL;
        printf("synth.pdf written\n");
    }
    fz_catch(ctx) { printf("caught: %s\n", fz_caught_message(ctx)); }

    if (path) fz_drop_path(ctx, path);
    if (wri) fz_drop_document_writer(ctx, wri);
    (void)doc;
    fz_drop_context(ctx);
    return 0;
}
