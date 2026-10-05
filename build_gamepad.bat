@echo off
setlocal EnableDelayedExpansion

title GoldHEN Gamepad Helper Builder
color 0A

cd /D "%~dp0"

echo [+] Workdir: !CD!

:: 1. ضبط المسارات وتزويد الـ PATH بمترجم الويندوز
if "%OO_PS4_TOOLCHAIN%"=="" set "OO_PS4_TOOLCHAIN=C:\OpenOrbis\PS4Toolchain"
if "%GOLDHEN_SDK%"=="" set "GOLDHEN_SDK=C:\GoldHEN_Plugins_SDK-main"
set "PATH=%OO_PS4_TOOLCHAIN%\bin\windows;%OO_PS4_TOOLCHAIN%\bin;%PATH%"

rd /s /q bin >nul 2>&1
mkdir bin\plugins
set BINDIR=!CD!\bin\plugins
echo [+] Output directory: !BINDIR!

set CC=clang
set CXX=clang++
set LD=ld.lld
set GH_SDK=%GOLDHEN_SDK%

set DEFS=-D_BSD_SOURCE=1 -D__BSD_VISIBLE=1 -D__PS4__=1 -DOO=1 -D__OPENORBIS__=1 -D__OOPS4__=1 -D__FINAL__=1
set LIBS=-lGoldHEN_Hook -lkernel -lc -lc++ -lSceVideoOut -lSceScreenShot -lSceVideoRecording -lSceSysmodule -lSceSystemService -lScePad
set COMMONFLAGS=--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -isysroot "%OO_PS4_TOOLCHAIN%" -isystem "%OO_PS4_TOOLCHAIN%\include" -I"%GOLDHEN_SDK%\include" -I"%~dp0common" -Wno-c99-designator %DEFS%
set LDFLAGS=-m elf_x86_64 -pie -e _init --script "%OO_PS4_TOOLCHAIN%\link.x" --eh-frame-hdr -L"%OO_PS4_TOOLCHAIN%\lib" -L%GH_SDK% %LIBS%

:: 2. تجهيز ملف git_ver.h منعاً للأخطاء
if not exist "common" mkdir "common"
echo #pragma once > common\git_ver.h
echo #define GIT_COMMIT "1.0.0" >> common\git_ver.h
echo #define GIT_VER "main" >> common\git_ver.h
echo #define GIT_NUM 100 >> common\git_ver.h
echo #define BUILD_DATE "%DATE% %TIME%" >> common\git_ver.h

:: 3. تحديد إضافة gamepad_helper حصراً
set "G=plugin_src\gamepad_helper"
set "PLUGIN_NAME=gamepad_helper"

echo.
echo [+] Building target: !PLUGIN_NAME!

rd /s /q "!G!\build" >nul 2>&1
mkdir "!G!\build"

set OBJS=
for %%f in ("!G!\source\*.c") do (
    if exist "%%f" (
        %CC% %COMMONFLAGS% -I"!G!\include" -c "%%f" -o "!G!\build\%%~nf.c.o"
        set "OBJS=!OBJS! "!G!\build\%%~nf.c.o""
    )
)

echo [+] Linking and creating PRX...
:: إضافة crtprx.o لضمان ربط الموديول بشكل صحيح مع GoldHEN SDK
%LD% "%GOLDHEN_SDK%\build\crtprx.o" %LDFLAGS% !OBJS! -o "!G!\build\!PLUGIN_NAME!.elf"

if exist "!G!\build\!PLUGIN_NAME!.elf" (
    "%OO_PS4_TOOLCHAIN%\bin\windows\create-fself.exe" -in="!G!\build\!PLUGIN_NAME!.elf" -out="!G!\build\!PLUGIN_NAME!.oelf" -lib="!G!\build\!PLUGIN_NAME!.prx" --paid 0x3800000000000011
    copy /Y "!G!\build\!PLUGIN_NAME!.prx" "!BINDIR!\" >nul
    echo.
    echo ====================================================
    echo [+] SUCCESS! File created:
    echo     bin\plugins\gamepad_helper.prx
    echo ====================================================
) else (
    echo.
    echo ====================================================
    echo [!] ERROR: Linking failed. Check the errors above.
    echo ====================================================
)

pause