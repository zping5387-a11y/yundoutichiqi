// VoiceCommand.cpp
#include "VoiceCommand.h"
#include "ScriptDoc.h"

namespace
{
    struct CmdWord
    {
        const wchar_t* word;    // 归一化后的命令词（小写、无标点空白）
        VoiceCmd       cmd;
        const wchar_t* label;   // 界面提示用的名字
    };

    // 长词在前：避免「向下翻一页」被更短的词抢先命中
    const CmdWord kWords[] =
    {
        // ---- 向下翻一页 ----
        { L"向下翻一页", VoiceCmd::PageDown, L"向下翻一页" },
        { L"往下翻一页", VoiceCmd::PageDown, L"向下翻一页" },
        { L"向下翻页",   VoiceCmd::PageDown, L"向下翻一页" },
        { L"往下翻页",   VoiceCmd::PageDown, L"向下翻一页" },
        { L"下翻一页",   VoiceCmd::PageDown, L"向下翻一页" },
        { L"向下翻",     VoiceCmd::PageDown, L"向下翻一页" },
        { L"往下翻",     VoiceCmd::PageDown, L"向下翻一页" },
        { L"向下一页",   VoiceCmd::PageDown, L"向下翻一页" },
        { L"下一页",     VoiceCmd::PageDown, L"向下翻一页" },
        { L"下一屏",     VoiceCmd::PageDown, L"向下翻一页" },
        { L"下页",       VoiceCmd::PageDown, L"向下翻一页" },

        // ---- 向上翻一页 ----
        { L"向上翻一页", VoiceCmd::PageUp, L"向上翻一页" },
        { L"往上翻一页", VoiceCmd::PageUp, L"向上翻一页" },
        { L"向上翻页",   VoiceCmd::PageUp, L"向上翻一页" },
        { L"往上翻页",   VoiceCmd::PageUp, L"向上翻一页" },
        { L"上翻一页",   VoiceCmd::PageUp, L"向上翻一页" },
        { L"向上翻",     VoiceCmd::PageUp, L"向上翻一页" },
        { L"往上翻",     VoiceCmd::PageUp, L"向上翻一页" },
        { L"向上一页",   VoiceCmd::PageUp, L"向上翻一页" },
        { L"上一页",     VoiceCmd::PageUp, L"向上翻一页" },
        { L"上一屏",     VoiceCmd::PageUp, L"向上翻一页" },
        { L"上页",       VoiceCmd::PageUp, L"向上翻一页" },

        // ---- 回到开头 ----
        { L"回到最前面", VoiceCmd::ToTop, L"回到开头" },
        { L"回到最上面", VoiceCmd::ToTop, L"回到开头" },
        { L"回到顶部",   VoiceCmd::ToTop, L"回到开头" },
        { L"回到开头",   VoiceCmd::ToTop, L"回到开头" },
        { L"回到第一页", VoiceCmd::ToTop, L"回到开头" },
        { L"回到页首",   VoiceCmd::ToTop, L"回到开头" },
        { L"从头开始",   VoiceCmd::ToTop, L"回到开头" },
        { L"重头开始",   VoiceCmd::ToTop, L"回到开头" },

        // ---- 英文（语言切到英语时可用；归一化已去掉空格）----
        { L"nextpage",      VoiceCmd::PageDown, L"向下翻一页" },
        { L"pagedown",      VoiceCmd::PageDown, L"向下翻一页" },
        { L"previouspage",  VoiceCmd::PageUp,   L"向上翻一页" },
        { L"prevpage",      VoiceCmd::PageUp,   L"向上翻一页" },
        { L"pageup",        VoiceCmd::PageUp,   L"向上翻一页" },
        { L"backtotop",     VoiceCmd::ToTop,    L"回到开头" },
        { L"gototop",       VoiceCmd::ToTop,    L"回到开头" },
        { L"backtostart",   VoiceCmd::ToTop,    L"回到开头" },
    };

    constexpr int kMaxPrefixCn = 5;    // 中文命令词前最多允许的口语字数（“请翻到下一页”）
    constexpr int kMaxPrefixEn = 18;   // 英文：允许 "please turn to the next page"
    constexpr int kMaxUtter    = 32;   // 整句话最长字数，超过必然不是命令
}

VoiceCmd MatchVoiceCommand(const std::wstring& text, const wchar_t** label)
{
    if (label) *label = nullptr;

    const std::wstring s = CScriptDoc::NormalizeText(text);
    if (s.empty() || (int)s.size() > kMaxUtter) return VoiceCmd::None;

    for (const CmdWord& w : kWords)
    {
        const std::wstring kw(w.word);
        if (s.size() < kw.size()) continue;

        // 必须落在句尾
        if (s.compare(s.size() - kw.size(), kw.size(), kw) != 0) continue;

        const int prefix = (int)(s.size() - kw.size());
        const bool ascii = (kw[0] < 0x80);
        if (prefix > (ascii ? kMaxPrefixEn : kMaxPrefixCn)) continue;

        if (label) *label = w.label;
        return w.cmd;
    }
    return VoiceCmd::None;
}
