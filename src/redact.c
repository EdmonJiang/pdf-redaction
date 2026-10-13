/*
 * redact.c - 打码操作模型、撤销/重做、像素级合成
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "redact.h"

#define RMIN(a,b) ((a) < (b) ? (a) : (b))
#define RMAX(a,b) ((a) > (b) ? (a) : (b))

/* ------------------------------------------------------------------ */
/* 操作表 / 撤销重做                                                   */
/* ------------------------------------------------------------------ */

void rd_op_init(RedactOp *op)
{
    if (op) memset(op, 0, sizeof(*op));
}

void rd_op_free(RedactOp *op)
{
    if (!op) return;
    free(op->pts);
    op->pts = NULL;
    op->npts = 0;
}

static RedactOp op_clone(const RedactOp *s)
{
    RedactOp d = *s;
    d.pts = NULL;
    if (s->npts > 0 && s->pts) {
        size_t bytes = sizeof(float) * 2 * (size_t)s->npts;
        d.pts = (float *)malloc(bytes);
        if (d.pts) memcpy(d.pts, s->pts, bytes);
        else d.npts = 0;
    } else {
        d.npts = 0;
    }
    return d;
}

void rd_init(RedactDoc *d)
{
    memset(d, 0, sizeof(*d));
}

void rd_free(RedactDoc *d)
{
    int i;
    if (!d) return;
    for (i = 0; i < d->n; i++) rd_op_free(&d->ops[i]);
    free(d->ops);
    memset(d, 0, sizeof(*d));
}

void rd_push(RedactDoc *d, const RedactOp *op)
{
    int i;
    if (!d || !op) return;

    /* 丢弃 redo 分支 */
    if (d->applied < d->n) {
        for (i = d->applied; i < d->n; i++) rd_op_free(&d->ops[i]);
        d->n = d->applied;
    }
    if (d->n == d->cap) {
        int nc = d->cap ? d->cap * 2 : 64;
        RedactOp *no = (RedactOp *)realloc(d->ops, sizeof(RedactOp) * (size_t)nc);
        if (!no) return;
        d->ops = no;
        d->cap = nc;
    }
    d->ops[d->n++] = op_clone(op);
    d->applied = d->n;
}

void rd_undo(RedactDoc *d)
{
    if (d && d->applied > 0) d->applied--;
}

void rd_redo(RedactDoc *d)
{
    if (d && d->applied < d->n) d->applied++;
}

int rd_can_undo(const RedactDoc *d)
{
    return d && d->applied > 0;
}

int rd_can_redo(const RedactDoc *d)
{
    return d && d->applied < d->n;
}

int rd_op_count(const RedactDoc *d, int page)
{
    int i, n = 0;
    if (!d) return 0;
    for (i = 0; i < d->applied; i++)
        if (d->ops[i].page == page) n++;
    return n;
}

void rd_clear_page(RedactDoc *d, int page)
{
    int i, w = 0;
    if (!d) return;
    for (i = 0; i < d->n; i++) {
        if (d->ops[i].page == page) {
            rd_op_free(&d->ops[i]);
        } else {
            if (w != i) d->ops[w] = d->ops[i];
            w++;
        }
    }
    d->n = w;
    d->applied = w;
}

void rd_clear_all(RedactDoc *d)
{
    int i;
    if (!d) return;
    for (i = 0; i < d->n; i++) rd_op_free(&d->ops[i]);
    d->n = 0;
    d->applied = 0;
}

/* ------------------------------------------------------------------ */
/* live op 构造                                                        */
/* ------------------------------------------------------------------ */

void rd_op_add_point(RedactOp *op, float x, float y)
{
    float *np;
    if (!op) return;
    np = (float *)realloc(op->pts, sizeof(float) * 2 * (size_t)(op->npts + 1));
    if (!np) return;
    op->pts = np;
    op->pts[op->npts * 2 + 0] = x;
    op->pts[op->npts * 2 + 1] = y;
    op->npts++;
}

void rd_op_set_rect(RedactOp *op, float x0, float y0, float x1, float y1)
{
    float *np;
    if (!op) return;
    np = (float *)realloc(op->pts, sizeof(float) * 4);
    if (!np) return;
    op->pts = np;
    op->pts[0] = x0; op->pts[1] = y0;
    op->pts[2] = x1; op->pts[3] = y1;
    op->npts = 2;
}

/* ------------------------------------------------------------------ */
/* 像素合成                                                            */
/* ------------------------------------------------------------------ */

static uint32_t color_px(const float c[3])
{
    uint32_t r = (uint32_t)(c[0] * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(c[1] * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(c[2] * 255.0f + 0.5f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void stamp_disc(uint32_t *px, int w, int h, float cx, float cy,
                       float r, uint32_t c)
{
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r);
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    float r2 = r * r;
    int x, y;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;

    for (y = y0; y <= y1; y++) {
        uint32_t *row = px + (size_t)y * (size_t)w;
        float dy = (float)y + 0.5f - cy;
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx;
            if (dx * dx + dy * dy <= r2) row[x] = c;
        }
    }
}

static void thick_line(uint32_t *px, int w, int h,
                       float x0, float y0, float x1, float y1,
                       float r, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int steps, i;

    if (r < 0.5f) r = 0.5f;
    steps = (int)(len / (r * 0.5f));
    if (steps < 1) steps = 1;
    if (steps > 8192) steps = 8192;
    for (i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        stamp_disc(px, w, h, x0 + dx * t, y0 + dy * t, r, c);
    }
}

static void fill_rect(uint32_t *px, int w, int h,
                      int x0, int y0, int x1, int y1, uint32_t c)
{
    int x, y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    for (y = y0; y <= y1; y++) {
        uint32_t *row = px + (size_t)y * (size_t)w;
        for (x = x0; x <= x1; x++) row[x] = c;
    }
}

static void mosaic_rect(uint32_t *px, int w, int h,
                        int x0, int y0, int x1, int y1, int block)
{
    int bx, by, t;
    if (block < 1) block = 1;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;

    for (by = y0; by <= y1; by += block) {
        int ey = RMIN(by + block - 1, y1);
        for (bx = x0; bx <= x1; bx += block) {
            int ex = RMIN(bx + block - 1, x1);
            unsigned r = 0, g = 0, b = 0, n = 0;
            uint32_t avg;
            int x, y;
            for (y = by; y <= ey; y++) {
                uint32_t *row = px + (size_t)y * (size_t)w;
                for (x = bx; x <= ex; x++) {
                    uint32_t c = row[x];
                    r += (c >> 16) & 255;
                    g += (c >> 8) & 255;
                    b += c & 255;
                    n++;
                }
            }
            if (!n) continue;
            avg = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
            for (y = by; y <= ey; y++) {
                uint32_t *row = px + (size_t)y * (size_t)w;
                for (x = bx; x <= ex; x++) row[x] = avg;
            }
        }
    }
}

void rd_apply_op_bits(const RedactOp *op, float zoom,
                      float bx0, float by0, float offx, float offy,
                      uint32_t *px, int w, int h)
{
    uint32_t c;

    if (!op || !px || w <= 0 || h <= 0 || op->npts < 1) return;

    switch (op->kind) {
    case ROP_RECT:
        if (op->npts >= 2) {
            int x0 = (int)floorf((op->pts[0] - bx0) * zoom + offx);
            int y0 = (int)floorf((op->pts[1] - by0) * zoom + offy);
            int x1 = (int)ceilf((op->pts[2] - bx0) * zoom + offx);
            int y1 = (int)ceilf((op->pts[3] - by0) * zoom + offy);
            fill_rect(px, w, h, x0, y0, x1, y1, color_px(op->color));
        }
        break;

    case ROP_MOSAIC:
        if (op->npts >= 2) {
            int x0 = (int)floorf((op->pts[0] - bx0) * zoom + offx);
            int y0 = (int)floorf((op->pts[1] - by0) * zoom + offy);
            int x1 = (int)ceilf((op->pts[2] - bx0) * zoom + offx);
            int y1 = (int)ceilf((op->pts[3] - by0) * zoom + offy);
            int block = (int)(op->block * zoom + 0.5f);
            mosaic_rect(px, w, h, x0, y0, x1, y1, block);
        }
        break;

    case ROP_STROKE:
    default: {
        float r = op->width * zoom * 0.5f;
        int i;
        if (r < 0.5f) r = 0.5f;
        c = color_px(op->color);
        if (op->npts == 1) {
            stamp_disc(px, w, h,
                       (op->pts[0] - bx0) * zoom + offx,
                       (op->pts[1] - by0) * zoom + offy, r, c);
        } else {
            for (i = 0; i + 1 < op->npts; i++) {
                float ax = (op->pts[i * 2 + 0] - bx0) * zoom + offx;
                float ay = (op->pts[i * 2 + 1] - by0) * zoom + offy;
                float bx = (op->pts[i * 2 + 2] - bx0) * zoom + offx;
                float by = (op->pts[i * 2 + 3] - by0) * zoom + offy;
                thick_line(px, w, h, ax, ay, bx, by, r, c);
            }
        }
        break;
    }
    }
}

void rd_apply_bits(const RedactDoc *d, int page, float zoom,
                   float bx0, float by0, float offx, float offy,
                   uint32_t *px, int w, int h)
{
    int i;
    if (!d || !px) return;
    for (i = 0; i < d->applied; i++) {
        if (d->ops[i].page == page)
            rd_apply_op_bits(&d->ops[i], zoom, bx0, by0, offx, offy, px, w, h);
    }
}
