// OfficeDoc.h - Word（.docx）/ PowerPoint（.pptx）稿件读取
//
// 直接解析 Office Open XML 包（本质是 zip），从 word/document.xml 与
// ppt/slides/slideN.xml 里抽取文字，拼成纯文本给提词器用。
// 不依赖本机是否装了 Office，也不会启动 Office；不修改原文件。
#pragma once

#include <windows.h>
#include <string>

namespace OfficeDoc
{
    // 扩展名是否为 .docx / .docm / .pptx / .pptm（老格式 .doc/.ppt 不算）
    bool IsOfficeFile(const std::wstring& path);

    // 旧版二进制格式（.doc / .dot / .ppt / .pps）：不支持，只用来给提示
    bool IsLegacyOfficeFile(const std::wstring& path);

    // 读取文稿纯文本。err 里是给用户看的中文原因。
    bool LoadText(const std::wstring& path, std::wstring& out, std::wstring& err);
}
