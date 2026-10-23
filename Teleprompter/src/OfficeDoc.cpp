// OfficeDoc.cpp - 从 .docx / .pptx 里抽文字（纯解包，不依赖 Office）
//
// 原理：Office Open XML 文档就是一个 zip 包。
//   Word  ：正文在 word/document.xml，文字在 <w:t>，段落以 <w:p> 分隔（表格单元格 <w:tc>）
//   PPT   ：每页在 ppt/slides/slideN.xml，文字在 <a:t>，段落以 <a:p> 分隔
// 所以思路是：用 miniz 解压出这几个 xml，再按标签扫一遍取文本节点，
// 段落/换行/制表符映射成 \n、\t，最后统一整理空行。
#include "OfficeDoc.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>
#include "miniz.h"

namespace
{
    // ---------------------------------------------------------------- 编码

    std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty()) return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                           nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string out((size_t)n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n <= 0) return std::wstring();
        std::wstring out((size_t)n, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
        return out;
    }

    void AppendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800)
        {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    // XML 文本节点里的实体（&amp; &#20013; 之类）解码后追加
    void AppendDecoded(std::string& out, const std::string& s)
    {
        for (size_t i = 0; i < s.size();)
        {
            if (s[i] != '&')
            {
                out.push_back(s[i++]);
                continue;
            }
            const size_t semi = s.find(';', i);
            if (semi == std::string::npos || semi - i > 10)
            {
                out.push_back(s[i++]);
                continue;
            }
            const std::string ent = s.substr(i + 1, semi - i - 1);
            unsigned cp = 0;
            bool ok = true;
            if (ent == "amp")       cp = '&';
            else if (ent == "lt")   cp = '<';
            else if (ent == "gt")   cp = '>';
            else if (ent == "quot") cp = '"';
            else if (ent == "apos") cp = '\'';
            else if (ent.size() > 1 && ent[0] == '#')
            {
                const bool hex = (ent[1] == 'x' || ent[1] == 'X');
                cp = (unsigned)strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            }
            else ok = false;

            if (!ok)
            {
                out.push_back(s[i++]);
                continue;
            }
            AppendUtf8(out, cp);
            i = semi + 1;
        }
    }

    // ---------------------------------------------------------------- XML → 纯文本

    // "w:t xml:space=\"preserve\"" / "/w:p" / "w:br/" 都取出 "w:t" / "w:p" / "w:br"
    std::string TagName(const std::string& tag)
    {
        size_t b = 0;
        while (b < tag.size() && (tag[b] == '/' || isspace((unsigned char)tag[b]))) ++b;
        size_t e = b;
        while (e < tag.size() && !isspace((unsigned char)tag[e]) && tag[e] != '/') ++e;
        return tag.substr(b, e - b);
    }

    std::string XmlToText(const std::string& xml)
    {
        std::string out;
        out.reserve(xml.size() / 3);

        // mc:AlternateContent 的 mc:Fallback 是给老版本 Office 的同一段内容的副本，
        // 两份都读会重复，这里整段跳过 Fallback
        int fallbackDepth = 0;

        size_t i = 0;
        const size_t n = xml.size();
        while (i < n)
        {
            const size_t lt = xml.find('<', i);
            if (lt == std::string::npos) break;
            const size_t gt = xml.find('>', lt);
            if (gt == std::string::npos) break;

            const std::string raw = xml.substr(lt + 1, gt - lt - 1);
            const bool closing = (!raw.empty() && raw[0] == '/');
            const bool selfClose = (!raw.empty() && raw.back() == '/');
            const std::string name = TagName(raw);
            i = gt + 1;

            if (name == "mc:Fallback")
            {
                if (closing) { if (fallbackDepth > 0) --fallbackDepth; }
                else if (!selfClose) ++fallbackDepth;
                continue;
            }
            if (fallbackDepth > 0) continue;

            if (!closing && (name == "w:t" || name == "a:t"))
            {
                size_t end = xml.find('<', i);      // 文本节点到下一个标签为止
                if (end == std::string::npos) end = n;
                AppendDecoded(out, xml.substr(i, end - i));
                i = end;
                continue;
            }
            if (closing && (name == "w:p" || name == "a:p"))
            {
                if (!out.empty() && out.back() != '\n') out.push_back('\n');
                continue;
            }
            if (closing && name == "w:tc")           // 表格单元格之间加个制表位
            {
                if (!out.empty() && out.back() != '\n' && out.back() != '\t') out.push_back('\t');
                continue;
            }
            if (!closing && (name == "w:br" || name == "w:cr" || name == "a:br"))
            {
                out.push_back('\n');                 // 手动换行 / 分页
                continue;
            }
            if (!closing && (name == "w:tab" || name == "a:tab"))
            {
                out.push_back('\t');
                continue;
            }
        }
        return out;
    }

    // 统一换行、去掉行尾空白、压缩多余空行
    void Polish(std::string& s)
    {
        std::string uni;
        uni.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\r')
            {
                if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
                uni.push_back('\n');
            }
            else uni.push_back(s[i]);
        }

        std::string out;
        out.reserve(uni.size());
        int blanks = 0;
        for (size_t i = 0; i < uni.size();)
        {
            size_t e = uni.find('\n', i);
            if (e == std::string::npos) e = uni.size();
            std::string line = uni.substr(i, e - i);

            size_t b = 0;
            while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
            size_t f = line.size();
            while (f > b && (line[f - 1] == ' ' || line[f - 1] == '\t')) --f;
            line = line.substr(b, f - b);

            if (line.empty())
            {
                if (!out.empty() && blanks < 1) out.push_back('\n');
                ++blanks;
            }
            else
            {
                out += line;
                out.push_back('\n');
                blanks = 0;
            }
            i = e + 1;
        }
        while (!out.empty() && out.back() == '\n') out.pop_back();
        s.swap(out);
    }

    // ---------------------------------------------------------------- zip 包

    bool ReadEntry(mz_zip_archive& zip, const char* name, std::string& out)
    {
        size_t sz = 0;
        void* p = mz_zip_reader_extract_file_to_heap(&zip, name, &sz, 0);
        if (!p) return false;
        if (sz > 0) out.assign((const char*)p, sz);
        mz_free(p);
        return sz > 0;
    }

    bool OpenPackage(const std::wstring& path, mz_zip_archive& zip, FILE*& fp, std::wstring& err)
    {
        fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
        {
            err = L"无法打开文件：" + path;
            return false;
        }
        memset(&zip, 0, sizeof(zip));
        if (!mz_zip_reader_init_cfile(&zip, fp, 0, 0))
        {
            fclose(fp);
            fp = nullptr;
            err = L"这不是可读的 .docx / .pptx 包：文件可能设置了打开密码、已损坏，"
                  L"或者其实是旧版格式。\n请在 Office / WPS 里另存为 .docx / .pptx 后再试。";
            return false;
        }
        return true;
    }

    void ClosePackage(mz_zip_archive& zip, FILE* fp)
    {
        mz_zip_reader_end(&zip);
        if (fp) fclose(fp);
    }

    bool LoadWordText(mz_zip_archive& zip, std::string& text, std::wstring& err)
    {
        std::string xml;
        if (!ReadEntry(zip, "word/document.xml", xml))
        {
            err = L"这个 Word 文档里没有正文（缺少 word/document.xml），可能不是标准 .docx。";
            return false;
        }
        text = XmlToText(xml);
        return true;
    }

    bool LoadPptText(mz_zip_archive& zip, std::string& text, std::wstring& err)
    {
        struct Slide
        {
            int         num;
            std::string name;
        };
        std::vector<Slide> slides;

        const std::string prefix = "ppt/slides/slide";
        const std::string suffix = ".xml";
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        for (mz_uint k = 0; k < count; ++k)
        {
            mz_zip_archive_file_stat st = {};
            if (!mz_zip_reader_file_stat(&zip, k, &st)) continue;

            const std::string name = st.m_filename;
            if (name.size() <= prefix.size() + suffix.size()) continue;
            if (name.compare(0, prefix.size(), prefix) != 0) continue;
            if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;

            const std::string digits = name.substr(prefix.size(),
                                                   name.size() - prefix.size() - suffix.size());
            if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) continue;
            slides.push_back({ atoi(digits.c_str()), name });
        }

        if (slides.empty())
        {
            err = L"这个 PPT 里没找到幻灯片（ppt/slides 为空），可能不是标准 .pptx。";
            return false;
        }

        // 幻灯片按页码排，不按 zip 里的字符串顺序（否则 slide10 会排到 slide2 前面）
        std::sort(slides.begin(), slides.end(),
                  [](const Slide& a, const Slide& b) { return a.num < b.num; });

        for (const auto& s : slides)
        {
            std::string xml;
            if (!ReadEntry(zip, s.name.c_str(), xml)) continue;
            text += XmlToText(xml);
            text += "\n\n";        // 页与页之间空一行
        }
        return true;
    }
}

namespace OfficeDoc
{
    bool IsOfficeFile(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos) return false;

        std::wstring ext = path.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext == L".docx" || ext == L".docm" || ext == L".pptx" || ext == L".pptm";
    }

    bool IsLegacyOfficeFile(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos) return false;

        std::wstring ext = path.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext == L".doc" || ext == L".dot" || ext == L".ppt" || ext == L".pps";
    }

    bool LoadText(const std::wstring& path, std::wstring& out, std::wstring& err)
    {
        out.clear();
        err.clear();

        const std::wstring ext = [&]
        {
            const size_t dot = path.find_last_of(L'.');
            std::wstring e = (dot == std::wstring::npos) ? std::wstring() : path.substr(dot);
            for (auto& c : e) c = (wchar_t)towlower(c);
            return e;
        }();
        const bool ppt = (ext == L".pptx" || ext == L".pptm");

        mz_zip_archive zip;
        FILE* fp = nullptr;
        if (!OpenPackage(path, zip, fp, err)) return false;

        std::string text;
        bool ok = false;
        if (ppt) ok = LoadPptText(zip, text, err);
        else     ok = LoadWordText(zip, text, err);

        ClosePackage(zip, fp);
        if (!ok) return false;

        Polish(text);
        out = Utf8ToWide(text);
        if (out.empty())
        {
            err = L"文档里没有可提词的文字（可能整页都是图片 / 扫描件）。";
            return false;
        }
        return true;
    }
}
