# 更新日志 · Changelog

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。
This project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2026-10-13

### ✅ 实现 · Implemented

- **M1–M2**：`pdf_doc`（MuPDF 打开/页信息/单页渲染）+ Win32 框架 + 连续纵向滚动预览
  （下一页接续显示、页间窄间隙、右侧滚动条、按视口中线判定页码）/ 翻页 / 缩放。
- **M3**：`render_pool` 多线程瓦片渲染（每线程独立 `fz_context`/`fz_document`）
  + 1024² 瓦片 + LRU 缓存 + 过期请求作废。
- **M4**：自由笔刷 / 矩形填充 / 马赛克；颜色选择器、笔刷粗细、马赛克块大小。
- **M5**：撤销/重做（`Ctrl+Z` / `Ctrl+Y`）+ 清除本页 / 清除全部。
- **M6**：真遮盖导出 `原名_打码.pdf`（可选 150/200/300 DPI、JPEG 压缩）：
  **仅对被修改过的页展平为位图**，未改页直接 `pdf_graft_mapped_page` 深拷贝原页，
  内存/体积/速度大幅优化（500 页、改 1 页：峰值内存 ~25MB，原逐页展平约 9GB）。
- **M7**：快捷键、状态提示、DPI 感知（Per-Monitor V2）、图标/版本资源；
  滚轮纵向滚动、`Ctrl+滚轮` 缩放、最大缩放 500%（任意 DPI 下滑块均可到端点）。

### 🐛 修复 · Fixes

- 取消瓦片预取：预取会让“可见 + 预取”超出 LRU 容量，形成
  “渲染→淘汰→再请求”死循环（空闲 CPU ≈158% → 0%）。
- 滚动条改用 `GetScrollInfo(SIF_TRACKPOS)` 的 32 位位置，
  修复大文档（>32767）滑块拉几页就到底的问题。
- `WM_SIZE` 时 `RedrawWindow(RDW_ALLCHILDREN)`，修复最大化后工具栏黑块。

### ✅ 决策 · Decisions

- 导出文件名后缀 **`_打码`**（`文件.pdf` → `文件_打码.pdf`）。
- 采用 **真遮盖**：被修改页展平为位图导出，被遮处的原始内容不进输出、无法提取；
  未修改页保留原矢量内容。
- 不采用「MuPDF redaction 注解保留矢量」方案。

### 🧪 测试 · Tests

- `tests/test_export.c`：端到端导出并回读像素校验（遮盖、方向）。
- `tests/make_synth.c`：生成方向校验用合成 PDF。
- `tests/test_pool.c`：多线程瓦片线程池验证。
- `tests/test_big.c`：生成 500 页 PDF 并测量导出峰值内存。
- `tests/test_scroll.c` / `tests/test_scroll_big.c`：连续滚动、页码中线判定、
  最大化重绘、缩放端点、大文档滚动条范围。

### 📝 文档 · Docs

- 完成实现方案 [`DESIGN.md`](DESIGN.md)。

---

## 版本说明 · Version Notes

- **[1.0.0]** — 2026-10-13 · 首个可用版本（open / 打码 / 撤销重做 / 展平导出）。
- **[未发布]** — 2026-10-13 · 设计文档阶段（代码待实现，目标 v1.0.0）
