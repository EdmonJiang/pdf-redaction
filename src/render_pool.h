/*
 * render_pool.h - 多线程瓦片渲染线程池 + LRU 缓存
 *
 * 每个工作线程持有独立的 fz_context / fz_document (MuPDF 非线程安全),
 * 共享同一份只读 PDF 内存。UI 线程派发可见瓦片请求, 工作线程渲染为
 * 32bpp DIB, 通过结果队列回传, UI 线程在 WM_APP_TILE 中取出并放入 LRU。
 */
#ifndef PDFRED_RENDER_POOL_H
#define PDFRED_RENDER_POOL_H

#include <windows.h>
#include <stddef.h>

typedef struct RenderPool RenderPool;

#define POOL_TILE    1024
#define WM_APP_TILE  (WM_APP + 1)

RenderPool *pool_create(HWND hWnd, const unsigned char *data, size_t len,
                        int nthreads);
void pool_destroy(RenderPool *p);

/* 进入新的一代 (翻页/缩放), 旧请求作废 */
void pool_set_gen(RenderPool *p, LONG gen);

/* 清空瓦片缓存与待处理请求 */
void pool_clear(RenderPool *p);

/* 请求渲染一个瓦片 (图像像素坐标); 已缓存或已在请求中则忽略 */
void pool_request(RenderPool *p, int page, float zoom,
                  int x, int y, int w, int h);

/* 查询已缓存瓦片 (命中时更新 LRU), 返回 HBITMAP 或 NULL */
HBITMAP pool_lookup(RenderPool *p, int page, float zoom, int x, int y,
                    int *ow, int *oh);

/* UI 线程: 取出结果队列, 投放 LRU (过期结果直接释放) */
void pool_drain(RenderPool *p);

#endif /* PDFRED_RENDER_POOL_H */
