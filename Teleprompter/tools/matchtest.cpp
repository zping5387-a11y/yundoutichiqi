// matchtest.cpp - 离线验证跟读定位（精确匹配）：喂稿件原文片段 / 流式增量 / 越界干扰
#include "framework.h"
#include "ScriptDoc.h"
#include "ScriptMatcher.h"
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "gdiplus.lib")

static FILE* g_log = nullptr;

static void Log(const wchar_t* fmt, ...)
{
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    char utf8[8192];
    int n = ::WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n > 0) { fwrite(utf8, 1, (size_t)n - 1, g_log); fputc('\n', g_log); fflush(g_log); }
}

static const wchar_t* kScript =
    L"同学们，上课！上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，我们来学习浮力的大小究竟与什么有关。"
    L"先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力，而且按得越深，这个力越大。"
    L"这说明，浸在液体中的物体受到向上的浮力，浮力的大小与物体排开液体的体积有关。"
    L"那么，浮力到底等于多少呢？我们用弹簧测力计做一个小实验。"
    L"第一步，把石块挂在测力计下，读出空气中的示数，记为 G。"
    L"第二步，把石块缓慢浸入水中，读出此时的示数，记为 F。"
    L"第三步，我们发现，G 减去 F，正好等于被石块排开的那部分水的重力。"
    L"于是我们得到阿基米德原理：浸在液体中的物体所受浮力，等于它排开的液体所受的重力。";

struct Case
{
    const wchar_t* reco;
    int  expect;   // 归一化稿件里的期望终点；-1 表示"不应该移动"
    bool stream;   // 是否为流式增量（同一句不断变长的中间结果）
};

static const Case kCases[] =
{
    // ---- 流式增量：同一句话从短到长 ----
    { L"同学们", -1 },
    { L"同学们上课", 5, true },
    { L"同学们上课上一节课我们讨论了物体在液体中的沉浮条件", 25, true },

    // ---- 稿件原文片段，逐句推进 ----
    { L"来学习浮力的大小究竟与什么有关", 47 },

    // ---- 句中同音错字（「浮力」→「福利」）：整句对不上，但句尾整块仍精确 ----
    { L"同学们上课上一节课我们讨论了物体在液体中的沉浮条件今天这节课我们来学习福利的大小究竟与什么有关先看一个生活中的现象把一个", 60 },

    { L"先看一个生活中的现象把一个空矿泉水瓶按进水里", 69 },
    { L"手会感觉到一个向上的力", 80 },
    { L"浸在液体中的物体受到向上的浮力，浮力的大小与物体排开液体的体积有关。", 126 },
    { L"那么，浮力到底等于多少呢？我们用弹簧测力计做一个小实验。", 151 },
    { L"第一步，把石块挂在测力计下，读出空气中的示数，记为 G。", 174 },

    // ---- 尾部多出识别噪声（末尾字还没稳定）：截尾后仍应精确命中 ----
    { L"第二步把石块缓慢浸入水中读出此时的示数记为F啊", 196 },

    // ---- 句首多带一个字（吞字/上一句零头）：掐头后仍应精确命中 ----
    { L"呃于是我们得到阿基米德原理浸在液体中的物体所受浮力等于它排开的液体所受的重力", 261 },

    // ---- 同一句重复上报：位置不能乱跳 ----
    { L"于是我们得到阿基米德原理浸在液体中的物体所受浮力等于它排开的液体所受的重力", 261, true },

    // ---- 稿件里没有的话：必须不动 ----
    { L"今天天气不错我们出去打球吧顺便买瓶可乐", -1 },
};

int wmain()
{
    _wfopen_s(&g_log, L"build\\matchtest.txt", L"wb");

    CScriptDoc doc;
    doc.SetText(kScript);
    CScriptMatcher m;
    m.Reset(&doc.Normalized());

    const std::wstring& n = doc.Normalized();
    Log(L"归一化稿件长度: %d", (int)n.size());
    Log(L"稿件: %s", n.c_str());
    Log(L"");

    int pass = 0, total = 0;
    int prevPos = 0;

    for (const Case& c : kCases)
    {
        int pos = 0;
        float score = 0.0f;
        const bool ok = m.Feed(c.reco, pos, score);
        const int actual = ok ? pos : prevPos;

        bool good;
        if (c.expect < 0) good = (!ok) || (actual == prevPos);
        else              good = ok && abs(actual - c.expect) <= 2;

        ++total;
        if (good) ++pass;

        int a = actual - 8; if (a < 0) a = 0;
        int b = actual + 8; if (b > (int)n.size()) b = (int)n.size();
        Log(L"[%s] ok=%d pos=%3d 期望=%4d 匹配度=%3.0f%%  %s%s",
            good ? L"PASS" : L"FAIL", (int)ok, actual, c.expect, score * 100.0f,
            c.stream ? L"[流式] " : L"", c.reco);
        Log(L"       定位处: ...%s...", n.substr(a, b - a).c_str());

        prevPos = actual;
    }

    Log(L"");
    Log(L"通过 %d / %d", pass, total);

    fclose(g_log);
    return 0;
}
