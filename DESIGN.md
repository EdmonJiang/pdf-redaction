# PDF 打码工具 · 实现方案

> 纯 C + Win32 + MuPDF 的原生 Windows PDF 打码（redaction）工具。
> 界面风格与 `pdf-watermark` 保持一致；MuPDF 负责高分辨率渲染与 PDF 读写。

---

## 0. 已确认的关键决策

| 决策 | 结论 |
| :--- | :--- |
| 导出文件名后缀 | **`_打码`**：`文件.pdf` → `文件_打码.pdf` |
| 遮盖强度 | **真遮盖**：逐页展平为位图导出，被遮处的原始文字/图像**不进入输出文件**，无法提取/恢复 |
| 实现顺序 | 先定稿设计；**暂不写代码**，确认后再按里程碑实现 |

---

## 1. 目标与范围

打开一个 PDF，把每一页**多线程、高分辨率**渲染成位图；用户可以在页面上用
**自由笔刷涂抹**、画**矩形填充**（无边框，填充色即当前颜色）、或套用**马赛克**；
支持**撤销/重做**、**翻页**、缩放；最后导出为新的 PDF，文件名为
`原名_打码.pdf`（如 `文件.pdf` → `文件_打码.pdf`）。

设计目标：

- **真遮盖（已确认）**：导出的 PDF 中被遮区域下方的原始文字/图像**不进入
  输出文件**，无法被复制、提取或恢复；而不是仅用一层矢量方块盖住。
- **高分辨率**：导出可配置（默认 150 DPI，可选 300 DPI），保证打印清晰。
- **不卡顿**：翻页/缩放由后台多线程渲染 + 缓存支撑。
- **风格统一**：控件布局、配色、字体（微软雅黑）、DPI 感知方式与
  `pdf-watermark` 完全一致。

非目标（第一版不做）：文本选择/编辑、OCR、批量多文件、加密 PDF 的写入。

---

## 2. 功能清单

| 分类 | 功能 |
| :--- | :--- |
| 文件 | 打开 PDF、保存为 PDF（`原名_打码.pdf`）、显示总页数/当前页 |
| 浏览 | 上一页 / 下一页（按钮 + PageUp/PageDown + 滚轮）、缩放到适应宽度、滚轮 Ctrl+缩放 |
| 渲染 | 多线程、高分辨率、瓦片化、LRU 缓存、渲染任务可取消/可提优先级 |
| 工具 | ① 自由笔刷涂抹 ② 矩形填充（无边框）③ 马赛克 |
| 参数 | 颜色（取色器）、笔刷粗细（滑块）、马赛克块大小（滑块） |
| 编辑 | 撤销 / 重做（`Ctrl+Z` / `Ctrl+Y`）、清除当前页、清除全部 |
| 导出 | 展平导出（可选导出 DPI）、导出时后台多线程 |

---

## 3. 技术选型

| 组件 | 选择 | 理由 |
| :--- | :--- | :--- |
| 语言 | C（C99/C11） | 与既有项目一致 |
| 界面 | Win32 API + comctl32 | 与 `pdf-watermark` 一致，无第三方 UI 库 |
| 渲染/PDF | **MuPDF**（`libmupdf.a`） | 高保真渲染、支持加密文档、可直接生成 PDF |
| 位图 | 32bpp top-down DIB（`CreateDIBSection`） | 可直接读写像素，便于打码合成 |
| 线程 | Win32 线程 + 信号量/事件 | 无额外依赖 |
| 构建 | TDM-GCC 9.2 / MinGW-w64 | 与既有项目一致 |

复用 `pdf-watermark` 已验证的 MuPDF 集成方式：
`../pdf-watermark/tools/build_mupdf.sh` 产出 `libmupdf.a`，
`noto_sumatra.c` 提供内置字体加载，运行时字体目录为 exe 同级 `mupdf_fonts/`。

---

## 4. 总体架构

```
┌──────────────────────────────────────────────────────────────┐
│ main.c            框架窗口 / 消息循环 / 快捷键                 │
├───────────────┬──────────────────┬───────────────────────────┤
│ ui.c           │ canvas.c         │ redact.c                  │
│ 控件布局/状态  │ 预览绘制/命中/交互 │ 打码操作模型 + 撤销/重做   │
├───────────────┴──────────────────┴───────────────────────────┤
│ render_pool.c   多线程渲染线程池 + 瓦片缓存(LRU)               │
├──────────────────────────────────────────────────────────────┤
│ pdf_doc.c       MuPDF 封装: 打开/页数/页尺寸/渲染/导出          │
└──────────────────────────────────────────────────────────────┘
```

数据流：

```
打开 PDF ──► pdf_doc_open()  ──► 读取文件到内存(共享只读)
                                     │
预览:  canvas 请求(页, 缩放, 可见区域)
        └─► render_pool  派发给工作线程
              └─ 每线程自己的 fz_context + fz_stream/fz_document
                    └─ 渲染瓦片 ──► 32bpp DIB ──► LRU 缓存
        └─► canvas 合成: 页面位图 + 打码操作(redact)  → 屏幕

导出:  redact 提供操作列表
        └─► pdf_doc_export()  逐页: 高分辨率渲染 → 应用打码 → 全页图像 → 新 PDF
```

---

## 5. 目录结构

```
pdf-redaction/
├── README.md
├── DESIGN.md                ← 本文件
├── CHANGELOG.md
├── LICENSE
├── Makefile
├── build.bat
├── app.rc                   (图标 + 版本信息)
├── redaction.ico
├── src/
│   ├── main.c               Win32 入口、框架窗口、消息循环
│   ├── ui.c / ui.h          控件创建/布局、状态、命令处理
│   ├── canvas.c / canvas.h  预览绘制、坐标换算、鼠标/键盘交互
│   ├── redact.c / redact.h  打码操作、撤销/重做、序列化
│   ├── render_pool.c / .h   渲染线程池、任务队列、瓦片 LRU 缓存
│   ├── pdf_doc.c / pdf_doc.h MuPDF 封装（打开/渲染/导出）
│   └── version.h
├── tools/
│   └── build_mupdf.sh       (可从 pdf-watermark 拷贝)
└── docs/
    └── MUPDF_NOTES.md       MuPDF API 速查与线程注意事项
```

---

## 6. 关键数据结构

```c
/* ---- 页面几何（显示坐标，单位: point，已含 /Rotate 与 CropBox） ---- */
typedef struct { float x0, y0, x1, y1; } PageRect;

/* ---- 打码操作 ---- */
typedef enum {
    ROP_STROKE = 0,   /* 自由笔刷涂抹 */
    ROP_RECT   = 1,   /* 矩形填充（无边框） */
    ROP_MOSAIC = 2    /* 马赛克 */
} RedactKind;

typedef struct {
    RedactKind kind;
    int    page;              /* 页索引 */
    float  color[3];          /* 0..1 RGB（STROKE/RECT 用） */
    float  width;             /* 笔刷粗细，point（STROKE 用） */
    float  block;             /* 马赛克块大小，point（MOSAIC 用） */
    int    npts;              /* 点数 */
    float *pts;               /* 页面坐标点序列；RECT 时为 4 个角 */
} RedactOp;

/* ---- 撤销/重做：一条按时间顺序的操作表 + 已应用数量 ---- */
typedef struct {
    RedactOp *ops;
    int       n, cap;
    int       applied;        /* ops[0..applied) 为“已生效” */
} RedactDoc;

/* ---- 渲染请求/结果 ---- */
typedef struct {
    int    page;
    float  zoom;              /* 1.0 = 72dpi；2.083 ≈ 150dpi */
    int    tileX, tileY;      /* 瓦片像素坐标（整页图坐标系） */
    int    tileW, tileH;
    int    seq;               /* 请求序号，用于丢弃过期结果 */
} RenderReq;

typedef struct {
    int      page; float zoom;
    int      x, y, w, h;      /* 在整页图中的位置 */
    HBITMAP  bmp;             /* 32bpp top-down DIB */
    ULONGLONG lastUse;
} Tile;

typedef struct {
    /* MuPDF */
    fz_context  *ctx;         /* 主线程 context */
    uint8_t     *data; size_t len;   /* 共享只读文件缓冲 */
    fz_document *doc;         /* 主线程用（导出/取页尺寸） */
    int          npages;
    PageRect    *bounds;      /* 每页显示尺寸 */

    /* 编辑 */
    RedactDoc    rd;
    int          cur_page;
    int          tool;        /* ROP_STROKE / ROP_RECT / ROP_MOSAIC */
    float        color[3];
    float        brushWidth;  /* point */
    float        mosaicBlock; /* point */

    /* 视图 */
    float        zoom;        /* 屏幕像素 / point */
    float        panX, panY;

    HWND         hWnd;
} App;
```

---

## 7. 坐标系统

三套坐标，转换关系必须明确：

| 坐标系 | 单位 | 说明 |
| :--- | :--- | :--- |
| **页面坐标 (page)** | point | 以 `fz_bound_page()` 的左上角为原点，y 向下。打码操作**永久保存**在这套坐标里。 |
| **整页图像坐标 (image)** | pixel | `image = (page - bound.origin) * zoom * (dpi/72)`；渲染缓存的坐标。 |
| **屏幕坐标 (screen)** | pixel | `screen = image * viewScale + pan`；即预览客户区坐标。 |

- 用 `fz_bound_page(ctx, page)` 得到**显示包围盒**（已应用 `/Rotate`、`CropBox`），
  它与渲染结果方向一致，避免自己处理旋转。
- 打码操作只存 page 坐标 ⇒ 缩放/翻页/DPI 变化都不失真；导出时按导出 zoom
  重新映射即可。
- 反向换算（屏幕 → 页面）用于鼠标落点。

---

## 8. 多线程高分辨率渲染

### 8.1 线程模型

MuPDF 的 `fz_context` 与 `fz_document` **都不是线程安全的**。采用
「**每线程独立 context + 独立 document**」的最稳妥模型：

- 打开文件时把整个 PDF 读入内存 `app->data`（**共享只读**）。
- 工作线程各建一个 `fz_context`（`fz_new_context`），以及基于共享内存的
  独立 `fz_stream`（`fz_open_memory(app->data, app->len)`）与
  `fz_document`（`fz_open_document_with_stream`）。
- 线程之间**不共享任何 MuPDF 对象** ⇒ 无需锁，最安全。

```c
typedef struct {
    HANDLE        thread;
    fz_context   *ctx;
    fz_document  *doc;      /* 该线程私有 */
    RenderPool   *pool;
} Worker;

static DWORD WINAPI worker_proc(LPVOID p)
{
    Worker *w = (Worker *)p;
    w->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_register_document_handlers(w->ctx);
    fz_stream *stm = fz_open_memory(w->ctx, w->pool->data, w->pool->len);
    w->doc = fz_open_document_with_stream(w->ctx, "application/pdf", stm);
    fz_drop_stream(w->ctx, stm);
    for (;;) {
        RenderReq req;
        if (!pool_pop(w->pool, &req)) break;      /* 退出信号 */
        if (pool_is_stale(w->pool, &req)) continue;/* 已被新请求取代 */
        HBITMAP bmp;
        if (render_tile(w, &req, &bmp) == 0)
            pool_publish(w->pool, &req, bmp);      /* 放回结果队列 */
    }
    fz_drop_document(w->ctx, w->doc);
    fz_drop_context(w->ctx);
    return 0;
}
```

> 备选：`fz_clone_context` 共享同一 document + 全局锁。实现更省内存，但要小心
> 锁粒度；第一版用上面的独立 document 方案，简单且不会有隐蔽竞态。

### 8.2 瓦片渲染

高分辨率整页往往很大（A4 @300dpi ≈ 2480×3508×4 ≈ 34 MB）。按 **瓦片** 渲染，
既限制单次内存，又能只渲染可见区域：

- 瓦片大小 `TILE = 1024×1024`（图像像素）。
- 渲染瓦片 `(tileX,tileY,tileW,tileH)`：

```c
static int render_tile(Worker *w, const RenderReq *q, HBITMAP *out)
{
    fz_context *ctx = w->ctx;
    fz_page *page = NULL; fz_pixmap *pix = NULL; HBITMAP bmp = NULL;
    int ok = -1;
    fz_try(ctx) {
        page = fz_load_page(ctx, w->doc, q->page);
        /* 把整页图像坐标系平移到瓦片左上角 */
        fz_matrix ctm = fz_scale(q->zoom, q->zoom);
        ctm = fz_pre_translate(ctm, -(float)q->tileX / q->zoom,
                                    -(float)q->tileY / q->zoom);
        pix = fz_new_pixmap(ctx, fz_device_rgb(ctx), q->tileW, q->tileH, NULL, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);
        fz_device *dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_page(ctx, page, dev, ctm, NULL);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        bmp = pixmap_to_dib(ctx, pix, q->tileW, q->tileH);  /* 见 8.4 */
        ok = 0;
    }
    fz_catch(ctx) { ok = -1; }
    fz_drop_pixmap(ctx, pix);
    fz_drop_page(ctx, page);
    if (ok == 0) *out = bmp;
    return ok;
}
```

要点：
- `fz_new_draw_device` 的 pixmap 自带裁剪，超出瓦片的绘制自动丢弃。
- `fz_set_aa_level(ctx, 8)` / `fz_set_graphics_aa_level` 提升线条质量。
- 渲染前可 `fz_clear_pixmap_with_value(ctx, pix, 255)` 填白底。

### 8.3 任务队列与调度

- 单生产者（UI）+ 多消费者（工作线程），互斥量 + 信号量。
- 请求带 `seq`；同一 (page,zoom,tile) 只保留最新请求，旧请求直接丢弃（
  `pool_is_stale`）。这样快速翻页/缩放时不会渲染一堆过期瓦片。
- 可见瓦片高优先级，预取（相邻页、相邻瓦片）低优先级。
- 结果队列由 UI 线程在 `WM_TIMER`/自定义消息里取出，插入 LRU 并 `InvalidateRect`。

### 8.4 pixmap → DIB

```c
static HBITMAP pixmap_to_dib(fz_context *ctx, fz_pixmap *pix, int w, int h)
{
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;   /* top-down */
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return NULL;
    const unsigned char *s = fz_pixmap_samples(ctx, pix);   /* 确保 unpacked */
    for (int y = 0; y < h; y++) {
        const unsigned char *sp = s + (size_t)y * pix->stride;
        unsigned char *dp = (unsigned char *)bits + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dp[x*4+0] = sp[x*pix->n+2];   /* B */
            dp[x*4+1] = sp[x*pix->n+1];   /* G */
            dp[x*4+2] = sp[x*pix->n+0];   /* R */
            dp[x*4+3] = 255;              /* A */
        }
    }
    return bmp;
}
```

### 8.5 LRU 缓存

- 键 `(page, zoomLevel, tileX, tileY)`；值 `Tile`。
- 容量按内存上限（如 256 MB）或瓦片数量上限（如 256 块）淘汰。
- 放大倍数用**离散档位**（100% → 150% → 200% → 300% …）以减少重复渲染。

---

## 9. 打码操作

### 9.1 涂抹（自由笔刷）

- 鼠标按下开始一条 `ROP_STROKE`，`mousemove` 追加页面坐标点（做抽稀，
  相邻点距离 < 1px 不记），抬起结束。
- 绘制：`Polyline` + 圆头圆角，`CreatePen(PS_SOLID, widthPx, color)`，
  `widthPx = op->width * zoom`。
- 记录 `width`（point），保证缩放/导出时线宽按比例。

### 9.2 矩形填充（无边框）

- 按下-拖拽-抬起 → 一个 `ROP_RECT`，记录两个对角点。
- 绘制：`FillRect`（或圆角矩形），**不画边框**，颜色 = 当前颜色。

### 9.3 马赛克

- 按下-拖拽-抬起 → 一个 `ROP_MOSAIC`，记录矩形区域与块大小 `block`（point）。
- 合成：取该区域对应的页面位图像素，按 `blockPx = block * zoom` 分块，
  每块取平均色填满，再画回该区域。

```c
/* 对 32bpp DIB 的矩形区域做马赛克（就地） */
static void mosaic_dib(uint32_t *px, int stride_px, int x0,int y0,int w,int h,int block)
{
    for (int by = 0; by < h; by += block)
      for (int bx = 0; bx < w; bx += block) {
        int bw = min(block, w - bx), bh = min(block, h - by);
        unsigned r=0,g=0,b=0,n=0;
        for (int y=0;y<bh;y++) for (int x=0;x<bw;x++) {
            uint32_t c = px[(y0+by+y)*stride_px + (x0+bx+x)];
            r += (c>>16)&255; g += (c>>8)&255; b += c&255; n++;
        }
        uint32_t avg = ((r/n)<<16)|((g/n)<<8)|(b/n);
        for (int y=0;y<bh;y++) for (int x=0;x<bw;x++)
            px[(y0+by+y)*stride_px + (x0+bx+x)] = avg;
      }
}
```

> 马赛克区域在导出时也执行同样算法（对高分辨率位图），保证导出与预览一致。

### 9.4 预览合成

预览表面是一张 32bpp DIB（便于像素级马赛克）。绘制顺序：

1. 填灰底 `RGB(240,240,240)`（与 pdfwatermark 一致）。
2. 画白色页面矩形 + 1px 边框 `RGB(160,160,160)`。
3. 画页面位图（按 `viewScale` 缩放，从瓦片缓存拼接可见区域）。
4. 按顺序应用 `rd.ops[0..applied)` 中当前页的操作：
   - RECT → 填充色块；
   - STROKE → 折线；
   - MOSAIC → 对该区域像素做马赛克（就地 + 每帧重建或缓存）。
5. 画正在进行的操作（实时反馈）。
6. 双缓冲 `BitBlt` 到屏幕，`WM_ERASEBKGND` 返回 1 防闪烁。

> 马赛克需要读取页面位图像素，因此预览表面与页面位图都使用 DIB；马赛克结果
> 可缓存（`dirty` 时重算）以避免每帧重算。

### 9.5 交互

- 左键：当前工具落笔。
- 右键：橡皮（删除命中区域最上层操作）——可选加分项。
- 中键/空格拖拽：平移；滚轮：翻页；Ctrl+滚轮：缩放。
- 快捷键：`Ctrl+Z` 撤销、`Ctrl+Y`/`Ctrl+Shift+Z` 重做、`PgUp/PgDn` 翻页。

---

## 10. 撤销 / 重做

采用「操作表 + 已应用数量」的经典模型，天然支持跨页撤销：

```c
static void rd_push(RedactDoc *d, const RedactOp *op)
{
    if (d->applied < d->n) {                 /* 丢弃 redo 分支 */
        for (int i = d->applied; i < d->n; i++) op_free(&d->ops[i]);
        d->n = d->applied;
    }
    if (d->n == d->cap) { d->cap = d->cap ? d->cap*2 : 64;
        d->ops = realloc(d->ops, d->cap * sizeof(RedactOp)); }
    d->ops[d->n++] = op_clone(op);
    d->applied = d->n;
}
static void rd_undo(RedactDoc *d) { if (d->applied > 0) d->applied--; }
static void rd_redo(RedactDoc *d) { if (d->applied < d->n) d->applied++; }
```

- 内存足够，操作本身很小（折线点数组），不做命令压缩。
- 若折线很长，可对点做 Douglas–Peucker 抽稀后再存。

---

## 11. 保存为 PDF（展平导出）

为保证「真遮盖」，采用**逐页展平**：把每页渲染成高分辨率位图 → 应用该页所有
打码操作 → 作为整页图像写入新 PDF。原始文字/图像不会进入输出文件。

```c
int pdfred_export(App *app, const wchar_t *outPath, float exportZoom)
{
    fz_context *ctx = app->ctx;
    fz_document_writer *wri = NULL;
    char path8[MAX_PATH*3];
    WideCharToMultiByte(CP_UTF8, 0, outPath, -1, path8, sizeof path8, NULL, NULL);

    fz_try(ctx) {
        wri = fz_new_document_writer(ctx, path8, "pdf", "compress");
        for (int i = 0; i < app->npages; i++) {
            fz_page *page = fz_load_page(ctx, app->doc, i);
            fz_rect b = fz_bound_page(ctx, page);
            fz_pixmap *pix = fz_new_pixmap_from_page(
                ctx, page, fz_scale(exportZoom, exportZoom), fz_device_rgb(ctx), 0);

            /* 应用该页的打码操作（页面坐标 -> 像素坐标） */
            apply_redactions_to_pixmap(ctx, pix, &app->rd, i, b, exportZoom);

            fz_image *img = fz_new_image_from_pixmap(ctx, pix, NULL);
            fz_rect mb = fz_make_rect(0, 0, b.x1 - b.x0, b.y1 - b.y0);
            fz_device *dev = fz_begin_page(ctx, wri, mb);
            fz_fill_image(ctx, dev, img,
                          fz_scale((b.x1-b.x0)/pix->w, (b.y1-b.y0)/pix->h),
                          1.0f, fz_default_color_params);
            fz_end_page(ctx, wri);

            fz_drop_image(ctx, img);
            fz_drop_pixmap(ctx, pix);
            fz_drop_page(ctx, page);
        }
        fz_close_document_writer(ctx, wri);
    }
    fz_catch(ctx) { return -1; }
    return 0;
}
```

`apply_redactions_to_pixmap` 与预览合成同源：把 page 坐标操作按
`(page - b.origin) * exportZoom` 映射到像素，依次画矩形/折线/马赛克。

导出的文件名：

```c
/* 原名 + _打码.pdf，例如 文件.pdf -> 文件_打码.pdf */
wcscpy(base, g_srcPath);
e = wcsrchr(base, L'.'); if (e) *e = 0;
wsprintfW(out, L"%s_打码.pdf", base);
```

> 需要 JPEG 压缩体积更小，可把 writer 选项换成 `"compress,no-recompress"` 或
> 在 `fz_new_image_from_pixmap` 前把 pixmap 转成 JPEG 编码的 image；默认用
> Flate，无损但较大。可在 UI 里给「导出画质」开关（无损 Flate / JPEG q=90）。

### 备选方案（已决定不采用）

曾考虑用 MuPDF 的 **redaction 注解**（`PDF_ANNOT_REDACT` + `pdf_redact_page()`）
只删除被遮处的文字/图像、其余区域保持矢量。**因不满足「自由笔刷 + 马赛克」
且遮盖不够彻底，已确定不采用**，统一使用逐页展平方案。此说明仅作溯源保留。

---

## 12. UI 布局（与 pdf-watermark 一致）

风格沿用 `pdf-watermark`：微软雅黑 `S(-14)`、灰底 `RGB(240,240,240)`、
白页 + `RGB(160,160,160)` 边框、双缓冲、DPI 感知（Per-Monitor V2）、
`S(v)=MulDiv(v,dpi,96)` 统一缩放。

```
┌────────────────────────────────────────────────────────────────────┐
│ [打开 PDF] [保存 PDF]   [上一页] 3/12 [下一页]   [撤销] [重做]      │  y=12
│ 工具: (•)自由笔刷 ( )矩形填充 ( )马赛克   [颜色...]  粗细[====] 8px │  y=56
│ 马赛克块 [====] 12px    缩放 [====] 100%  □适应宽度                  │  y=92
│ 提示: 打开 PDF 后可直接涂抹; 保存导出为 原名_打码.pdf               │  y=122
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│                        PDF 预览 / 编辑区                            │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

- 控件 ID 连续编号（`IDC_OPEN=1001` …），风格与 `pdfwatermark.c` 相同。
- 预览区在参数栏（`S(150)` 左右）之下；`WM_SIZE` 重排。
- 状态栏/标题显示：`PDF 打码工具 v1.0.0 2026-10-13`。

---

## 13. 线程安全与内存管理

- UI 线程与渲染线程只通过**队列**交换数据；MuPDF 对象不跨线程。
- `HBITMAP` 属 GDI，可跨线程创建/使用，但**统一在 UI 线程 `DeleteObject`**；
  结果队列里的过期位图由 UI 线程回收。
- 关闭文档：先停止线程池（投递退出信号 + `WaitForMultipleObjects`），
  再释放缓存与主 context。
- 导出可在 UI 线程同步执行（大文件建议放独立线程 + 进度框）。

---

## 14. 构建与依赖

与 `pdf-watermark` 相同：

```
依赖（不随仓库提供）：
  ../pdf-watermark/sumatrapdf/ext/mupdf         (MuPDF 头文件)
  ../pdf-watermark/sumatrapdf/src/shared/mupdf  (noto_sumatra.c)
  ../pdf-watermark/mupdf_build/libmupdf.a       (静态库)
  ./mupdf_fonts/                                (内置字体, 运行时)
```

`build.bat`（草稿）：

```bat
@echo off
setlocal
set TC=D:\SOFT\...\TDM-GCC-64\bin
if not defined CC set "CC=%TC%\gcc.exe"
if not defined WINDRES set "WINDRES=%TC%\windres.exe"
if not defined MUPDF_ROOT set "MUPDF_ROOT=..\pdf-watermark"
if not defined MUPDF set "MUPDF=%MUPDF_ROOT%\sumatrapdf\ext\mupdf"
if not defined NOTO  set "NOTO=%MUPDF_ROOT%\sumatrapdf\src\shared\mupdf\noto_sumatra.c"
if not defined LIBMUPDF set "LIBMUPDF=%MUPDF_ROOT%\mupdf_build\libmupdf.a"

"%WINDRES%" -I. -O coff -o app.res app.rc || exit /b 1
"%CC%" -O2 -mwindows -municode -Wall -I. -Isrc -I"%MUPDF%\include" ^
  -I"%MUPDF_ROOT%\sumatrapdf\src\shared\mupdf" -o pdfredaction.exe ^
  src\main.c src\ui.c src\canvas.c src\redact.c src\render_pool.c src\pdf_doc.c ^
  app.res "%NOTO%" "%LIBMUPDF%" ^
  -lcomctl32 -lgdi32 -lgdiplus -lole32 -loleaut32 -luuid -lcomdlg32 ^
  -lshlwapi -lshell32 -lwindowscodecs -lws2_32 -luser32 -lm || exit /b 1
echo Build OK: pdfredaction.exe
```

---

## 15. 里程碑

| 阶段 | 内容 | 产出 |
| :--- | :--- | :--- |
| M1 | `pdf_doc` 封装：打开、取页数/尺寸、单线程渲染当前页 | 能打开并显示 PDF |
| M2 | Win32 框架 + 预览绘制 + 翻页/缩放（单线程） | 可浏览 |
| M3 | 渲染线程池 + 瓦片 + LRU | 翻页/缩放顺滑 |
| M4 | 自由笔刷 / 矩形填充 / 马赛克 + 颜色/粗细 | 可打码 |
| M5 | 撤销/重做 + 清除 | 编辑完整 |
| M6 | 展平导出 `_打码.pdf` + 导出 DPI | 可用成品 |
| M7 | 细节：右键橡皮、快捷键、进度提示、异常处理、图标/版本 | 发布 v1.0.0 |

---

## 16. 风险与对策

| 风险 | 对策 |
| :--- | :--- |
| MuPDF 线程不安全 | 每线程独立 context + document；不共享 MuPDF 对象 |
| 高分辨率内存暴涨 | 瓦片渲染 + LRU + 导出同样瓦片化后拼接 |
| 快速翻页累积过期渲染 | 请求带 seq，丢弃过期；只保留最新 |
| 坐标/旋转错位 | 一律用 `fz_bound_page` 显示包围盒；op 存 page 坐标 |
| 导出体积大 | 提供 Flate/JPEG 选项；JPEG 默认 q=90 |
| 加密 PDF | 支持打开（MuPDF 处理）；导出为展平图像，天然规避权限问题 |
| 大文件导出慢 | 导出线程 + 进度条；可取消 |

---

## 附录 A · MuPDF API 速查

```c
fz_context *fz_new_context(alloc, locks, max_store);
void        fz_register_document_handlers(fz_context*);
fz_document *fz_open_document(fz_context*, const char *filename);
fz_document *fz_open_document_with_stream(fz_context*, const char *magic, fz_stream*);
fz_stream   *fz_open_memory(fz_context*, const unsigned char *data, size_t len);
int          fz_count_pages(fz_context*, fz_document*);
fz_page     *fz_load_page(fz_context*, fz_document*, int number);
fz_rect      fz_bound_page(fz_context*, fz_page*);
fz_pixmap   *fz_new_pixmap_from_page(fz_context*, fz_page*, fz_matrix ctm,
                                     fz_colorspace*, int alpha);
fz_matrix    fz_scale(float, float); fz_matrix fz_translate(float,float);
fz_matrix    fz_pre_translate(fz_matrix, float, float); fz_rect fz_make_rect(...);
fz_device   *fz_new_draw_device(fz_context*, fz_matrix, fz_pixmap*);
void         fz_run_page(fz_context*, fz_page*, fz_device*, fz_matrix, fz_cookie*);
unsigned char *fz_pixmap_samples(fz_context*, fz_pixmap*);
void         fz_clear_pixmap_with_value(fz_context*, fz_pixmap*, int value);
void         fz_set_aa_level(fz_context*, int);
/* 生成 PDF */
fz_document_writer *fz_new_document_writer(fz_context*, const char*, const char*, const char*);
fz_device   *fz_begin_page(fz_context*, fz_document_writer*, fz_rect);
void         fz_fill_image(fz_context*, fz_device*, fz_image*, fz_matrix, float, fz_color_params);
void         fz_end_page(fz_context*, fz_document_writer*);
void         fz_close_document_writer(fz_context*, fz_document_writer*);
fz_image    *fz_new_image_from_pixmap(fz_context*, fz_pixmap*, fz_image*);
```

> 以仓库里 `../pdf-watermark/src/mupdf_render.c` 为范例（它已经把
> `fz_new_pixmap_from_page` → 32bpp DIB 的流程跑通），本项目在此基础上加
> 瓦片、线程池与打码合成。

## 附录 B · 与 pdf-watermark 的复用

- 字体目录定位、`noto_sumatra.c` 内置字体加载：直接照搬
  `pdf-watermark/src/mupdf_render.c` 的 `pwMuInit` / `loadBuiltinFont`。
- 控件风格、DPI 感知、双缓冲预览：照搬 `pdf-watermark/src/pdfwatermark.c`。
- 构建脚本与 MuPDF 依赖路径：照搬 `pdf-watermark/build.bat`。

---

*文档版本：2026-10-13 · 对应实现目标 v1.0.0*
