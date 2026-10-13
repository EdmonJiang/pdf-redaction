# 更新日志 · Changelog

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。
This project adheres to [Semantic Versioning](https://semver.org/).

---

## [未发布] — 2026-10-13

### ✅ 决策 · Decisions

- 导出文件名后缀确定为 **`_打码`**（`文件.pdf` → `文件_打码.pdf`）。
- 采用 **真遮盖**：逐页展平为位图导出，被遮处的原始内容不进输出、无法提取。
- 不采用「MuPDF redaction 注解保留矢量」方案。
- 当前仅完成设计，**暂不编写代码**。

### 📝 文档 · Docs

- 完成实现方案 [`DESIGN.md`](DESIGN.md)：总体架构、数据结构、坐标系、
  多线程高分辨率渲染（每线程独立 MuPDF context + 瓦片 + LRU 缓存）、
  涂抹/矩形/马赛克合成、撤销/重做、展平导出、UI 布局、构建与里程碑。

---

## 版本说明 · Version Notes

- **[未发布]** — 2026-10-13 · 设计文档阶段（代码待实现，目标 v1.0.0）
