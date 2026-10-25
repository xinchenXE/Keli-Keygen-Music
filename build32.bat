@echo off
rem ===================================================================
rem  Keil keygen tune - 32-bit smallest build (PE32, runs on Win11 x64)
rem  clang -Oz -flto + lld, no CRT, then UPX
rem ===================================================================
setlocal
set CC=E:\project\test\tools\llvm-mingw-20261006-msvcrt-i686\bin\i686-w64-mingw32-clang.exe
set UPX=E:\project\test\tools\upx\upx.exe
cd /d "%~dp0"

"%CC%" -Oz -flto -nostdlib -DMINI_PLAYER -DAUDIODRIVER_NULL -Wl,--entry=__start -Wl,-s ^
  -o Keil32.exe main.c replayer_min.c paula.c nulldriver.c -lkernel32 -lmsvcrt -lwinmm

copy /y Keil32.exe Keil32_upx.exe >nul
if exist "%UPX%" "%UPX%" --best -q Keil32_upx.exe

echo.
for %%f in (Keil32.exe Keil32_upx.exe) do echo   %%f  %%~zf bytes
endlocal
