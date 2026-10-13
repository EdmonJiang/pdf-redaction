@echo off
rem ============================================================
rem  Build & run the headless self-tests (Windows, TDM-GCC)
rem ============================================================
setlocal
cd /d "%~dp0\.."

set TC=D:\SOFT\Embarcadero_Dev-Cpp_6.3_TDM-GCC_9.2_Portable\TDM-GCC-64\bin
if "%CC%"=="" set CC=%TC%\gcc.exe
if "%MUPDF_ROOT%"=="" set MUPDF_ROOT=..\pdf-watermark
set MUPDF=%MUPDF_ROOT%\sumatrapdf\ext\mupdf\include
set SHARED=%MUPDF_ROOT%\sumatrapdf\src\shared\mupdf
set NOTO=%SHARED%\noto_sumatra.c
set LIB=%MUPDF_ROOT%\mupdf_build\libmupdf.a
set LIBS=-lcomctl32 -lgdi32 -lgdiplus -lole32 -loleaut32 -lcomdlg32 -lshlwapi -lshell32 -lwindowscodecs -luuid -lws2_32 -luser32 -lm
set BASE=-O2 -municode -Wall -I. -Isrc -I"%MUPDF%" -I"%SHARED%"

if not exist "%LIB%" (
    echo [ERROR] MuPDF library not found: %LIB%
    exit /b 1
)

echo Building tests...
"%CC%" %BASE% -o tests\make_synth.exe tests\make_synth.c "%NOTO%" "%LIB%" %LIBS% || exit /b 1
"%CC%" %BASE% -o tests\test_export.exe tests\test_export.c src\ui.c src\canvas.c src\redact.c src\render_pool.c src\pdf_doc.c "%NOTO%" "%LIB%" %LIBS% || exit /b 1
"%CC%" %BASE% -o tests\test_pool.exe tests\test_pool.c src\render_pool.c src\redact.c "%NOTO%" "%LIB%" %LIBS% || exit /b 1

echo Running end-to-end export test (synth.pdf)...
tests\make_synth.exe || exit /b 1
tests\test_export.exe synth.pdf synth_redacted.pdf
if errorlevel 1 (
    echo [FAIL] export test
    exit /b 1
)

echo Running tile pool test...
tests\test_pool.exe
if errorlevel 1 (
    echo [FAIL] pool test
    exit /b 1
)

echo All tests passed.
endlocal
