// ScriptDoc.cpp
#include "ScriptDoc.h"
#include "OfficeDoc.h"
#include <cstdio>
#include <cwctype>

using namespace Gdiplus;

namespace
{
    bool IsPunct(wchar_t c)
    {
        if (c < 0x80)
            return !(iswalnum(c) != 0 || c == L'\'' || c == L'_');
        if (c >= 0x3000 && c <= 0x303F) return true;   // CJK 标点
        if (c >= 0xFE30 && c <= 0xFE6F) return true;   // CJK 兼容形式
        if (c >= 0xFF01 && c <= 0xFF65) return true;   // 全角标点
        if (c >= 0x2010 && c <= 0x2027) return true;   // 破折号/引号/省略号
        if (c == 0x00B7 || c == 0x30FB) return true;
        return false;
    }

    // 把整块文件字节按 BOM / UTF-8 / ANSI 解成宽字符（LoadFile 与 PreviewFile 共用）
    std::wstring DecodeTxtBuffer(const std::vector<char>& buf)
    {
        std::wstring text;
        const unsigned char* p = (const unsigned char*)buf.data();
        size_t n = buf.size();

        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)          // UTF-16 LE
        {
            text.assign((const wchar_t*)(p + 2), (n - 2) / sizeof(wchar_t));
        }
        else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)     // UTF-16 BE
        {
            size_t cnt = (n - 2) / 2;
            text.resize(cnt);
            for (size_t i = 0; i < cnt; ++i)
                text[i] = (wchar_t)((p[2 + i * 2] << 8) | p[3 + i * 2]);
        }
        else
        {
            size_t off = 0;
            if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) off = 3;

            const char* src = (const char*)p + off;
            int srcLen = (int)(n - off);

            int wlen = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
            UINT cp = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;
            if (wlen <= 0)
            {
                cp = CP_ACP;
                flags = 0;
                wlen = ::MultiByteToWideChar(cp, flags, src, srcLen, nullptr, 0);
            }
            if (wlen <= 0) return L"";

            text.resize((size_t)wlen);
            ::MultiByteToWideChar(cp, flags, src, srcLen, &text[0], wlen);
        }
        return text;
    }

    float MeasureWidth(Graphics& g, Font& font, const std::wstring& s)
    {
        if (s.empty()) return 0.0f;
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        RectF layoutRect(0, 0, 100000.0f, 4000.0f);
        RectF bound;
        g.MeasureString(s.c_str(), (INT)s.size(), &font, layoutRect, &fmt, &bound);
        return bound.Width;
    }
}

std::wstring CScriptDoc::NormalizeText(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s)
    {
        if (iswspace(c)) continue;
        if (IsPunct(c)) continue;
        out.push_back((wchar_t)towlower(c));
    }
    return out;
}

bool CScriptDoc::LoadFile(const std::wstring& path, std::wstring& err)
{
    // Word / PPT：解包取正文，当普通文稿用（后续归一化、匹配、排版完全一致）
    if (OfficeDoc::IsOfficeFile(path))
    {
        std::wstring text;
        if (!OfficeDoc::LoadText(path, text, err)) return false;
        SetText(text);
        return true;
    }
    if (OfficeDoc::IsLegacyOfficeFile(path))
    {
        err = L"暂不支持旧版 .doc / .ppt。\n请在 Office / WPS 里「另存为」.docx 或 .pptx 后再打开。";
        return false;
    }

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
    {
        err = L"无法打开文件：" + path;
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(fp);
        err = L"文件为空";
        return false;
    }

    std::vector<char> buf((size_t)size);
    size_t read = fread(buf.data(), 1, buf.size(), fp);
    fclose(fp);
    buf.resize(read);

    std::wstring text = DecodeTxtBuffer(buf);
    if (text.empty())
    {
        err = L"文件编码无法识别（请另存为 UTF-8）";
        return false;
    }

    SetText(text);
    return true;
}

// 取文件开头 maxChars 个字符做列表预览：压缩空白为单空格，截断补「…」
std::wstring CScriptDoc::PreviewFile(const std::wstring& path, int maxChars)
{
    if (maxChars <= 0) return L"";

    std::wstring text;
    if (OfficeDoc::IsOfficeFile(path))
    {
        std::wstring err;
        if (!OfficeDoc::LoadText(path, text, err)) return L"";
    }
    else if (OfficeDoc::IsLegacyOfficeFile(path))
    {
        return L"";                     // 旧版 doc/ppt 不支持，不显示预览
    }
    else
    {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return L"";
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size <= 0) { fclose(fp); return L""; }
        std::vector<char> buf((size_t)size);
        size_t rd = fread(buf.data(), 1, buf.size(), fp);
        fclose(fp);
        buf.resize(rd);
        text = DecodeTxtBuffer(buf);
    }

    // 压缩空白（换行/连续空格 → 单空格），截取前 maxChars 个字符
    std::wstring out;
    out.reserve(min((size_t)maxChars, text.size()));
    bool prevSpace = true;              // 顺手去掉开头空白
    size_t i = 0;
    for (; i < text.size() && (int)out.size() < maxChars; ++i)
    {
        const wchar_t c = text[i];
        if (iswspace(c))
        {
            if (!prevSpace) { out.push_back(L' '); prevSpace = true; }
            continue;
        }
        out.push_back(c);
        prevSpace = false;
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (i < text.size() && !out.empty()) out.push_back(L'…');
    return out;
}

void CScriptDoc::SetText(const std::wstring& text)
{
    m_raw = text;
    m_lines.clear();
    m_layoutW = m_layoutFont = m_layoutSpacing = -1;
    BuildParagraphs();
    BuildNormalized();
}

void CScriptDoc::BuildParagraphs()
{
    m_paragraphs.clear();
    std::wstring cur;
    int curStart = 0;

    for (int i = 0; i < (int)m_raw.size(); ++i)
    {
        wchar_t c = m_raw[i];
        if (c == L'\r')
        {
            m_paragraphs.emplace_back(cur, curStart);
            cur.clear();
            if (i + 1 < (int)m_raw.size() && m_raw[i + 1] == L'\n') ++i;
            curStart = i + 1;
        }
        else if (c == L'\n')
        {
            m_paragraphs.emplace_back(cur, curStart);
            cur.clear();
            curStart = i + 1;
        }
        else
        {
            cur.push_back(c);
        }
    }
    m_paragraphs.emplace_back(cur, curStart);
}

void CScriptDoc::BuildNormalized()
{
    m_norm.clear();
    m_normToOrig.clear();
    m_origToNorm.assign(m_raw.size() + 1, -1);

    for (int i = 0; i < (int)m_raw.size(); ++i)
    {
        wchar_t c = m_raw[i];
        if (iswspace(c) || IsPunct(c)) continue;
        m_origToNorm[i] = (int)m_norm.size();
        m_normToOrig.push_back(i);
        m_norm.push_back((wchar_t)towlower(c));
    }
    m_origToNorm[m_raw.size()] = (int)m_norm.size();
}

int CScriptDoc::NormToOrig(int normIndex) const
{
    if (m_normToOrig.empty()) return 0;
    if (normIndex <= 0) return m_normToOrig.front();
    if (normIndex >= (int)m_normToOrig.size()) return m_normToOrig.back();
    return m_normToOrig[normIndex];
}

int CScriptDoc::OrigToNorm(int origIndex) const
{
    if (m_origToNorm.empty()) return 0;
    if (origIndex <= 0) return 0;
    if (origIndex >= (int)m_origToNorm.size()) return (int)m_norm.size();
    for (int i = origIndex; i >= 0; --i)
        if (m_origToNorm[i] >= 0) return m_origToNorm[i];
    return 0;
}

void CScriptDoc::Layout(Graphics* g, int fontSize, int lineSpacingPct, int widthPx)
{
    if (widthPx < 40) widthPx = 40;
    if (fontSize < 8) fontSize = 8;

    if (!m_lines.empty() && m_layoutW == widthPx && m_layoutFont == fontSize &&
        m_layoutSpacing == lineSpacingPct)
        return;

    m_lines.clear();
    m_lineH = (int)std::lround(fontSize * lineSpacingPct / 100.0f);
    if (m_lineH < fontSize) m_lineH = fontSize;

    const wchar_t* famName = L"Microsoft YaHei UI";
    {
        FontFamily probe(famName);
        if (!probe.IsAvailable())
        {
            FontFamily alt(L"Microsoft YaHei");
            if (alt.IsAvailable()) famName = L"Microsoft YaHei";
        }
    }
    FontFamily fam(famName);
    Font font(&fam, (REAL)fontSize, FontStyleRegular, UnitPixel);
    const float maxW = (float)widthPx;

    for (auto& para : m_paragraphs)
    {
        const std::wstring& text = para.first;
        const int paraStart = para.second;

        if (text.empty())
        {
            m_lines.push_back({ L"", paraStart, paraStart });
            continue;
        }

        int start = 0;
        const int n = (int)text.size();
        while (start < n)
        {
            while (start < n && text[start] == L' ') ++start;   // 行首不吃空格
            if (start >= n) break;

            std::wstring line;
            int i = start;
            while (i < n)
            {
                std::wstring cand = line;
                cand.push_back(text[i]);
                float cw = MeasureWidth(*g, font, cand);
                if (!line.empty() && cw > maxW) break;
                line.swap(cand);
                ++i;
            }
            if (i == start) i = start + 1;   // 单字超宽兜底

            int brk = i;
            // 西文尽量在空格处断行
            if (brk < n && (iswalnum(text[brk]) || text[brk] == L'\'') && text[brk] < 0x80)
            {
                size_t sp = line.find_last_of(L' ');
                if (sp != std::wstring::npos && sp > 0)
                {
                    brk = start + (int)sp + 1;
                    line = text.substr(start, brk - start);
                }
            }

            // 去掉行尾空格
            while (!line.empty() && line.back() == L' ') line.pop_back();

            m_lines.push_back({ line, paraStart + start, paraStart + brk });
            start = brk;
        }
    }

    if (m_lines.empty()) m_lines.push_back({ L"", 0, 0 });

    m_layoutW = widthPx;
    m_layoutFont = fontSize;
    m_layoutSpacing = lineSpacingPct;
}

int CScriptDoc::LineIndexOfChar(int origChar) const
{
    if (m_lines.empty()) return 0;
    int lo = 0, hi = (int)m_lines.size() - 1, ans = 0;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (m_lines[mid].origStart <= origChar)
        {
            ans = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return ans;
}

int CScriptDoc::CharIndexOfLine(int line) const
{
    if (m_lines.empty()) return 0;
    if (line < 0) line = 0;
    if (line >= (int)m_lines.size()) line = (int)m_lines.size() - 1;
    return m_lines[line].origStart;
}
