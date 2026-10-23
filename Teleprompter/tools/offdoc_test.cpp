// offdoc_test.cpp - 命令行验证 .docx / .pptx 文字抽取（不启动界面）
//
// 用法：offdoc_test.exe <file1> [file2 ...]
//   stdout：每个文件先打印 "=== <路径>"，再打印抽出的纯文本（UTF-8）；
//           失败时打印 "!!! <原因>"。任一文件失败 → 退出码 1。
// 断言交给 tools/offdoc_test.py 做。
#include <windows.h>
#include <cstdio>
#include <string>
#include "../src/OfficeDoc.h"

static void PrintUtf8(const std::wstring& s)
{
    if (s.empty()) return;
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return;
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    fwrite(out.data(), 1, out.size(), stdout);
}

int wmain(int argc, wchar_t** argv)
{
    ::SetConsoleOutputCP(CP_UTF8);
    if (argc < 2)
    {
        printf("usage: offdoc_test.exe <file> [file...]\n");
        return 2;
    }

    bool ok = true;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring path = argv[i];
        PrintUtf8(L"=== " + path);
        printf("\n");

        if (!OfficeDoc::IsOfficeFile(path))
        {
            printf("!!! 不是 Office 文档扩展名 (IsOfficeFile=false)\n");
            ok = false;
            continue;
        }

        std::wstring text, err;
        if (!OfficeDoc::LoadText(path, text, err))
        {
            PrintUtf8(L"!!! " + err);
            printf("\n");
            ok = false;
            continue;
        }
        PrintUtf8(text);
        printf("\n");
    }
    return ok ? 0 : 1;
}
