/*
 * tests/test_big.c - 生成 500 页矢量 PDF, 仅改 1 页后导出, 观察内存占用。
 */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
#include "app.h"
#include "pdf_doc.h"

App g_app;
int g_dpi = 96;

static void gen_big(const char *path, int npages)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_register_document_handlers(ctx);
    fz_try(ctx) {
        pdf_document *doc = pdf_create_document(ctx);
        int i;
        for (i = 0; i < npages; i++) {
            fz_rect mb = fz_make_rect(0, 0, 595, 842);
            pdf_obj *res = NULL;
            fz_buffer *cont = NULL;
            fz_device *dev;
            fz_path *path;
            float col[3] = { 0.9f, 0.9f, 0.95f };

            dev = pdf_page_write(ctx, doc, mb, &res, &cont);
            path = fz_new_path(ctx);
            fz_moveto(ctx, path, 50, 50);
            fz_lineto(ctx, path, 545, 50);
            fz_lineto(ctx, path, 545, 792);
            fz_lineto(ctx, path, 50, 792);
            fz_closepath(ctx, path);
            fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx),
                         col, 1.0f, fz_default_color_params);
            fz_drop_path(ctx, path);
            fz_close_device(ctx, dev);
            fz_drop_device(ctx, dev);

            {
                pdf_obj *po = pdf_add_page(ctx, doc, mb, 0, res, cont);
                pdf_drop_obj(ctx, res);
                fz_drop_buffer(ctx, cont);
                pdf_insert_page(ctx, doc, -1, po);
                pdf_drop_obj(ctx, po);
            }
        }
        pdf_save_document(ctx, doc, path, NULL);
        pdf_drop_document(ctx, doc);
        printf("generated %s (%d pages)\n", path, npages);
    }
    fz_catch(ctx) {
        printf("gen error: %s\n", fz_caught_message(ctx));
    }
    fz_drop_context(ctx);
}

static unsigned long mem_kb(int peak)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return (unsigned long)((peak ? pmc.PeakWorkingSetSize
                                 : pmc.WorkingSetSize) / 1024);
}

int wmain(void)
{
    wchar_t err[256] = L"";
    RedactOp op;
    int npages = 500;

    gen_big("big.pdf", npages);

    pdf_doc_global_init(NULL);
    g_app.hWnd = NULL;

    if (pdf_doc_open(&g_app, L"big.pdf", err, 256) != 0) {
        printf("open failed\n");
        return 1;
    }
    printf("opened %d pages; mem=%lu KB peak=%lu KB (before export)\n",
           g_app.npages, mem_kb(0), mem_kb(1));

    rd_op_init(&op);
    op.kind = ROP_RECT;
    op.page = 0;
    op.color[0] = 1.0f;
    rd_op_set_rect(&op, 100, 100, 400, 400);
    rd_push(&g_app.rd, &op);
    rd_op_free(&op);

    if (pdf_doc_export(&g_app, L"big_out.pdf", 150.0f / 72.0f, 0, err, 256) != 0) {
        printf("export failed\n");
        pdf_doc_close(&g_app);
        return 1;
    }
    printf("export ok; mem=%lu KB peak=%lu KB\n", mem_kb(0), mem_kb(1));

    pdf_doc_close(&g_app);
    return 0;
}
