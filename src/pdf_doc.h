/*
 * pdf_doc.h - MuPDF 封装 (打开 / 页信息 / 展平导出)
 */
#ifndef PDFRED_PDF_DOC_H
#define PDFRED_PDF_DOC_H

#include <stddef.h>
#include "app.h"

/* 全局一次性初始化: 设置内置字体加载器与字体目录 (在创建任何 context 前调用) */
int  pdf_doc_global_init(const char *fontDir);
void pdf_doc_global_shutdown(void);

/* 打开 PDF (整份读入内存, 供多线程共享只读) */
int  pdf_doc_open(App *app, const wchar_t *path, wchar_t *err, size_t errMax);
void pdf_doc_close(App *app);

/*
 * 展平导出: 逐页高分辨率渲染 -> 应用打码 -> 整页图像写入新 PDF。
 * 被遮盖处原始内容不会进入输出文件 (真遮盖)。
 * exportZoom = 导出 DPI / 72。
 */
int  pdf_doc_export(App *app, const wchar_t *outPath, float exportZoom,
                    int jpeg, wchar_t *err, size_t errMax);

#endif /* PDFRED_PDF_DOC_H */
