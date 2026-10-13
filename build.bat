@echo off
rem ============================================================
rem  Build pdfredaction.exe  (Windows, TDM-GCC 64-bit)
rem ============================================================
rem  Prerequisites
rem    ..\pdf-watermark\sumatrapdf\ext\mupdf      MuPDF headers
rem    ..\pdf-watermark\sumatrapdf\src\shared\mupdf\noto_sumatra.c
rem    ..\pdf-watermark\mupdf_build\libmupdf.a    MuPDF static lib
rem    .\mupdf_fonts\                             built-in fonts (runtime)
rem
rem  Override the toolchain with:
rem    set CC=D:\path\to\gcc.exe
rem ============================================================
setlocal

set TC=D:\SOFT\Embarcadero_Dev-Cpp_6.3_TDM-GCC_9.2_Portable\TDM-GCC-64\bin
if "%CC%"==""        set CC=%TC%\gcc.exe
if "%WINDRES%"==""   set WINDRES=%TC%\windres.exe
if "%MUPDF_ROOT%"=="" set MUPDF_ROOT=..\pdf-watermark
if "%MUPDF%"==""     set MUPDF=%MUPDF_ROOT%\sumatrapdf\ext\mupdf
if "%NOTO%"==""      set NOTO=%MUPDF_ROOT%\sumatrapdf\src\shared\mupdf\noto_sumatra.c
if "%MUPDF_LIB%"=="" set MUPDF_LIB=%MUPDF_ROOT%\mupdf_build\libmupdf.a

if not exist "%CC%" (
    echo [ERROR] compiler not found: %CC%
    echo         set CC=^<path to gcc.exe^>
    exit /b 1
)
if not exist "%MUPDF_LIB%" (
    echo [ERROR] MuPDF library not found: %MUPDF_LIB%
    echo         build it first in pdf-watermark:  sh tools\build_mupdf.sh
    exit /b 1
)

echo Compiling resources...
"%WINDRES%" -I. -O coff -o app.res app.rc
if errorlevel 1 (
    echo [ERROR] windres failed
    exit /b 1
)

echo Compiling sources...
"%CC%" -O2 -mwindows -municode -Wall ^
  -I. -Isrc -I"%MUPDF%\include" -I"%MUPDF_ROOT%\sumatrapdf\src\shared\mupdf" ^
  -o pdfredaction.exe ^
  src\main.c src\ui.c src\canvas.c src\redact.c src\render_pool.c src\pdf_doc.c ^
  app.res "%NOTO%" "%MUPDF_LIB%" ^
  -lcomctl32 -lgdi32 -lgdiplus -lole32 -loleaut32 -lcomdlg32 ^
  -lshlwapi -lshell32 -lwindowscodecs -luuid -lws2_32 -luser32 -lm

if errorlevel 1 (
    echo Build FAILED
    exit /b 1
)
echo Build OK: pdfredaction.exe
endlocal
