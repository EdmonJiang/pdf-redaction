# PDF 打码工具 · PDF Redaction (C / Windows)

纯 **C + Win32 + MuPDF** 的原生 Windows PDF 打码工具。
打开 PDF，多线程高分辨率渲染为位图，用**自由笔刷涂抹 / 矩形填充 / 马赛克**遮盖
敏感信息，支持**撤销/重做**与**翻页**，最后导出为 `原名_打码.pdf`。
界面风格与 [`pdf-watermark`](../pdf-watermark) 保持一致。

> 状态：**实现方案（设计文档）阶段**，代码尚未开始编写。
> 完整技术方案见 [`DESIGN.md`](DESIGN.md)。

---

## 功能一览

- **打开 / 保存 PDF**：导出文件名 = 原名 + `_打码.pdf`（`文件.pdf` → `文件_打码.pdf`）
- **多线程高分辨率渲染**：每线程独立 MuPDF context，瓦片渲染 + LRU 缓存
- **翻页 / 缩放**：按钮、滚轮、快捷键
- **三种打码工具**
  - 自由笔刷涂抹
  - 矩形填充（无边框，填充色 = 当前颜色）
  - 马赛克（块大小可调）
- **参数**：颜色（取色器）、笔刷粗细、马赛克块大小
- **撤销 / 重做**（`Ctrl+Z` / `Ctrl+Y`）
- **真遮盖**：导出把每页展平为位图，被遮盖处的原始内容不再存在于输出 PDF

## 技术要点

| | |
| :--- | :--- |
| 语言 / 界面 | C + Win32 API + comctl32（与 pdf-watermark 一致） |
| 渲染 / PDF | [MuPDF](https://mupdf.com/)（`libmupdf.a`） |
| 多线程 | 每线程独立 `fz_context` + `fz_document`（MuPDF 非线程安全） |
| 位图 | 32bpp top-down DIB，便于像素级马赛克与 alpha 合成 |
| 导出 | `fz_new_document_writer("pdf")`，逐页展平为全页图像 |

## 目录结构（规划）

```
pdf-redaction/
├── DESIGN.md            ← 详细实现方案（先看这个）
├── README.md
├── src/
│   ├── main.c           框架窗口 / 消息循环
│   ├── ui.c / ui.h      控件布局与状态
│   ├── canvas.c / .h    预览绘制 / 坐标 / 交互
│   ├── redact.c / .h    打码操作 + 撤销重做
│   ├── render_pool.c/.h 多线程渲染 + 瓦片 LRU
│   └── pdf_doc.c / .h   MuPDF 封装（打开/渲染/导出）
├── tools/build_mupdf.sh
├── build.bat  Makefile  app.rc  redaction.ico
└── docs/MUPDF_NOTES.md
```

## 依赖

复用 `pdf-watermark` 工作区已构建好的 MuPDF（构建方法见其 `tools/build_mupdf.sh`）：

```
../pdf-watermark/sumatrapdf/ext/mupdf         # 头文件
../pdf-watermark/sumatrapdf/src/shared/mupdf  # noto_sumatra.c
../pdf-watermark/mupdf_build/libmupdf.a       # 静态库
./mupdf_fonts/                                # 内置字体（运行时）
```

## 后续

按 `DESIGN.md` 第 15 节的里程碑 M1→M7 实现。完成后：
`build.bat` → `pdfredaction.exe`。

---

MIT License（本项目自身代码）；链接 MuPDF（AGPL-3.0），分发二进制需遵守其许可。
