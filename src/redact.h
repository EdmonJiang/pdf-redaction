/*
 * redact.h - 打码操作模型 + 撤销/重做 + 位图合成
 *
 * 打码操作永久保存在「页面坐标」(point, 原点 = 页面显示包围盒左上角, y 向下),
 * 因此缩放 / 翻页 / 导出 DPI 变化都不会失真。合成时统一映射到 32bpp 像素缓冲。
 */
#ifndef PDFRED_REDACT_H
#define PDFRED_REDACT_H

#include <stdint.h>

typedef enum {
    ROP_STROKE = 0,   /* 自由笔刷涂抹 */
    ROP_RECT   = 1,   /* 矩形填充 (无边框) */
    ROP_MOSAIC = 2    /* 马赛克 */
} RedactKind;

typedef struct {
    RedactKind kind;
    int    page;              /* 页索引 */
    float  color[3];          /* 0..1 RGB (STROKE/RECT 用) */
    float  width;             /* 笔刷粗细, point (STROKE 用) */
    float  block;             /* 马赛克块大小, point (MOSAIC 用) */
    int    npts;              /* 点数 */
    float *pts;               /* 页面坐标点序列: x,y,x,y,... */
} RedactOp;

/* 撤销/重做：按时间顺序的操作表 + 已应用数量 */
typedef struct {
    RedactOp *ops;
    int       n, cap;
    int       applied;        /* ops[0..applied) 为“已生效” */
} RedactDoc;

void rd_init(RedactDoc *d);
void rd_free(RedactDoc *d);

void rd_push(RedactDoc *d, const RedactOp *op);   /* 追加并立即生效 */
void rd_undo(RedactDoc *d);
void rd_redo(RedactDoc *d);
int  rd_can_undo(const RedactDoc *d);
int  rd_can_redo(const RedactDoc *d);
int  rd_op_count(const RedactDoc *d, int page);
void rd_clear_page(RedactDoc *d, int page);       /* 删除该页全部操作, 不可撤销 */
void rd_clear_all(RedactDoc *d);                  /* 清空全部操作 */

/* 构造中的操作 (live op) */
void rd_op_init(RedactOp *op);
void rd_op_free(RedactOp *op);
void rd_op_add_point(RedactOp *op, float x, float y);
void rd_op_set_rect(RedactOp *op, float x0, float y0, float x1, float y1);

/*
 * 把单个操作合成到 32bpp (BGRA) 像素缓冲。
 *   zoom : 像素 / point
 *   bx0,by0 : 页面显示包围盒原点 (page 坐标)
 *   offx,offy : 页面图像原点在目标缓冲中的偏移 (预览时 = pan - canvas 原点; 导出 = 0)
 *   px   : 缓冲首地址, 行跨度 = w 像素
 */
void rd_apply_op_bits(const RedactOp *op, float zoom,
                      float bx0, float by0, float offx, float offy,
                      uint32_t *px, int w, int h);

/* 依次合成 ops[0..applied) 中属于 page 的操作 */
void rd_apply_bits(const RedactDoc *d, int page, float zoom,
                   float bx0, float by0, float offx, float offy,
                   uint32_t *px, int w, int h);

#endif /* PDFRED_REDACT_H */
