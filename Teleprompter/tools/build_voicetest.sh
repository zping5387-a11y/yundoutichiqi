#!/usr/bin/env bash
# 语音命令识别回归测试（纯文本匹配，不需要麦克风）
set -e
cd "$(dirname "$0")/.."
VCTOOLS="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207"
SDKBIN="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
CL="$VCTOOLS/bin/Hostx64/x64/cl.exe"
export PATH="$SDKBIN:$VCTOOLS/bin/Hostx64/x64:$PATH"
export INCLUDE="C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//include;C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//atlmfc//include;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//ucrt;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//shared;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//um;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//winrt"
export LIB="C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//lib//x64;C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//atlmfc//lib//x64;C://Program Files (x86)//Windows Kits//10//Lib//10.0.26100.0//ucrt//x64;C://Program Files (x86)//Windows Kits//10//Lib//10.0.26100.0//um//x64"
mkdir -p build/obj-vtest
"$CL" -nologo -c -EHsc -MT -utf-8 -std:c++17 -W3 -O2 \
      -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -DNDEBUG -Isrc -Ithird_party/miniz \
      "-Fobuild/obj-vtest/" tools/voicetest.cpp src/ScriptDoc.cpp src/OfficeDoc.cpp src/VoiceCommand.cpp
# miniz 是 C 代码，按 C 编译（-TC）
"$CL" -nologo -c -MT -O2 -TC -W3 -D_CRT_SECURE_NO_WARNINGS \
      "-Fobuild/obj-vtest/" third_party/miniz/miniz.c third_party/miniz/miniz_tinfl.c \
      third_party/miniz/miniz_tdef.c third_party/miniz/miniz_zip.c
"$CL" -nologo build/obj-vtest/*.obj "-Fe:build/voicetest.exe" \
      -link -SUBSYSTEM:CONSOLE uafxcw.lib gdiplus.lib
echo "[OK] build/voicetest.exe"
