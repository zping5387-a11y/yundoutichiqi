// ModelInstaller.cpp - 离线语音模型下载 / 解压 / 安装
#include "ModelInstaller.h"
#include "SherpaEngine.h"

#include <winhttp.h>
#include <shlobj.h>
#include <cstdarg>
#include <cwctype>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr wchar_t kOfficialUrl[] =
        L"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
        L"sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30.tar.bz2";

    // ---- 首选通道：按文件从 HF 镜像取 ----
    // GitHub 上只有一个 570MB 的 tar.bz2，国内下到一半被掐断就整包作废，还要再赌一次 bz2 解压。
    // 逐文件下载没有这些问题：断了只重传当前文件，也不用 tar。
    struct ModelFile
    {
        const wchar_t* name;
        ULONGLONG      bytes;   // 固定版本；实际大小不符说明源不对（或拿到的是错误页）
    };

    // 小文件先下：几秒内进度就能往前走，不会卡在 0% 干等 761MB 的 encoder
    constexpr ModelFile kModelFiles[] = {
        { L"tokens.txt",          18626 },
        { L"bpe.model",          263956 },
        { L"joiner.int8.onnx",  1545417 },
        { L"decoder.onnx",      8533022 },
        { L"encoder.int8.onnx", 761133737 },
    };

    constexpr wchar_t kMirrorHosts[][64] = {
        L"hf-mirror.com",      // 国内直连
        L"huggingface.co",     // 官方源，国外网络或已配代理时可用
    };

    // 自建镜像 / 测试用：TP_MODEL_MIRROR 指向一个目录地址，
    // 直接在后面拼文件名即可（例如内网文件服务器的 .../models 目录）
    std::wstring CustomMirror()
    {
        wchar_t env[2048] = { 0 };
        if (::GetEnvironmentVariableW(L"TP_MODEL_MIRROR", env, ARRAYSIZE(env)) <= 0 || !env[0])
            return std::wstring();
        std::wstring s = env;
        while (!s.empty() && s.back() == L'/') s.pop_back();
        return s;
    }

    std::wstring MirrorUrl(const wchar_t* host, const std::wstring& file)
    {
        const std::wstring base = CustomMirror();
        if (!base.empty()) return base + L"/" + file;
        return std::wstring(L"https://") + host + L"/csukuangfj/" +
               CModelInstaller::ModelName() + L"/resolve/main/" + file;
    }

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

    // 机器上到底有没有配代理（IE/系统设置或 PAC）。
    // AUTOMATIC_PROXY 会同时打开 WPAD 自动发现：没配代理的机器上经常要
    // 空转十几秒才超时（12002），国内直连快的场景反而更慢。
    bool HasSystemProxy()
    {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG cfg {};
        if (!::WinHttpGetIEProxyConfigForCurrentUser(&cfg)) return false;

        const bool has = (cfg.lpszProxy && *cfg.lpszProxy)
                      || (cfg.lpszAutoConfigUrl && *cfg.lpszAutoConfigUrl)
                      || cfg.fAutoDetect;
        if (cfg.lpszProxy)           ::GlobalFree(cfg.lpszProxy);
        if (cfg.lpszProxyBypass)     ::GlobalFree(cfg.lpszProxyBypass);
        if (cfg.lpszAutoConfigUrl)   ::GlobalFree(cfg.lpszAutoConfigUrl);
        return has;
    }

    // 显式代理：TP_MODEL_PROXY 优先，其次是标准的 HTTP(S)_PROXY。
    // 有些机器（学校机房、公司内网）只有代理能出网，而系统代理设置里什么都没写。
    std::wstring EnvProxy()
    {
        const wchar_t* keys[] = { L"TP_MODEL_PROXY", L"HTTPS_PROXY", L"https_proxy",
                                  L"HTTP_PROXY", L"http_proxy" };
        for (const wchar_t* k : keys)
        {
            wchar_t v[2048] = { 0 };
            if (::GetEnvironmentVariableW(k, v, ARRAYSIZE(v)) > 0 && v[0])
                return std::wstring(v);
        }
        return std::wstring();
    }

    // useProxy=true 走系统代理 / PAC，false 强制直连；proxyOverride 非空时用它做代理地址
    HINTERNET OpenSession(bool useProxy, const std::wstring& proxyOverride = std::wstring())
    {
        const wchar_t* ua = L"YunDouTeleprompter/1.1 (offline-asr-model)";
        HINTERNET h = ::WinHttpOpen(ua,
                                    useProxy ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
                                             : WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h)
            h = ::WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h && useProxy && !proxyOverride.empty())
        {
            std::vector<wchar_t> buf(proxyOverride.begin(), proxyOverride.end());
            buf.push_back(0);
            WINHTTP_PROXY_INFO pi {};
            pi.dwAccessType    = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            pi.lpszProxy       = buf.data();
            pi.lpszProxyBypass = nullptr;
            ::WinHttpSetOption(h, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
        }
        if (h) ::WinHttpSetTimeouts(h, 10000, 10000, 20000, 60000);
        return h;
    }

    std::wstring ExeDir()
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        const size_t pos = p.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? std::wstring(L".") : p.substr(0, pos);
    }

    std::wstring ParentDir(const std::wstring& p)
    {
        const size_t pos = p.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? std::wstring(L".") : p.substr(0, pos);
    }

    bool FileExists(const std::wstring& p)
    {
        const DWORD a = ::GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirExists(const std::wstring& p)
    {
        const DWORD a = ::GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    ULONGLONG FileSize(const std::wstring& p)
    {
        WIN32_FILE_ATTRIBUTE_DATA d {};
        if (!::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
        return (ULONGLONG(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    }

    // 目录能建、能写才算可写（装到 Program Files 时会被 UAC 拦下）
    bool DirWritable(const std::wstring& d)
    {
        if (!DirExists(d))
        {
            if (::SHCreateDirectoryExW(nullptr, d.c_str(), nullptr) != ERROR_SUCCESS && !DirExists(d))
                return false;
        }
        const std::wstring probe = d + L"\\_write_test.tmp";
        HANDLE h = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        ::CloseHandle(h);
        return true;
    }

    std::wstring ComputeInstallRoot()
    {
        const std::wstring exeModels = ExeDir() + L"\\models";
        if (DirWritable(exeModels)) return exeModels;

        wchar_t appdata[MAX_PATH] = { 0 };
        if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        {
            const std::wstring d = std::wstring(appdata) + L"\\TeacherPrompter\\models";
            if (DirWritable(d)) return d;
        }
        return exeModels;   // 都不行就报错在下载环节，路径提示仍指向 exe 同级
    }

    std::wstring SystemTarPath()
    {
        wchar_t sys[MAX_PATH] = { 0 };
        const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
        if (!n) return std::wstring();
        const std::wstring tar = std::wstring(sys, n) + L"\\tar.exe";
        return FileExists(tar) ? tar : std::wstring();
    }

    std::wstring FmtMB(ULONGLONG bytes)
    {
        wchar_t b[64] = { 0 };
        swprintf_s(b, L"%.0f MB", double(bytes) / (1024.0 * 1024.0));
        return b;
    }

    std::wstring FmtSpeed(double bytesPerSec)
    {
        wchar_t b[64] = { 0 };
        if (bytesPerSec >= 1024 * 1024)
            swprintf_s(b, L"%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
        else
            swprintf_s(b, L"%.0f KB/s", bytesPerSec / 1024.0);
        return b;
    }

    std::wstring FmtEta(ULONGLONG remainBytes, double bytesPerSec)
    {
        if (bytesPerSec < 1024) return std::wstring();
        const int sec = int(remainBytes / bytesPerSec);
        wchar_t b[64] = { 0 };
        if (sec >= 3600)      swprintf_s(b, L"约 %d 小时 %d 分", sec / 3600, (sec % 3600) / 60);
        else if (sec >= 60)   swprintf_s(b, L"剩余 %d 分", (sec + 59) / 60);
        else                  swprintf_s(b, L"剩余 %d 秒", sec);
        return b;
    }

    // 排障日志：%APPDATA%\TeacherPrompter\install.log（UTF-8 BOM，超过 512KB 自动重开）
    std::wstring LogDir()
    {
        wchar_t appdata[MAX_PATH] = { 0 };
        if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
            return std::wstring();
        return std::wstring(appdata) + L"\\TeacherPrompter";
    }

    void AppendLog(const wchar_t* fmt, ...)
    {
        const std::wstring dir = LogDir();
        if (dir.empty()) return;
        const std::wstring path = dir + L"\\install.log";
        ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        if (FileSize(path) > 512 * 1024) ::DeleteFileW(path.c_str());

        wchar_t body[2048] = { 0 };
        va_list ap;
        va_start(ap, fmt);
        _vsnwprintf_s(body, _TRUNCATE, fmt, ap);
        va_end(ap);

        SYSTEMTIME st {};
        ::GetLocalTime(&st);
        wchar_t line[2200] = { 0 };
        swprintf_s(line, L"%04d-%02d-%02d %02d:%02d:%02d %s\r\n",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, body);

        HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        if (::GetFileSize(h, nullptr) == 0)
        {
            const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
            DWORD wrote = 0;
            ::WriteFile(h, bom, 3, &wrote, nullptr);
        }
        const std::wstring s(line);
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                               nullptr, 0, nullptr, nullptr);
        if (need > 0)
        {
            std::vector<char> utf8(need);
            ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), utf8.data(), need, nullptr, nullptr);
            DWORD wrote = 0;
            ::WriteFile(h, utf8.data(), (DWORD)utf8.size(), &wrote, nullptr);
        }
        ::CloseHandle(h);
    }

    // 读文本文件（bsdtar 的错误可能是 UTF-8，也可能是本地 ANSI），空白压成单空格
    std::wstring ReadTextFile(const std::wstring& path, size_t maxBytes = 8192)
    {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return std::wstring();

        LARGE_INTEGER size {};
        ::GetFileSizeEx(h, &size);
        size_t take = (size.QuadPart > 0) ? (size_t)size.QuadPart : 0;
        if (take > maxBytes) take = maxBytes;

        std::vector<char> raw(take + 1, 0);
        DWORD got = 0;
        if (take) ::ReadFile(h, raw.data(), (DWORD)take, &got, nullptr);
        ::CloseHandle(h);
        if (!got) return std::wstring();

        int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)got, nullptr, 0);
        UINT cp = CP_UTF8;
        if (n <= 0)
        {
            cp = CP_ACP;
            n = ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)got, nullptr, 0);
        }
        if (n <= 0) return std::wstring();

        std::wstring out((size_t)n, L'\0');
        ::MultiByteToWideChar(cp, 0, raw.data(), (int)got, &out[0], n);
        for (wchar_t& ch : out)
            if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        return out;
    }

    bool RemoveDirTree(const std::wstring& dir)
    {
        if (!DirExists(dir)) return true;
        WIN32_FIND_DATAW fd {};
        HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            const std::wstring child = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                RemoveDirTree(child);
            }
            else
            {
                ::SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                ::DeleteFileW(child.c_str());
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        return ::RemoveDirectoryW(dir.c_str()) != FALSE;
    }

    // bz2 文件头是 "BZh"：被网络代理/杀软换成 HTML 错误页时，能在这里就发现，
    // 不用等 tar 跑完一遍才报错
    bool LooksLikeBz2(const std::wstring& path)
    {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char magic[3] = { 0 };
        DWORD got = 0;
        ::ReadFile(h, magic, 3, &got, nullptr);
        ::CloseHandle(h);
        return got == 3 && magic[0] == 'B' && magic[1] == 'Z' && magic[2] == 'h';
    }

    // 目标卷剩余空间够不够（查不到就不拦，交给 tar 自己报错）
    bool FreeSpaceAtLeast(const std::wstring& dir, ULONGLONG need)
    {
        ULARGE_INTEGER freeBytes {};
        ULARGE_INTEGER totalBytes {};
        if (!::GetDiskFreeSpaceExW(dir.c_str(), &freeBytes, &totalBytes, nullptr)) return true;
        return freeBytes.QuadPart >= need;
    }

    // 跑 tar 解压；stdout/stderr 落到 logPath，失败时把真实错误带回 errText
    bool RunTar(const std::wstring& tar, const wchar_t* mode, const std::wstring& package,
                const std::wstring& dest, const std::wstring& logPath, std::wstring& errText)
    {
        const std::wstring cmd = L"\"" + tar + L"\" " + mode + L" \"" + package + L"\" -C \"" + dest + L"\"";
        std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
        cmdline.push_back(0);

        SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
        HANDLE hLog = ::CreateFileW(logPath.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW si {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (hLog != INVALID_HANDLE_VALUE)
        {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput  = nullptr;
            si.hStdOutput = hLog;
            si.hStdError  = hLog;          // 关键：把 tar 的报错留住
        }

        PROCESS_INFORMATION pi {};
        if (!::CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                              hLog != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW,
                              nullptr, nullptr, &si, &pi))
        {
            const DWORD e = ::GetLastError();
            if (hLog != INVALID_HANDLE_VALUE) ::CloseHandle(hLog);
            errText = L"启动 tar 失败（" + std::to_wstring(e) + L"）";
            return false;
        }
        if (hLog != INVALID_HANDLE_VALUE) ::CloseHandle(hLog);

        const DWORD waitRc = ::WaitForSingleObject(pi.hProcess, 30 * 60 * 1000);
        DWORD exitCode = 1;
        ::GetExitCodeProcess(pi.hProcess, &exitCode);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        if (waitRc != WAIT_OBJECT_0)
        {
            errText = L"解压超时（超过 30 分钟）";
            return false;
        }
        if (exitCode != 0)
        {
            errText = ReadTextFile(logPath, 4096);
            const std::wstring code = L"tar 退出码 " + std::to_wstring(exitCode);
            errText = errText.empty() ? code : (code + L"：" + errText);
            return false;
        }
        return true;
    }

    // 把 tar 的英文错误翻译成老师能照做的下一步
    std::wstring ExplainTarError(const std::wstring& raw)
    {
        std::wstring low;
        low.reserve(raw.size());
        for (wchar_t ch : raw) low.push_back((wchar_t)::towlower(ch));
        auto has = [&](const wchar_t* k) { return low.find(k) != std::wstring::npos; };

        if (has(L"truncated") || has(L"damaged") || has(L"bzip") || has(L"unexpected end")
            || has(L"unrecognized archive") || has(L"not a valid"))
            return L"安装包不完整（已清理，请重新下载）";
        if (has(L"no space") || has(L"space left") || has(L"disk full"))
            return L"磁盘空间不足，清理后重试";
        if (has(L"permission denied") || has(L"access is denied"))
            return L"没有写入权限（可能被杀毒软件拦了）";
        if (has(L"file name too long") || has(L"path too long"))
            return L"路径太长，请把程序放到更短的目录";
        if (has(L"can't chdir") || has(L"cannot chdir"))
            return L"解压目录不可用，请重启程序";
        return L"解压失败（详见 install.log）";
    }
}

std::wstring CModelInstaller::ModelName()
{
    return CSherpaEngine::ModelFolderName();
}

std::wstring CModelInstaller::DownloadUrl()
{
    wchar_t env[2048] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_MODEL_URL", env, ARRAYSIZE(env)) > 0 && env[0])
        return env;
    return kOfficialUrl;
}

std::wstring CModelInstaller::InstallRoot()
{
    static const std::wstring root = ComputeInstallRoot();
    return root;
}

std::wstring CModelInstaller::InstallDir()
{
    return InstallRoot() + L"\\" + ModelName();
}

std::wstring CModelInstaller::PackagePath()
{
    return InstallRoot() + L"\\_download\\" + ModelName() + L".tar.bz2.part";
}

bool CModelInstaller::Installed()
{
    return CSherpaEngine::ModelAvailable();
}

CModelInstaller::~CModelInstaller()
{
    Cancel();
    Wait();
}

std::wstring CModelInstaller::Status()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void CModelInstaller::SetProgress(int percent, const std::wstring& text)
{
    m_percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = text;
    }
    PostNotify();
}

void CModelInstaller::PostNotify()
{
    if (m_notify && m_msg)
        ::PostMessageW(m_notify, m_msg, 0, 0);
}

void CModelInstaller::Start(HWND notify, UINT msg)
{
    if (m_running) return;
    if (m_thread.joinable()) m_thread.join();

    m_notify = notify;
    m_msg = msg;
    m_cancel = false;
    m_percent = 0;
    m_state = State::Connecting;
    m_activeReq = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = L"正在连接下载服务器…";
    }
    m_running = true;
    m_thread = std::thread(&CModelInstaller::ThreadMain, this);
}

void CModelInstaller::Cancel()
{
    m_cancel = true;
    // 关掉正在读的句柄：阻塞在 WinHttpReadData 的线程会立刻拿到错误并退出，
    // 退出程序时不用干等接收超时。
    HINTERNET h = m_activeReq.exchange(nullptr);
    if (h) ::WinHttpCloseHandle(h);
}

void CModelInstaller::Wait()
{
    if (m_thread.joinable()) m_thread.join();
}

void CModelInstaller::ThreadMain()
{
    std::wstring err;

    if (Installed())
    {
        m_state = State::Done;
        SetProgress(100, L"语音模型已安装");
        m_running = false;
        return;
    }

    // TP_MODEL_URL 说明用户有自建源（或测试脚本在跑 tar 通道），沿用整包流程；
    // 默认走镜像逐文件下载 —— 不碰 tar，也不会因为一个坏包整轮重来。
    wchar_t env[8] = { 0 };
    const bool customUrl = ::GetEnvironmentVariableW(L"TP_MODEL_URL", env, ARRAYSIZE(env)) > 0;

    bool ok = false;
    bool extracting = false;
    if (customUrl)
    {
        ok = DownloadTo(err);
        if (ok && !m_cancel)
        {
            extracting = true;
            m_state = State::Extracting;
            SetProgress(95, L"正在解压安装（约 1~2 分钟，请不要关闭程序）…");
            ok = ExtractTo(err);
        }
    }
    else
    {
        m_state = State::Downloading;
        ok = DownloadFiles(err);
    }

    if (m_cancel)
    {
        m_state = State::Canceled;
        SetProgress(m_percent, L"已暂停下载（下次会接着下）");
        m_running = false;
        return;
    }
    if (!ok)
    {
        m_state = State::Failed;
        SetProgress(m_percent, (extracting ? L"安装失败：" : L"下载失败：") + err);
        m_running = false;
        return;
    }

    if (!Installed())
    {
        m_state = State::Failed;
        SetProgress(95, L"安装失败：模型文件不完整，请重试");
        m_running = false;
        return;
    }

    // 装好了就把下载中间产物清掉（老版本留下的 tar.bz2 坏包可能占几百 MB）
    RemoveDirTree(InstallRoot() + L"\\_download");

    m_state = State::Done;
    m_percent = 100;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = L"语音模型已安装 · 可以开始跟读";
    }
    PostNotify();
    m_running = false;
}

// 逐文件下载：每个文件独立续传，某个源不行就换下一个源
bool CModelInstaller::DownloadFiles(std::wstring& err)
{
    const std::wstring dir = InstallDir();
    if (!DirWritable(dir))
    {
        err = L"安装目录不可写：" + dir;
        return false;
    }

    ULONGLONG totalBytes = 0;
    for (const auto& f : kModelFiles) totalBytes += f.bytes;

    const ULONGLONG need = totalBytes + totalBytes / 10;
    if (!FreeSpaceAtLeast(dir, need))
    {
        err = L"磁盘空间不足（需要约 " + FmtMB(need) + L"）";
        AppendLog(L"空间预检不通过：需要 %llu 字节 → %s", need, dir.c_str());
        return false;
    }

    ULONGLONG doneBefore = 0;
    const bool custom = !CustomMirror().empty();
    for (const auto& f : kModelFiles)
    {
        if (m_cancel) return false;

        const std::wstring dest = dir + L"\\" + f.name;
        const ULONGLONG have = FileSize(dest);
        // 已装好的文件跳过：重装 / 修复都很快。.part 机制保证正式文件只会是完整的，
        // 所以自建镜像下只要文件在就算好了（大小可能和官方不同）
        if (have == f.bytes || (custom && have > 0))
        {
            doneBefore += f.bytes;
            continue;
        }

        std::wstring lastErr;
        bool got = false;
        int tried = 0;
        for (const wchar_t* host : kMirrorHosts)
        {
            if (custom && tried++ > 0) break;      // 自定义镜像只有一个源
            if (m_cancel) return false;
            AppendLog(L"下载 %s ← %s", f.name, host);

            std::wstring one;
            if (DownloadOneFile(MirrorUrl(host, f.name), dest, f.name, f.bytes,
                                doneBefore, totalBytes, one))
            {
                got = true;
                break;
            }
            if (m_cancel) return false;
            lastErr = one;
            AppendLog(L"源 %s 下载 %s 失败：%s", host, f.name, one.c_str());
        }

        if (!got)
        {
            err = std::wstring(f.name) + L"：" + lastErr;
            return false;
        }
        doneBefore += f.bytes;
    }
    return true;
}

// 单个文件：先写 dest.part，校验通过再改名成正式文件
bool CModelInstaller::DownloadOneFile(const std::wstring& url, const std::wstring& dest,
                                      const std::wstring& name, ULONGLONG expect,
                                      ULONGLONG doneBefore, ULONGLONG totalBytes, std::wstring& err)
{
    const std::wstring part = dest + L".part";
    ULONGLONG have = FileSize(part);
    if (have > expect)                     // 跟服务器的文件对不上，丢掉重来
    {
        ::DeleteFileW(part.c_str());
        have = 0;
    }
    if (have == expect)                    // 上次下完但没改名
    {
        ::DeleteFileW(dest.c_str());
        if (::MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        have = 0;
    }

    URL_COMPONENTS uc {};
    uc.dwStructSize     = sizeof(uc);
    wchar_t host[256]   = { 0 };
    wchar_t path[2048]  = { 0 };
    uc.lpszHostName     = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = ARRAYSIZE(path);

    if (!::WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc))
    {
        err = L"下载地址无效：" + url;
        return false;
    }
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    // 先按机器实际情况选策略（配了代理就走代理，没配就直连），不通再换另一种。
    // 只认死一种会出事：没配代理的机器上 WPAD 自动发现要空转十几秒，
    // 配了代理的机器上直连又会一直被墙。
    // 策略顺序：显式代理（环境变量）→ 系统代理 / PAC → 直连
    struct NetPlan { bool proxy; std::wstring addr; };
    std::vector<NetPlan> plan;
    const std::wstring envProxy = EnvProxy();
    if (!envProxy.empty())       plan.push_back({ true, envProxy });
    if (HasSystemProxy())        plan.push_back({ true, std::wstring() });
    plan.push_back({ false, std::wstring() });

    bool ok = false;
    for (size_t mi = 0; mi < plan.size() && !ok && !m_cancel; ++mi)
    {
        const bool useProxy = plan[mi].proxy;
        if (mi > 0)
            AppendLog(L"%s 换网络策略重试（%s）", name.c_str(),
                      !useProxy ? L"直连"
                                : (plan[mi].addr.empty() ? L"系统代理" : L"环境变量代理"));

        HINTERNET ses = OpenSession(useProxy, plan[mi].addr);
        if (!ses)
        {
            err = L"初始化网络失败（" + std::to_wstring(::GetLastError()) + L"）";
            return false;
        }

        HINTERNET con = nullptr;
        HINTERNET req = nullptr;
        FILE* fp = nullptr;
        err.clear();

        // 416（.part 比服务器文件还长）时清掉重下一次
        for (int attempt = 0; attempt < 2 && !ok && !m_cancel; ++attempt)
        {
            err.clear();
            con = ::WinHttpConnect(ses, host, uc.nPort, 0);
            if (!con) { err = L"连接 " + std::wstring(host) + L" 失败（" + std::to_wstring(::GetLastError()) + L"）"; break; }

            req = ::WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       https ? WINHTTP_FLAG_SECURE : 0);
            if (!req) { err = L"创建请求失败（" + std::to_wstring(::GetLastError()) + L"）"; break; }
            m_activeReq = req;

            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            ::WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

            if (have > 0)
            {
                wchar_t range[128] = { 0 };
                swprintf_s(range, L"Range: bytes=%llu-", have);
                ::WinHttpAddRequestHeaders(req, range, (DWORD)-1L,
                                           WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }

            const std::wstring head = L"正在连接下载服务器（" + name + L"）…";
            SetProgress(int(95.0 * double(doneBefore + have) / double(totalBytes)), head);

            if (!::WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                err = L"发送请求失败（" + std::to_wstring(::GetLastError()) + L"）";
                break;
            }
            if (!::WinHttpReceiveResponse(req, nullptr))
            {
                err = L"服务器无响应（" + std::to_wstring(::GetLastError()) + L"）";
                break;
            }

            DWORD code = 0;
            DWORD codeLen = sizeof(code);
            ::WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen, WINHTTP_NO_HEADER_INDEX);

            DWORD contentLen = 0;
            DWORD lenLen = sizeof(contentLen);
            ::WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &contentLen, &lenLen, WINHTTP_NO_HEADER_INDEX);

            // 有的代理 / 网关会把响应改成 chunked，拿不到 Content-Length。
            // 这种情况不能直接判失败：按清单大小做进度基准，最后用实际字节数校验。
            ULONGLONG total = expect;
            bool knownLen = false;
            if (code == 206 && have > 0 && contentLen > 0)
            {
                total = have + contentLen;
                knownLen = true;
            }
            else if (code == 200 && contentLen > 0)
            {
                have = 0;                       // 服务器不支持续传：从头写
                total = contentLen;
                knownLen = true;
            }
            else if (code == 200)
            {
                have = 0;
                AppendLog(L"%s 无 Content-Length（可能 chunked），按清单 %llu 估进度",
                          name.c_str(), expect);
            }
            else if (code == 416 && have > 0)
            {
                AppendLog(L"%s 返回 416，丢掉 .part 重下", name.c_str());
                ::DeleteFileW(part.c_str());
                have = 0;
                m_activeReq = nullptr;          // 先摘掉句柄，避免 Cancel 抢先关闭后重复关
                ::WinHttpCloseHandle(req);
                req = nullptr;
                ::WinHttpCloseHandle(con);
                con = nullptr;
                continue;
            }
            else
            {
                err = L"服务器返回 HTTP " + std::to_wstring(code);
                break;
            }

            if (knownLen && total != expect)
            {
                // 官方源的文件是固定版本，大小对不上多半是拿到错误页（代理/门户劫持）。
                // 但自建镜像可能放了别的裁剪版本，只记日志不拦。
                if (CustomMirror().empty())
                {
                    err = L"文件大小不符（期望 " + FmtMB(expect) + L"，实际 " + FmtMB(total) + L"）";
                    AppendLog(L"%s 大小不符：期望 %llu，实际 %llu（URL %s）",
                              name.c_str(), expect, total, url.c_str());
                    break;
                }
                AppendLog(L"%s 大小与清单不同（%llu / %llu），按自定义镜像处理，继续",
                          name.c_str(), total, expect);
            }

            fp = _wfopen(part.c_str(), have > 0 ? L"ab" : L"wb");
            if (!fp) { err = L"无法写入文件：" + part; break; }

            std::vector<char> buf(256 * 1024);
            ULONGLONG done = have;
            ULONGLONG lastBytes = done;
            DWORD lastTick = ::GetTickCount();
            double speed = 0.0;

            while (!m_cancel)
            {
                DWORD got = 0;
                if (!::WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got))
                {
                    if (!m_cancel)
                        err = L"下载中断（" + std::to_wstring(::GetLastError()) + L"）";
                    break;
                }
                if (got == 0) break;            // 正常结束

                if (fwrite(buf.data(), 1, got, fp) != got)
                {
                    err = L"写入磁盘失败（磁盘空间不足？）";
                    break;
                }
                done += got;

                const DWORD now = ::GetTickCount();
                if (now - lastTick >= 400)
                {
                    const double dt = (now - lastTick) / 1000.0;
                    const double inst = double(done - lastBytes) / (dt > 0 ? dt : 1.0);
                    speed = (speed > 0.0) ? (speed * 0.6 + inst * 0.4) : inst;
                    lastBytes = done;
                    lastTick = now;

                    const ULONGLONG all = doneBefore + done;
                    const int pct = int(95.0 * double(all) / double(totalBytes));
                    wchar_t line[256] = { 0 };
                    const std::wstring eta = FmtEta(totalBytes > all ? totalBytes - all : 0, speed);
                    swprintf_s(line, L"正在下载语音模型 %d%% · %s / %s · %s%s%s",
                               int(100.0 * double(all) / double(totalBytes)),
                               FmtMB(all).c_str(), FmtMB(totalBytes).c_str(),
                               FmtSpeed(speed).c_str(),
                               eta.empty() ? L"" : L" · ",
                               eta.c_str());
                    SetProgress(pct, line);
                }
            }

            if (m_cancel) break;
            if (!err.empty()) break;

            // 连接被中间掐断时 ReadData 只返回 0，看着像正常结束 —— 必须比对字节数。
            // 有 Content-Length 就比它；没有（chunked）时官方源比清单大小，自建镜像只要有内容。
            const ULONGLONG want = knownLen ? total : expect;
            const bool complete = knownLen ? (done == total)
                                           : (CustomMirror().empty() ? (done == expect) : (done > 0));
            if (!complete)
            {
                err = L"下载中断（" + FmtMB(done) + L" / " + FmtMB(want) + L"，进度已保留）";
                AppendLog(L"%s 下载不完整：%llu / %llu 字节", name.c_str(), done, want);
                break;
            }

            if (fp) { fclose(fp); fp = nullptr; }
            ::DeleteFileW(dest.c_str());
            if (!::MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
            {
                err = L"保存文件失败：" + dest;
                break;
            }
            ok = true;
        }

        if (fp) fclose(fp);
        HINTERNET ownedReq = m_activeReq.exchange(nullptr);
        if (ownedReq) ::WinHttpCloseHandle(ownedReq);
        if (con) ::WinHttpCloseHandle(con);
        ::WinHttpCloseHandle(ses);
    }

    if (!ok && !m_cancel && err.empty())
        err = L"网络不通（代理和直连都失败）";
    return ok;
}

bool CModelInstaller::DownloadTo(std::wstring& err)
{
    const std::wstring url  = DownloadUrl();
    const std::wstring part = PackagePath();

    if (!DirWritable(ParentDir(part)))
    {
        err = L"安装目录不可写：" + ParentDir(part);
        return false;
    }

    // 断点续传：先看有没有上次没下完的 .part
    ULONGLONG have = FileSize(part);

    URL_COMPONENTS uc {};
    uc.dwStructSize     = sizeof(uc);
    wchar_t host[256]   = { 0 };
    wchar_t path[2048]  = { 0 };
    uc.lpszHostName     = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = ARRAYSIZE(path);

    if (!::WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc))
    {
        err = L"下载地址无效：" + url;
        return false;
    }
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET ses = ::WinHttpOpen(L"YunDouTeleprompter/1.0 (offline-asr-model)",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
    {
        err = L"初始化网络失败（" + std::to_wstring(::GetLastError()) + L"）";
        return false;
    }
    ::WinHttpSetTimeouts(ses, 10000, 10000, 20000, 60000);

    bool ok = false;
    HINTERNET con = nullptr;
    HINTERNET req = nullptr;
    FILE* fp = nullptr;
    do
    {
        con = ::WinHttpConnect(ses, host, uc.nPort, 0);
        if (!con) { err = L"连接服务器失败（" + std::to_wstring(::GetLastError()) + L"）"; break; }

        req = ::WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { err = L"创建请求失败（" + std::to_wstring(::GetLastError()) + L"）"; break; }
        m_activeReq = req;

        // GitHub release 会 302 到签名地址，必须允许跨主机重定向
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        ::WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

        if (have > 0)
        {
            wchar_t range[128] = { 0 };
            swprintf_s(range, L"Range: bytes=%llu-", have);
            ::WinHttpAddRequestHeaders(req, range, (DWORD)-1L,
                                       WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        SetProgress(int(m_percent), L"正在连接下载服务器…");

        if (!::WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            err = L"发送请求失败（" + std::to_wstring(::GetLastError()) + L"）";
            break;
        }
        if (!::WinHttpReceiveResponse(req, nullptr))
        {
            err = L"服务器无响应（" + std::to_wstring(::GetLastError()) + L"）";
            break;
        }

        DWORD code = 0;
        DWORD codeLen = sizeof(code);
        ::WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen, WINHTTP_NO_HEADER_INDEX);

        DWORD contentLen = 0;
        DWORD lenLen = sizeof(contentLen);
        ::WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &contentLen, &lenLen, WINHTTP_NO_HEADER_INDEX);

        ULONGLONG total = 0;
        if (code == 206 && have > 0)
        {
            total = have + contentLen;          // 服务器接着给
        }
        else if (code == 200)
        {
            have = 0;                           // 不支持续传 / 首次下载：从头来
            total = contentLen;
        }
        else if (code == 416)
        {
            // .part 已经比服务器文件还大（换过源），丢掉重下
            ::DeleteFileW(part.c_str());
            err = L"临时文件与服务器不一致，请重试";
            break;
        }
        else
        {
            err = L"服务器返回 HTTP " + std::to_wstring(code);
            break;
        }

        if (total == 0)
        {
            err = L"服务器没有给出文件大小（可能是镜像不支持）";
            break;
        }

        fp = _wfopen(part.c_str(), have > 0 ? L"ab" : L"wb");
        if (!fp) { err = L"无法写入临时文件：" + part; break; }

        std::vector<char> buf(256 * 1024);
        ULONGLONG done = have;
        ULONGLONG lastBytes = done;
        DWORD lastTick = ::GetTickCount();
        double speed = 0.0;

        while (!m_cancel)
        {
            DWORD got = 0;
            if (!::WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got))
            {
                err = L"下载中断（" + std::to_wstring(::GetLastError()) + L"）";
                break;
            }
            if (got == 0) break;                // 正常结束

            if (fwrite(buf.data(), 1, got, fp) != got)
            {
                err = L"写入磁盘失败（磁盘空间不足？）";
                break;
            }
            done += got;

            const DWORD now = ::GetTickCount();
            if (now - lastTick >= 400)
            {
                const double dt = (now - lastTick) / 1000.0;
                const double inst = double(done - lastBytes) / (dt > 0 ? dt : 1.0);
                speed = (speed > 0.0) ? (speed * 0.6 + inst * 0.4) : inst;
                lastBytes = done;
                lastTick = now;

                const int pct = int(100.0 * done / double(total) * 0.9);   // 下载占 0~90%，解压占 90~100%
                wchar_t line[256] = { 0 };
                const std::wstring eta = FmtEta(total > done ? total - done : 0, speed);
                swprintf_s(line, L"正在下载语音模型 %d%% · %s / %s · %s%s%s",
                           int(100.0 * done / double(total)),
                           FmtMB(done).c_str(), FmtMB(total).c_str(),
                           FmtSpeed(speed).c_str(),
                           eta.empty() ? L"" : L" · ",
                           eta.c_str());
                SetProgress(pct, line);
            }
        }

        if (m_cancel)
        {
            ok = false;
            break;
        }
        if (!err.empty())
        {
            break;
        }

        // 关键：连接被中间掐断时 ReadData 只返回 0 字节，看起来像「正常结束」。
        // 不比对总长就会留下一个截断的 .part，后面 tar 解压必然失败。
        if (done != total)
        {
            err = L"下载中断（" + FmtMB(done) + L" / " + FmtMB(total) + L"，进度已保留）";
            AppendLog(L"下载不完整：%llu / %llu 字节", done, total);
            break;
        }
        ok = true;
    } while (false);

    if (fp) fclose(fp);
    // Cancel() 可能已抢先关掉了请求句柄（exchange 拿到空就说明被接管了）
    HINTERNET ownedReq = m_activeReq.exchange(nullptr);
    if (ownedReq) ::WinHttpCloseHandle(ownedReq);
    if (con) ::WinHttpCloseHandle(con);
    ::WinHttpCloseHandle(ses);

    return ok;
}

bool CModelInstaller::ExtractTo(std::wstring& err)
{
    const std::wstring part = PackagePath();
    const std::wstring root = InstallRoot();
    const std::wstring dlDir = ParentDir(part);

    if (!FileExists(part))
    {
        err = L"下载的压缩包不见了：" + part;
        return false;
    }

    const std::wstring tar = SystemTarPath();
    if (tar.empty())
    {
        err = L"系统没有 tar.exe（需要 Win10 1803 以上）";
        return false;
    }

    const ULONGLONG packSize = FileSize(part);
    AppendLog(L"开始解压 %s（%.0f MB）→ %s", part.c_str(),
              double(packSize) / 1048576.0, root.c_str());

    if (!LooksLikeBz2(part))
    {
        ::DeleteFileW(part.c_str());
        err = L"安装包不完整（下载内容不是 bz2，可能被代理替换）";
        AppendLog(L"文件头不是 BZh，已删除 %s", part.c_str());
        return false;
    }

    // 解压后大约是压缩包的 2.2 倍（xlarge-int8 实测约 1.25 GB）
    const ULONGLONG need = packSize * 22 / 10;
    if (!FreeSpaceAtLeast(root, need))
    {
        err = L"磁盘空间不足（解压需约 " + FmtMB(need) + L"）";
        AppendLog(L"磁盘空间预检不通过：需要 %.0f MB", double(need) / 1048576.0);
        return false;
    }

    // 先解到 _unpack，成功后再整体搬到 models\<ModelName>：
    // 解压失败不会留下半个模型目录，也不会破坏已经装好的旧模型
    const std::wstring staging = root + L"\\_unpack";
    RemoveDirTree(staging);
    if (!DirWritable(staging))
    {
        err = L"无法创建解压目录：" + staging;
        AppendLog(L"解压目录不可写：%s", staging.c_str());
        return false;
    }

    const std::wstring logPath = dlDir + L"\\tar.log";
    std::wstring raw;
    bool extracted = false;
    // 先用 -xf 让 libarchive 自己识别压缩格式（新版 Windows tar 支持），
    // 不行再退回显式 -xjf，兼容不同版本自带的 bsdtar
    for (const wchar_t* mode : { L"-xf", L"-xjf" })
    {
        RemoveDirTree(staging);
        if (!DirWritable(staging))
        {
            err = L"无法创建解压目录：" + staging;
            return false;
        }
        if (RunTar(tar, mode, part, staging, logPath, raw))
        {
            extracted = true;
            break;
        }
        AppendLog(L"tar %s 失败：%s", mode, raw.c_str());
    }

    if (!extracted)
    {
        RemoveDirTree(staging);
        std::wstring why = ExplainTarError(raw);
        // 包本身坏了就没法续传（.part 已经是坏文件），删掉让下次从头下
        if (why.find(L"不完整") != std::wstring::npos) ::DeleteFileW(part.c_str());

        err = why;
        if (why == L"解压失败")
            err += L"（详见 " + LogDir() + L"\\install.log）";
        return false;
    }

    const std::wstring from = staging + L"\\" + ModelName();
    if (!DirExists(from))
    {
        RemoveDirTree(staging);
        err = L"安装包里没有 " + ModelName() + L" 目录，下载源可能不对";
        AppendLog(L"解压后找不到顶层目录：%s", from.c_str());
        return false;
    }

    const std::wstring to = InstallDir();
    RemoveDirTree(to);                          // 清掉上一次的半成品
    if (!::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED))
    {
        const DWORD e = ::GetLastError();
        RemoveDirTree(staging);
        err = L"安装模型文件失败（错误码 " + std::to_wstring(e) + L"），请检查磁盘空间或占用";
        AppendLog(L"MoveFileEx 失败 err=%lu：%s -> %s", e, from.c_str(), to.c_str());
        return false;
    }
    RemoveDirTree(staging);

    AppendLog(L"模型安装完成：%s", to.c_str());
    ::DeleteFileW(part.c_str());            // 装好了就不留 570MB 的临时包
    ::DeleteFileW(logPath.c_str());
    ::RemoveDirectoryW(dlDir.c_str());      // _download 空目录顺手清掉
    return true;
}
