# MuPDF 集成笔记

面向本项目的 MuPDF 使用要点（版本以 `pdf-watermark` 工作区构建的 `libmupdf.a` 为准）。

## 1. 上下文与线程安全

- `fz_context` **不是线程安全的**；`fz_document` / `fz_page` / `fz_pixmap` 也都不是。
- 多线程渲染推荐两种模型：

  **A. 每线程独立 context + 独立 document（本项目采用，最安全）**
  ```c
  fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
  fz_register_document_handlers(ctx);
  fz_stream *stm = fz_open_memory(ctx, shared_data, shared_len); /* 共享只读内存 */
  fz_document *doc = fz_open_document_with_stream(ctx, "application/pdf", stm);
  fz_drop_stream(ctx, stm);
  ```
  线程之间不共享任何 MuPDF 对象，无需加锁。

  **B. `fz_clone_context` + 共享 document + 全局锁**
  省内存，但要自己保证 document 访问串行化，容易出隐蔽竞态。

- 退出：每个线程 `fz_drop_document` → `fz_drop_context`。

## 2. 页面与显示包围盒

- `fz_load_page(ctx, doc, n)` 取页；`fz_bound_page(ctx, page)` 返回**显示包围盒**
  （已应用 `/Rotate`、`CropBox`），直接作为渲染/坐标基准，避免自行处理旋转。
- 页尺寸（point）：`w = b.x1 - b.x0`, `h = b.y1 - b.y0`。

## 3. 渲染

```c
fz_matrix ctm = fz_scale(zoom, zoom);            /* zoom = dpi / 72 */
fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
```

- 想更高质量：`fz_set_aa_level(ctx, 8)`（或分别设 text/graphics AA）。
- 白底：`fz_clear_pixmap_with_value(ctx, pix, 255)`（先 clear 再渲染，或渲染到
  单独 pixmap 再合成）。
- **瓦片**：不要对每个瓦片调用 `fz_new_pixmap_from_page`（会整页重渲染）。
  改为自己建 pixmap + draw device：
  ```c
  fz_pixmap *tile = fz_new_pixmap(ctx, fz_device_rgb(ctx), tw, th, NULL, 0);
  fz_clear_pixmap_with_value(ctx, tile, 255);
  fz_device *dev = fz_new_draw_device(ctx, fz_identity, tile);
  fz_matrix ctm = fz_scale(z, z);
  ctm = fz_pre_translate(ctm, -(float)tx / z, -(float)ty / z); /* 平移瓦片到原点 */
  fz_run_page(ctx, page, dev, ctm, NULL);
  fz_close_device(ctx, dev);
  fz_drop_device(ctx, dev);
  ```

## 4. 读取像素

- 用 `fz_pixmap_samples(ctx, pix)` 取得**已解包**的采样指针（`pix->samples`
  可能为 NULL / 未解包）。
- 布局：`samples[y*stride + x*n + c]`，`n` 为每像素分量数（RGB=3，RGBA=4）。
  转 32bpp DIB 时注意 BGRA 顺序。
- `stride` 可能大于 `w*n`，按 `stride` 跳行。

## 5. 生成 PDF

用 document writer 最省事：

```c
fz_document_writer *wri = fz_new_document_writer(ctx, path8, "pdf", "compress");
for (每页) {
    fz_page *page = fz_load_page(ctx, doc, i);
    fz_rect b = fz_bound_page(ctx, page);
    fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, fz_scale(z, z), fz_device_rgb(ctx), 0);
    /* ... 在 pix 上应用打码 ... */
    fz_image *img = fz_new_image_from_pixmap(ctx, pix, NULL);
    fz_rect mb = fz_make_rect(0, 0, b.x1 - b.x0, b.y1 - b.y0);
    fz_device *dev = fz_begin_page(ctx, wri, mb);
    fz_fill_image(ctx, dev, img, fz_scale((b.x1-b.x0)/pix->w, (b.y1-b.y0)/pix->h),
                  1.0f, fz_default_color_params);
    fz_end_page(ctx, wri);
    fz_drop_image(ctx, img); fz_drop_pixmap(ctx, pix); fz_drop_page(ctx, page);
}
fz_close_document_writer(ctx, wri);
```

- writer 选项：`"compress"`（Flate）、`"compress,compress-images"` 等；也可在
  生成 image 前先 JPEG 编码以减小体积。
- 需要保留矢量内容时改用 `pdf_create_document` + `pdf_add_page` +
  `pdf_insert_page` + `pdf_save_document`，或 MuPDF 的 redaction 注解
  `pdf_redact_page`（限制见 `DESIGN.md` 第 11 节）。

## 6. 错误处理

- 把所有 MuPDF 调用包在 `fz_try(ctx) { ... } fz_catch(ctx) { ... }` 中。
- catch 里用 `fz_caught_message(ctx)` 取错误信息（UTF-8），转宽字符给用户。
- `fz_try`/`fz_catch` 不能跨函数边界（setjmp 限制），每个可能抛错的函数
  自带 try/catch。

## 7. 内置字体

- `noto_sumatra.c` 提供 `fz_set_builtin_font_loader(loader)`；
  本项目运行时从 exe 同级 `mupdf_fonts/{urw,droid,sil}/` 加载。
  直接复用 `pdf-watermark/src/mupdf_render.c` 的 `loadBuiltinFont` / `pwMuInit`。

## 8. 常见坑

| 现象 | 原因 / 对策 |
| :--- | :--- |
| 水印/打码上下镜像 | 页面内容流未配对的 `cm` 残留；用 `q`/`Q` 包住或复位矩阵 |
| 多线程随机崩溃 | 共享了 `fz_context`/`fz_document`；改每线程独立 |
| 内存暴涨 | 整页高 DPI；改瓦片 + LRU + 导出分块 |
| 渲染结果颜色不对 | 忘了 `fz_device_rgb`；或 BGR/RGB 搞反 |
| `samples` 为 NULL | 用 `fz_pixmap_samples()` 而不是直接访问字段 |
| 字体缺失/豆腐块 | `mupdf_fonts/` 未随 exe 部署，或路径不对 |
