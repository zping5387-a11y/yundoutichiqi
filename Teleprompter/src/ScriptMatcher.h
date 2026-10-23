// ScriptMatcher.h - 把识别到的语音片段精确匹配到文稿位置（严格子串查找）
#pragma once

#include "framework.h"
#include "ScriptDoc.h"

class CScriptMatcher
{
public:
    void Reset(const std::wstring* norm)
    {
        m_norm = norm;
        m_pos = 0;
        m_failStreak = 0;
    }

    int  Current() const { return m_pos; }
    void SetPos(int p) { m_pos = p; }

    // recognized: 本次识别文本。返回 true 表示定位成功，newPos 为归一化文本中的新位置
    bool Feed(const std::wstring& recognized, int& newPos, float& score);

private:
    const std::wstring* m_norm = nullptr;
    int m_pos = 0;
    int m_failStreak = 0;
};
