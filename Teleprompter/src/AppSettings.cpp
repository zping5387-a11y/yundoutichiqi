// AppSettings.cpp
#include "AppSettings.h"
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma warning(disable: 4996)

// 类内初始化的 static const 成员若被 odr-use（比如传给 std::min）就需要一个定义
const size_t AppSettings::kMaxRecent;

namespace
{
    std::wstring ExeDir()
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        size_t pos = p.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? std::wstring(L".") : p.substr(0, pos);
    }

    COLORREF ParseColor(const wchar_t* s, COLORREF def)
    {
        int r = 0, g = 0, b = 0;
        if (swscanf_s(s, L"%d,%d,%d", &r, &g, &b) == 3)
            return RGB(r, g, b);
        return def;
    }

    std::wstring FormatColor(COLORREF c)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
        return buf;
    }

    // 路径大小写不敏感去重（Windows 习惯）
    bool SamePath(const std::wstring& a, const std::wstring& b)
    {
        return a.size() == b.size() && ::_wcsicmp(a.c_str(), b.c_str()) == 0;
    }

    void MoveToFront(std::vector<std::wstring>& list, const std::wstring& path, size_t maxCount)
    {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const std::wstring& p) { return SamePath(p, path); }),
                   list.end());
        list.insert(list.begin(), path);
        if (list.size() > maxCount) list.resize(maxCount);
    }

    void AppendUnique(std::vector<std::wstring>& list, const std::wstring& path)
    {
        for (const auto& p : list)
            if (SamePath(p, path)) return;
        list.push_back(path);
    }
}

std::wstring AppSettings::SettingsPath()
{
    std::wstring dir = ExeDir();
    std::wstring ini = dir + L"\\TeacherPrompter.ini";

    // 安装到 Program Files 等只读目录时退回 %APPDATA%
    HANDLE h = ::CreateFileW(ini.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(h);
        return ini;
    }

    wchar_t appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
    {
        std::wstring d = std::wstring(appdata) + L"\\TeacherPrompter";
        ::SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
        return d + L"\\settings.ini";
    }
    return ini;
}

std::wstring AppSettings::RecentStorePath()
{
    // 「最近文稿 / 上次稿件」是程序运行时产生的状态，单独放 sidecar，
    // 不写进主 INI —— 主 INI 的 [file] 段属于用户手写的稿件定义，只读。
    wchar_t env[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_DATA_DIR", env, MAX_PATH) > 0 && env[0])
    {
        const std::wstring dir = env;
        ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        return dir + L"\\recent.ini";
    }

    wchar_t appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
    {
        const std::wstring dir = std::wstring(appdata) + L"\\TeacherPrompter";
        ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        return dir + L"\\recent.ini";
    }
    return ExeDir() + L"\\recent.ini";
}

void AppSettings::Load()
{
    const std::wstring ini = SettingsPath();
    const wchar_t* f = ini.c_str();
    wchar_t buf[512] = { 0 };

    fontSize    = ::GetPrivateProfileIntW(L"view", L"fontSize", fontSize, f);
    lineSpacing = ::GetPrivateProfileIntW(L"view", L"lineSpacing", lineSpacing, f);
    bgAlpha     = ::GetPrivateProfileIntW(L"view", L"bgAlpha", bgAlpha, f);
    autoSpeed   = ::GetPrivateProfileIntW(L"view", L"autoSpeed", autoSpeed, f);
    langId      = ::GetPrivateProfileIntW(L"speech", L"langId", langId, f);
    asrEngine   = ::GetPrivateProfileIntW(L"speech", L"asrEngine", asrEngine, f);
    smartFollow = ::GetPrivateProfileIntW(L"speech", L"smartFollow", smartFollow ? 1 : 0, f) != 0;

    auto boolOf = [&](const wchar_t* key, bool def) -> bool
    {
        return ::GetPrivateProfileIntW(L"view", key, def ? 1 : 0, f) != 0;
    };
    highlightCur = boolOf(L"highlightCur", highlightCur);
    shadow       = boolOf(L"shadow", shadow);
    mirror       = boolOf(L"mirror", mirror);
    autoScroll   = boolOf(L"autoScroll", autoScroll);
    visible      = boolOf(L"visible", visible);
    locked       = boolOf(L"locked", locked);

    ::GetPrivateProfileStringW(L"view", L"textColor", L"", buf, 512, f);
    if (buf[0]) textColor = ParseColor(buf, textColor);
    ::GetPrivateProfileStringW(L"view", L"hiColor", L"", buf, 512, f);
    if (buf[0]) hiColor = ParseColor(buf, hiColor);
    ::GetPrivateProfileStringW(L"view", L"bgColor", L"", buf, 512, f);
    if (buf[0]) bgColor = ParseColor(buf, bgColor);
    clickThrough = ::GetPrivateProfileIntW(L"view", L"clickThrough", clickThrough ? 1 : 0, f) != 0;

    ::GetPrivateProfileStringW(L"window", L"rect", L"", buf, 512, f);
    if (buf[0])
    {
        int l = 0, t = 0, r = 0, b = 0;
        if (swscanf_s(buf, L"%d,%d,%d,%d", &l, &t, &r, &b) == 4)
        {
            overlayRect = { l, t, r, b };
        }
    }

    // 主面板位置与尺寸（左上角 + 窗口宽高，像素）
    ::GetPrivateProfileStringW(L"window", L"panelPos", L"", buf, 512, f);
    if (buf[0])
    {
        int x = 0, y = 0;
        if (swscanf_s(buf, L"%d,%d", &x, &y) == 2)
        {
            panelPos = { x, y };
            panelPosValid = true;
        }
    }

    ::GetPrivateProfileStringW(L"window", L"panelSize", L"", buf, 512, f);
    if (buf[0])
    {
        int w = 0, h = 0;
        if (swscanf_s(buf, L"%d,%d", &w, &h) == 2 && w > 0 && h > 0)
        {
            panelSize = { w, h };
            panelSizeValid = true;
        }
    }

    ::GetPrivateProfileStringW(L"speech", L"inputDevice", L"", buf, 512, f);
    inputDeviceId = buf;

    // 首次隐藏到托盘的提示只弹一次，记在 [window] 段
    trayHintShown = ::GetPrivateProfileIntW(L"window", L"trayHint", trayHintShown ? 1 : 0, f) != 0;

    // 关于页的作者邮箱：没写就用内置默认值
    ::GetPrivateProfileStringW(L"about", L"email", L"", buf, 512, f);
    if (buf[0]) authorEmail = buf;

    // ---- 稿件：[file] 段是用户手写的定义，这里只读取 ----
    ::GetPrivateProfileStringW(L"file", L"lastScript", L"", buf, 512, f);
    const std::wstring iniLastScript = buf;

    m_presetRecent.clear();
    for (size_t i = 0; i < kMaxRecent; ++i)
    {
        const std::wstring key = L"recent" + std::to_wstring(i);
        wchar_t p[MAX_PATH * 2] = { 0 };
        if (::GetPrivateProfileStringW(L"file", key.c_str(), L"", p, MAX_PATH * 2, f) <= 0) break;
        m_presetRecent.push_back(p);
    }

    // ---- 程序自己的文稿记录（sidecar）：recent0 最新 … recent7 最旧 ----
    const std::wstring store = RecentStorePath();
    const wchar_t* sf = store.c_str();
    m_progRecent.clear();
    for (size_t i = 0; i < kMaxRecent; ++i)
    {
        const std::wstring key = L"recent" + std::to_wstring(i);
        wchar_t p[MAX_PATH * 2] = { 0 };
        if (::GetPrivateProfileStringW(L"file", key.c_str(), L"", p, MAX_PATH * 2, sf) <= 0) break;
        m_progRecent.push_back(p);
    }
    ::GetPrivateProfileStringW(L"file", L"lastScript", L"", buf, 512, sf);
    const std::wstring storeLastScript = buf;

    // 启动用哪篇：INI 里写了就用 INI 的（用户的定义优先），否则用上次打开的那篇
    lastScriptPath = iniLastScript.empty() ? storeLastScript : iniLastScript;
    RebuildRecent();

    if (fontSize < 16) fontSize = 16;
    if (fontSize > 160) fontSize = 160;
    if (lineSpacing < 100) lineSpacing = 100;
    if (lineSpacing > 320) lineSpacing = 320;
    if (bgAlpha < 0) bgAlpha = 0;
    if (bgAlpha > 255) bgAlpha = 255;
    if (autoSpeed < 1) autoSpeed = 1;
    if (autoSpeed > 10) autoSpeed = 10;
}

void AppSettings::Save() const
{
    const std::wstring ini = SettingsPath();
    const wchar_t* f = ini.c_str();
    wchar_t buf[64];

    ::WritePrivateProfileStringW(L"view", L"fontSize", std::to_wstring(fontSize).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"lineSpacing", std::to_wstring(lineSpacing).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"bgAlpha", std::to_wstring(bgAlpha).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"autoSpeed", std::to_wstring(autoSpeed).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"highlightCur", highlightCur ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"shadow", shadow ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"mirror", mirror ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"autoScroll", autoScroll ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"visible", visible ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"locked", locked ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"clickThrough", clickThrough ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"view", L"textColor", FormatColor(textColor).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"hiColor", FormatColor(hiColor).c_str(), f);
    ::WritePrivateProfileStringW(L"view", L"bgColor", FormatColor(bgColor).c_str(), f);
    ::WritePrivateProfileStringW(L"speech", L"langId", std::to_wstring(langId).c_str(), f);
    ::WritePrivateProfileStringW(L"speech", L"asrEngine", std::to_wstring(asrEngine).c_str(), f);
    ::WritePrivateProfileStringW(L"speech", L"smartFollow", smartFollow ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"speech", L"inputDevice", inputDeviceId.c_str(), f);

    swprintf_s(buf, L"%d,%d,%d,%d", overlayRect.left, overlayRect.top, overlayRect.right, overlayRect.bottom);
    ::WritePrivateProfileStringW(L"window", L"rect", buf, f);

    if (panelPosValid)
    {
        swprintf_s(buf, L"%d,%d", panelPos.x, panelPos.y);
        ::WritePrivateProfileStringW(L"window", L"panelPos", buf, f);
    }
    if (panelSizeValid && panelSize.cx > 0 && panelSize.cy > 0)
    {
        swprintf_s(buf, L"%d,%d", (int)panelSize.cx, (int)panelSize.cy);
        ::WritePrivateProfileStringW(L"window", L"panelSize", buf, f);
    }
    ::WritePrivateProfileStringW(L"window", L"trayHint", trayHintShown ? L"1" : L"0", f);
    ::WritePrivateProfileStringW(L"about", L"email", authorEmail.c_str(), f);

    // [file] 段（稿件定义）不在这里写：那是用户手写、程序只读的内容。
    // 上次稿件与最近文稿改存 sidecar，见 SaveRecentStore()。
    SaveRecentStore();
}

void AppSettings::SaveRecentStore() const
{
    const std::wstring store = RecentStorePath();
    const wchar_t* sf = store.c_str();

    ::WritePrivateProfileStringW(L"file", L"lastScript", lastScriptPath.c_str(), sf);

    // 先清掉旧的一整组键，再按顺序写，避免数量变少后残留旧路径
    for (size_t i = 0; i < kMaxRecent; ++i)
    {
        const std::wstring key = L"recent" + std::to_wstring(i);
        ::WritePrivateProfileStringW(L"file", key.c_str(), nullptr, sf);
    }
    for (size_t i = 0; i < m_progRecent.size() && i < kMaxRecent; ++i)
    {
        const std::wstring key = L"recent" + std::to_wstring(i);
        ::WritePrivateProfileStringW(L"file", key.c_str(), m_progRecent[i].c_str(), sf);
    }
}

// 显示用列表 = 程序打开过的（最近在前）+ INI 里的预设清单（补在后、去重）
void AppSettings::RebuildRecent()
{
    recentScripts = m_progRecent;
    for (const auto& p : m_presetRecent)
    {
        if (recentScripts.size() >= kMaxRecent) break;
        AppendUnique(recentScripts, p);
    }
    if (recentScripts.size() > kMaxRecent) recentScripts.resize(kMaxRecent);
}

void AppSettings::AddRecentScript(const std::wstring& path)
{
    if (path.empty()) return;

    MoveToFront(m_progRecent, path, kMaxRecent);
    RebuildRecent();
}

bool AppSettings::IsPresetRecent(const std::wstring& path) const
{
    for (const auto& p : m_presetRecent)
        if (SamePath(p, path)) return true;
    return false;
}

void AppSettings::RemoveRecentScript(const std::wstring& path)
{
    m_progRecent.erase(std::remove_if(m_progRecent.begin(), m_progRecent.end(),
                                      [&](const std::wstring& p) { return SamePath(p, path); }),
                       m_progRecent.end());
    RebuildRecent();
    SaveRecentStore();
}

void AppSettings::HidePresetRecent(const std::wstring& path)
{
    m_presetRecent.erase(std::remove_if(m_presetRecent.begin(), m_presetRecent.end(),
                                        [&](const std::wstring& p) { return SamePath(p, path); }),
                         m_presetRecent.end());
    RebuildRecent();
}
