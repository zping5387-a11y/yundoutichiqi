// ScriptDoc.h - 文稿加载、归一化与排版
#pragma once

#include "framework.h"

struct PromptLine
{
    std::wstring text;      // 该行显示的文本
    int          origStart; // 在原文中的起始字符索引
    int          origEnd;   // 在原文中的结束字符索引（不含）
};

class CScriptDoc
{
public:
    bool LoadFile(const std::wstring& path, std::wstring& err);
    void SetText(const std::wstring& text);

    const std::wstring& Raw() const { return m_raw; }
    bool Empty() const { return m_raw.empty(); }
    int  CharCount() const { return (int)m_raw.size(); }

    // 归一化文本（去标点/空白，小写）用于语音匹配
    const std::wstring& Normalized() const { return m_norm; }
    int NormToOrig(int normIndex) const;
    int OrigToNorm(int origIndex) const;

    // 排版（需要 GDI+ Graphics 用于文字测量）
    void Layout(Gdiplus::Graphics* g, int fontSize, int lineSpacingPct, int widthPx);
    const std::vector<PromptLine>& Lines() const { return m_lines; }
    int  LineHeight() const { return m_lineH; }
    int  TotalHeight() const { return m_lineH * (int)m_lines.size(); }
    int  LineIndexOfChar(int origChar) const;
    int  CharIndexOfLine(int line) const;

    static std::wstring NormalizeText(const std::wstring& s);

    // 取文件开头 maxChars 个字符做列表预览（压缩空白为单空格，截断补「…」；
    // docx/pptx 解包正文，txt 按 BOM/编码解码；失败返回空串）
    static std::wstring PreviewFile(const std::wstring& path, int maxChars);

private:
    void BuildNormalized();
    void BuildParagraphs();

    std::wstring m_raw;
    std::wstring m_norm;
    std::vector<int> m_normToOrig;
    std::vector<int> m_origToNorm;
    std::vector<std::pair<std::wstring, int>> m_paragraphs; // 段落文本 + 原文起始索引
    std::vector<PromptLine> m_lines;
    int m_lineH = 40;
    int m_layoutW = -1;
    int m_layoutFont = -1;
    int m_layoutSpacing = -1;
};
