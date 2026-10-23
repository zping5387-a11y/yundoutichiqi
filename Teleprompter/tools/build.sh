#!/usr/bin/env bash
# 命令行构建脚本（Git Bash / MSYS 环境）。Windows 下也可直接用 tools/build_cl.bat
set -e
cd "$(dirname "$0")/.."

VCTOOLS="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207"
SDKBIN="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
CL="$VCTOOLS/bin/Hostx64/x64/cl.exe"

# 离线语音识别（sherpa-onnx）：预编译包是 MT Release，和本工程的 -MT 一致
SHERPA="$PWD/third_party/sherpa/sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts"
SHERPA_WIN=$(cygpath -w "$SHERPA")

# Office 文档（.docx/.pptx）解包用 miniz（zlib 兼容的 zip/inflate，C 代码）
MINIZ="$PWD/third_party/miniz"
MINIZ_WIN=$(cygpath -w "$MINIZ")

export PATH="$SDKBIN:$VCTOOLS/bin/Hostx64/x64:$PATH"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\include;C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\atlmfc\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\winrt;$SHERPA_WIN\\include;$MINIZ_WIN"
export LIB="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\lib\\x64;C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\atlmfc\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;$SHERPA_WIN\\lib"

mkdir -p build/obj-mt

"$CL" -nologo -c -EHsc -MT -utf-8 -std:c++17 -W3 -O2 \
      -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -DNDEBUG \
      "-Fobuild/obj-mt/" src/AppSettings.cpp src/ScriptDoc.cpp src/ScriptMatcher.cpp \
      src/OfficeDoc.cpp src/VoiceCommand.cpp src/AudioCapture.cpp src/SpeechEngine.cpp \
      src/SherpaEngine.cpp src/ModelInstaller.cpp src/OverlayWnd.cpp src/RecentListWnd.cpp \
      src/ControlDlg.cpp src/TeleprompterApp.cpp

# miniz 是 C 代码，按 C 编译（-TC）
"$CL" -nologo -c -MT -O2 -TC -W3 -D_CRT_SECURE_NO_WARNINGS "-Fobuild/obj-mt/" \
      third_party/miniz/miniz.c third_party/miniz/miniz_tinfl.c \
      third_party/miniz/miniz_tdef.c third_party/miniz/miniz_zip.c

"$SDKBIN/rc.exe" -nologo -fo build/Teleprompter.res src/Teleprompter.rc

"$CL" -nologo build/obj-mt/*.obj build/Teleprompter.res "-Fe:build/TeacherPrompter.exe" \
      -link -SUBSYSTEM:WINDOWS -ENTRY:wWinMainCRTStartup -MANIFEST:EMBED uafxcw.lib \
      sherpa-onnx-c-api.lib

# 运行时需要这三个 DLL 与 exe 同目录
cp -f "$SHERPA/lib/sherpa-onnx-c-api.dll" "$SHERPA/lib/onnxruntime.dll" \
      "$SHERPA/lib/onnxruntime_providers_shared.dll" build/

echo "[OK] build/TeacherPrompter.exe"
