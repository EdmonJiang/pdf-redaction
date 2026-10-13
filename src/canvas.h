/*
 * canvas.h - 连续纵向滚动预览 / 坐标换算 / 鼠标交互
 */
#ifndef PDFRED_CANVAS_H
#define PDFRED_CANVAS_H

#include <windows.h>
#include "app.h"

/* 预览区 (客户区坐标, 已排除右侧滚动条) */
RECT canvas_rect(void);
int  canvas_hit(const App *a, int x, int y);

void canvas_paint(App *a, HDC dc);
void canvas_shutdown(void);

void canvas_mouse_down(App *a, int x, int y);
void canvas_mouse_move(App *a, int x, int y);
void canvas_mouse_up(App *a, int x, int y);
void canvas_mouse_capture_lost(App *a);

/* 布局 / 滚动 */
void canvas_relayout(App *a);              /* 重算每页位置并夹紧滚动量 */
void canvas_sync_scrollbar(App *a);        /* 同步右侧滚动条 */
void canvas_vscroll(App *a, int code, int pos);
void canvas_scroll_delta(App *a, float dy);
void canvas_scroll_to_page(App *a, int page);
void canvas_update_current(App *a);        /* 按中线判定当前页 */

void canvas_fit_width(App *a);
void canvas_zoom_set(App *a, float zoom);
void canvas_refresh(App *a);

#endif /* PDFRED_CANVAS_H */
