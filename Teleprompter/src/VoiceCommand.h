// VoiceCommand.h - 语音命令：说一句话就能翻页，不用碰键盘
//
// 设计要点：
//  1. 只认「整句话的结尾」是命令词。老师正常说「下一页」时它就是句尾；
//     而正文里出现的「下一页的内容我们下节课讲」不会被误当成命令。
//  2. 命令词前面允许少量口语前缀（“请翻到下一页”“帮我往下翻一页”）。
//  3. 纯文本匹配，不依赖具体识别引擎（离线 sherpa / 系统 SAPI 都适用）。
#pragma once

#include "framework.h"

enum class VoiceCmd
{
    None = 0,
    PageDown,   // 向下翻一页
    PageUp,     // 向上翻一页
    ToTop,      // 回到开头
};

// text：识别结果原文（可带标点、大小写混杂，内部会归一化）。
// 命中时 label 返回该命令的标准中文名（例如 L"向下翻一页"），供界面提示用。
VoiceCmd MatchVoiceCommand(const std::wstring& text, const wchar_t** label);
