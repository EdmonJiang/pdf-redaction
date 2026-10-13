# Makefile for pdf-redaction (Windows / MinGW-w64)
#
# 依赖:
#   1) libmupdf.a   由 pdf-watermark/tools/build_mupdf.sh 生成
#   2) mupdf_fonts/ 内置字体 (见 README)
#
# 用法:
#   make            构建 pdfredaction.exe
#   make clean      清理

CC       ?= gcc
WINDRES  ?= windres
MUPDF_ROOT ?= ../pdf-watermark
MUPDF_DIR  ?= $(MUPDF_ROOT)/sumatrapdf/ext/mupdf
NOTO_C     ?= $(MUPDF_ROOT)/sumatrapdf/src/shared/mupdf/noto_sumatra.c
MUPDF_LIB  ?= $(MUPDF_ROOT)/mupdf_build/libmupdf.a

SRC = src/main.c src/ui.c src/canvas.c src/redact.c src/render_pool.c src/pdf_doc.c

CFLAGS  = -O2 -mwindows -municode -Wall -I. -Isrc \
          -I$(MUPDF_DIR)/include -I$(MUPDF_ROOT)/sumatrapdf/src/shared/mupdf
LDFLAGS = -mwindows -municode
LIBS    = $(MUPDF_LIB) $(NOTO_C) \
          -lcomctl32 -lgdi32 -lgdiplus -lole32 -loleaut32 -lcomdlg32 \
          -lshlwapi -lshell32 -lwindowscodecs -luuid -lws2_32 -luser32 -lm

TARGET = pdfredaction.exe

all: $(TARGET)

app.res: app.rc redaction.ico
	$(WINDRES) -I. -O coff -o $@ app.rc

$(TARGET): $(SRC) app.res $(MUPDF_LIB)
	$(CC) $(CFLAGS) -o $@ $(SRC) app.res $(LIBS) $(LDFLAGS)

clean:
	rm -f $(TARGET) app.res

.PHONY: all clean
