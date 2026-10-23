#!/usr/bin/env bash
# 构建 Office 文稿抽取单测（不需要 MFC）
set -e
cd "$(dirname "$0")/.."
VCTOOLS="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207"
SDKBIN="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
CL="$VCTOOLS/bin/Hostx64/x64/cl.exe"
export PATH="$SDKBIN:$VCTOOLS/bin/Hostx64/x64:$PATH"
export INCLUDE="C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//include;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//ucrt;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//shared;C://Program Files (x86)//Windows Kits//10//Include//10.0.26100.0//um"
export LIB="C://Program Files//Microsoft Visual Studio//2022//Community//VC//Tools//MSVC//14.44.35207//lib//x64;C://Program Files (x86)//Windows Kits//10//Lib//10.0.26100.0//ucrt//x64;C://Program Files (x86)//Windows Kits//10//Lib//10.0.26100.0//um//x64"

mkdir -p build/obj-odoc
"$CL" -nologo -c -EHsc -MT -utf-8 -std:c++17 -W3 -O2 \
      -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -DNDEBUG -Isrc -Ithird_party/miniz \
      "-Fobuild/obj-odoc/" tools/offdoc_test.cpp src/OfficeDoc.cpp
"$CL" -nologo -c -MT -O2 -TC -W3 -D_CRT_SECURE_NO_WARNINGS -Ithird_party/miniz \
      "-Fobuild/obj-odoc/" third_party/miniz/miniz.c third_party/miniz/miniz_tinfl.c \
      third_party/miniz/miniz_tdef.c third_party/miniz/miniz_zip.c
"$CL" -nologo build/obj-odoc/*.obj "-Fe:build/offdoc_test.exe" -link -SUBSYSTEM:CONSOLE
echo "[OK] build/offdoc_test.exe"
