// voicetest.cpp - 离线验证语音命令识别：口头说法 / 标点 / 正文误伤
#include "framework.h"
#include "ScriptDoc.h"
#include "VoiceCommand.h"
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")

static FILE* g_log = nullptr;

static void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    char utf8[4096];
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n > 0) { fwrite(utf8, 1, (size_t)n - 1, g_log); fputc('\n', g_log); fflush(g_log); }
}

static const wchar_t* CmdName(VoiceCmd c)
{
    switch (c)
    {
    case VoiceCmd::PageDown: return L"下一页";
    case VoiceCmd::PageUp:   return L"上一页";
    case VoiceCmd::ToTop:    return L"回顶部";
    default:                 return L"不是命令";
    }
}

struct Case
{
    const wchar_t* said;
    VoiceCmd expect;
};

static const Case kCases[] =
{
    // ---- 正常说法 ----
    { L"下一页",                    VoiceCmd::PageDown },
    { L"下一页。",                  VoiceCmd::PageDown },
    { L"向下翻一页",                VoiceCmd::PageDown },
    { L"往下翻一页",                VoiceCmd::PageDown },
    { L"往下翻",                    VoiceCmd::PageDown },
    { L"向下翻页",                  VoiceCmd::PageDown },
    { L"下一屏",                    VoiceCmd::PageDown },
    { L"请翻到下一页",              VoiceCmd::PageDown },   // 5 字前缀，允许
    { L"帮我往下翻一页",            VoiceCmd::PageDown },

    { L"上一页",                    VoiceCmd::PageUp },
    { L"上一页！",                  VoiceCmd::PageUp },
    { L"向上翻一页",                VoiceCmd::PageUp },
    { L"往上翻一页",                VoiceCmd::PageUp },
    { L"往上翻",                    VoiceCmd::PageUp },
    { L"翻到上一页",                VoiceCmd::PageUp },

    { L"回到顶部",                  VoiceCmd::ToTop },
    { L"回到开头",                  VoiceCmd::ToTop },
    { L"从头开始",                  VoiceCmd::ToTop },
    { L"回到第一页",                VoiceCmd::ToTop },
    { L"请回到最上面",              VoiceCmd::ToTop },

    // ---- 英文（语言切英语时）----
    { L"Next page",                 VoiceCmd::PageDown },
    { L"Please turn to the next page", VoiceCmd::PageDown },
    { L"previous page",             VoiceCmd::PageUp },

    // ---- 正文 / 闲聊必须不命中 ----
    { L"同学们，上课！",            VoiceCmd::None },
    { L"下一页的内容我们下节课再讲", VoiceCmd::None },
    { L"我们来看上一页的这张图",    VoiceCmd::None },
    { L"浮力的大小与物体排开液体的体积有关", VoiceCmd::None },
    { L"今天天气不错我们出去打球吧顺便买瓶可乐", VoiceCmd::None },
    { L"翻页",                      VoiceCmd::None },   // 没方向，不动
    { L"",                          VoiceCmd::None },
};

int wmain()
{
    _wfopen_s(&g_log, L"build\\voicetest.txt", L"wb");

    int pass = 0, total = 0;
    for (const Case& c : kCases)
    {
        const wchar_t* label = nullptr;
        const VoiceCmd got = MatchVoiceCommand(c.said, &label);
        const bool ok = (got == c.expect);

        ++total;
        if (ok) ++pass;
        Log(L"[%s] 期望=%-6s 实际=%-6s 标签=%s  「%s」",
            ok ? L"PASS" : L"FAIL", CmdName(c.expect), CmdName(got),
            label ? label : L"-", c.said);
    }

    Log(L"");
    Log(L"通过 %d / %d", pass, total);

    fclose(g_log);
    return pass == total ? 0 : 1;
}
