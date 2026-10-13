/*
 * ui.h - 控件布局 / 状态 / 命令
 */
#ifndef PDFRED_UI_H
#define PDFRED_UI_H

#include <windows.h>
#include "app.h"

/* 控件 ID (同时供快捷键表使用) */
#define IDC_OPEN         1001
#define IDC_SAVE         1002
#define IDC_PREV         1003
#define IDC_NEXT         1004
#define IDC_PAGELABEL    1005
#define IDC_UNDO         1006
#define IDC_REDO         1007
#define IDC_CLEARPAGE    1008
#define IDC_CLEARALL     1009
#define IDC_TOOL_STROKE  1010
#define IDC_TOOL_RECT    1011
#define IDC_TOOL_MOSAIC  1012
#define IDC_COLOR        1013
#define IDC_TB_WIDTH     1014
#define IDC_ST_WIDTH     1015
#define IDC_TB_BLOCK     1016
#define IDC_ST_BLOCK     1017
#define IDC_TB_ZOOM      1018
#define IDC_ST_ZOOM      1019
#define IDC_FITWIDTH     1020
#define IDC_ST_INFO      1021
#define IDC_CB_JPEG      1022
#define IDC_CB_EXPORTDPI 1023
#define IDC_ST_SWATCH    1024
#define IDC_ZOOM_IN      1025
#define IDC_ZOOM_OUT     1026
#define IDC_VSCROLL      1027

int  ui_toolbar_height(void);

void ui_update(HWND hWnd);
void ui_open_file(HWND hWnd, const wchar_t *path);
void ui_do_open(HWND hWnd);
void ui_do_save(HWND hWnd);

void ui_goto_page(App *a, int page);
void ui_zoom_step(App *a, int dir);

LRESULT CALLBACK ui_wndproc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

#endif /* PDFRED_UI_H */
