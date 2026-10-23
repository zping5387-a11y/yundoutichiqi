@echo off
rem ============================================================
rem  命令行编译（无需打开 Visual Studio）
rem  输出：build\TeacherPrompter.exe （静态链接 MFC，免安装运行库）
rem ============================================================
setlocal

set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "MSVCVER=14.44.35207"
set "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"

set "VCTOOLS=%VSDIR%\VC\Tools\MSVC\%MSVCVER%"
set "INCLUDE=%VCTOOLS%\include;%VCTOOLS%\atlmfc\include;%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\winrt"
set "LIB=%VCTOOLS%\lib\x64;%VCTOOLS%\atlmfc\lib\x64;%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64"
set "PATH=%VCTOOLS%\bin\Hostx64\x64;%SDKROOT%\bin\%SDKVER%\x64;%PATH%"

cd /d "%~dp0.."
if not exist build\obj-mt mkdir build\obj-mt

cl /nologo /c /EHsc /MT /utf-8 /std:c++17 /O2 /W3 ^
   /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS /DNDEBUG ^
   /Fobuild\obj-mt\ src\AppSettings.cpp src\ScriptDoc.cpp src\ScriptMatcher.cpp ^
   src\VoiceCommand.cpp src\SpeechEngine.cpp src\RecentListWnd.cpp ^
   src\OverlayWnd.cpp src\ControlDlg.cpp src\TeleprompterApp.cpp
if errorlevel 1 goto :fail

rc /nologo /fo build\Teleprompter.res src\Teleprompter.rc
if errorlevel 1 goto :fail

cl /nologo build\obj-mt\*.obj build\Teleprompter.res /Fe:build\TeacherPrompter.exe ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MANIFEST:EMBED uafxcw.lib
if errorlevel 1 goto :fail

echo.
echo [OK] build\TeacherPrompter.exe
exit /b 0

:fail
echo.
echo [FAILED] 编译失败，请查看上面的错误信息。
exit /b 1
