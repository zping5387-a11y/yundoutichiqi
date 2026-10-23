// ControlDlg.cpp
#include "ControlDlg.h"
#include "OfficeDoc.h"
#include <shellapi.h>
#include <fstream>
#include <iterator>
#include <share.h>

// 资源管理器重启后会广播 TaskbarCreated，需要重新添加托盘图标
static const UINT g_wmTaskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
// 单实例：用户重复启动时，由新进程通知已有实例把面板叫出来
static const UINT g_wmShowPanelReq = ::RegisterWindowMessageW(L"TeacherPrompter.ShowPanel");

BEGIN_MESSAGE_MAP(CControlDlg, CDialogEx)
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_HSCROLL()
    ON_WM_HOTKEY()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_DROPFILES()
    ON_WM_SIZE()
    ON_WM_MOVE()
    ON_WM_SHOWWINDOW()
    ON_WM_GETMINMAXINFO()
    ON_WM_DRAWITEM()
    ON_WM_NCHITTEST()
    ON_WM_NCACTIVATE()
    ON_WM_PAINT()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_COMMAND_RANGE(IDC_TAB_SCRIPT, IDC_TAB_SETTINGS, &CControlDlg::OnTabClick)
    ON_COMMAND_RANGE(IDC_TAB_ABOUT, IDC_TAB_ABOUT, &CControlDlg::OnTabClick)
    ON_BN_CLICKED(IDC_BTN_COPY_CONTACT, &CControlDlg::OnCopyContact)
    ON_STN_CLICKED(IDC_ST_CONTACT, &CControlDlg::OnContactClick)
    ON_BN_CLICKED(IDC_BTN_OPEN, &CControlDlg::OnOpenScript)
    ON_BN_CLICKED(IDC_BTN_CLIPBOARD, &CControlDlg::OnClipboardScript)
    ON_BN_CLICKED(IDC_BTN_SAMPLE, &CControlDlg::OnSampleScript)
    ON_BN_CLICKED(IDC_BTN_SPEECH, &CControlDlg::OnToggleSpeech)
    ON_BN_CLICKED(IDC_BTN_VOICECMD, &CControlDlg::OnToggleCmd)
    ON_STN_CLICKED(IDC_ST_SPEECH, &CControlDlg::OnModelStatusClick)
    ON_MESSAGE(WM_APP_MODEL_EVENT, &CControlDlg::OnModelEvent)
    ON_CBN_SELCHANGE(IDC_CMB_MIC, &CControlDlg::OnMicChanged)
    ON_CBN_SELCHANGE(IDC_CMB_LANG, &CControlDlg::OnLangChanged)
    ON_CBN_SELCHANGE(IDC_CMB_ENGINE, &CControlDlg::OnEngineChanged)
    ON_BN_CLICKED(IDC_BTN_SHOW, &CControlDlg::OnToggleShow)
    ON_BN_CLICKED(IDC_BTN_LOCK, &CControlDlg::OnToggleLock)
    ON_BN_CLICKED(IDC_BTN_FULLSCREEN, &CControlDlg::OnFullscreen)
    ON_BN_CLICKED(IDC_BTN_RESETPOS, &CControlDlg::OnResetPos)
    ON_BN_CLICKED(IDC_BTN_W_MINUS, &CControlDlg::OnSizeWMinus)
    ON_BN_CLICKED(IDC_BTN_W_PLUS, &CControlDlg::OnSizeWPlus)
    ON_BN_CLICKED(IDC_BTN_H_MINUS, &CControlDlg::OnSizeHMinus)
    ON_BN_CLICKED(IDC_BTN_H_PLUS, &CControlDlg::OnSizeHPlus)
    ON_BN_CLICKED(IDC_BTN_COLOR, &CControlDlg::OnPickTextColor)
    ON_BN_CLICKED(IDC_BTN_HICOLOR, &CControlDlg::OnPickHiColor)
    ON_BN_CLICKED(IDC_BTN_BGCOLOR, &CControlDlg::OnPickBgColor)
    ON_BN_CLICKED(IDC_BTN_QUIT, &CControlDlg::OnQuit)
    ON_BN_CLICKED(IDC_BTN_HIDE_PANEL, &CControlDlg::OnHidePanel)
    ON_MESSAGE(WM_APP_TRAY, &CControlDlg::OnTrayMessage)
    ON_REGISTERED_MESSAGE(g_wmTaskbarCreated, &CControlDlg::OnTaskbarCreated)
    ON_REGISTERED_MESSAGE(g_wmShowPanelReq, &CControlDlg::OnShowPanelRequest)
    ON_CONTROL_RANGE(BN_CLICKED, IDC_CHK_HIGHLIGHT, IDC_CHK_SHADOW, &CControlDlg::OnCheckChanged)
    ON_CONTROL_RANGE(BN_CLICKED, IDC_CHK_PASSTHRU, IDC_CHK_PASSTHRU, &CControlDlg::OnCheckChanged)
END_MESSAGE_MAP()

namespace
{
    // 主面板设计稿尺寸（逻辑单位，S() 负责 DPI / 自适应缩放）
    // 左侧竖排页签 84 宽，右侧内容区从 x=112 起、宽 432
    constexpr int kDesignW  = 640;
    constexpr int kDesignH  = 640;
    constexpr int kCaptionH = 44;      // 无边框窗口的标题拖动带高度
    constexpr int kTabX     = 16;
    constexpr int kTabW     = 84;
    constexpr int kTab1Y    = 76;
    constexpr int kTabH     = 52;
    constexpr int kTabGap   = 6;
    constexpr int kTab2Y    = kTab1Y + kTabH + kTabGap;    // 134 语音
    constexpr int kTab3Y    = kTab2Y + kTabH + kTabGap;    // 192 设置
    constexpr int kTab4Y    = kTab3Y + kTabH + kTabGap;    // 250 关于
    constexpr int kCx       = 112;     // 内容区左边界
    constexpr int kCw       = 512;     // 内容区宽度（右边留 16）

    const wchar_t* kSampleScript =
        L"同学们，上课！\n"
        L"上一节课我们讨论了物体在液体中的沉浮条件，今天这节课，我们来学习浮力的大小究竟与什么有关。\n"
        L"先看一个生活中的现象：把一个空矿泉水瓶按进水里，手会感觉到一个向上的力，而且按得越深，这个力越大。\n"
        L"这说明，浸在液体中的物体受到向上的浮力，浮力的大小与物体排开液体的体积有关。\n"
        L"那么，浮力到底等于多少呢？我们用弹簧测力计做一个小实验。\n"
        L"第一步，把石块挂在测力计下，读出空气中的示数，记为 G。\n"
        L"第二步，把石块缓慢浸入水中，读出此时的示数，记为 F。\n"
        L"第三步，我们发现，G 减去 F，正好等于被石块排开的那部分水的重力。\n"
        L"于是我们得到阿基米德原理：浸在液体中的物体所受浮力，等于它排开的液体所受的重力。\n"
        L"写成公式就是 F浮 等于 ρ液 g V排。\n"
        L"请注意，这里用的是液体的密度，而不是物体的密度；用的是排开液体的体积，而不是物体的体积。\n"
        L"这两个「陷阱」是考试中最容易出错的地方，请同学们在笔记本上重点标记。\n"
        L"接下来，我们做两道随堂练习，检验一下大家的理解。\n"
        L"第一题：一个体积为 100 立方厘米的物体，完全浸没在水中，求它受到的浮力。\n"
        L"提示：先求排开水的体积，再用密度、重力加速度和体积相乘。\n"
        L"好，给大家一分钟时间，做完的同学举手示意。\n"
        L"（示例文稿到此结束，可以添加你自己的教案或讲稿）";

    // 用 GDI+ 现画一个托盘图标（深蓝圆角方块 + 白色 T），免去额外的 .ico 资源
    HICON CreateTrayIcon(int size)
    {
        HICON icon = nullptr;
        {
            Gdiplus::Bitmap bmp(size, size, PixelFormat32bppARGB);
            if (bmp.GetLastStatus() != Gdiplus::Ok) return nullptr;

            Gdiplus::Graphics g(&bmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));

            const Gdiplus::REAL pad = size * 0.07f;
            const Gdiplus::REAL r = size * 0.24f;
            const Gdiplus::RectF rc(pad, pad, (Gdiplus::REAL)size - pad * 2, (Gdiplus::REAL)size - pad * 2);

            Gdiplus::GraphicsPath path;
            path.AddArc(rc.X, rc.Y, r, r, 180.0f, 90.0f);
            path.AddArc(rc.GetRight() - r, rc.Y, r, r, 270.0f, 90.0f);
            path.AddArc(rc.GetRight() - r, rc.GetBottom() - r, r, r, 0.0f, 90.0f);
            path.AddArc(rc.X, rc.GetBottom() - r, r, r, 90.0f, 90.0f);
            path.CloseFigure();

            Gdiplus::SolidBrush bg(Gdiplus::Color(255, 34, 78, 150));
            g.FillPath(&bg, &path);

            Gdiplus::FontFamily famSeg(L"Segoe UI");
            Gdiplus::FontFamily famYa(L"Microsoft YaHei UI");
            const Gdiplus::FontFamily* fam = famSeg.IsAvailable() ? &famSeg
                                              : (famYa.IsAvailable() ? &famYa : Gdiplus::FontFamily::GenericSansSerif());
            Gdiplus::Font font(fam, (Gdiplus::REAL)(size * 0.66f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush fg(Gdiplus::Color(255, 255, 255, 255));
            g.DrawString(L"T", -1, &font, rc, &fmt, &fg);

            bmp.GetHICON(&icon);
        }
        return icon;
    }
}

CControlDlg::CControlDlg(CWnd* pParent)
    : CDialogEx(IDD_CONTROL_DIALOG, pParent)
{
}

namespace
{
    // 跟读排查用：设置 TP_SPEECH_LOG=<文件路径> 后，把识别文本、匹配结果、语音命令写入日志
    FILE* SpeechLogFile()
    {
        static FILE* fp = nullptr;
        static bool inited = false;
        if (!inited)
        {
            inited = true;
            wchar_t path[MAX_PATH] = { 0 };
            DWORD n = ::GetEnvironmentVariableW(L"TP_SPEECH_LOG", path, MAX_PATH);
            // _SH_DENYWR：允许自动化脚本边跑边读，只禁止并发写
            if (n > 0 && n < MAX_PATH) fp = _wfsopen(path, L"ab", _SH_DENYWR);
        }
        return fp;
    }

    void WriteLogLine(const std::wstring& l)
    {
        FILE* fp = SpeechLogFile();
        if (!fp) return;

        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(),
                                               nullptr, 0, nullptr, nullptr);
        if (bytes > 0)
        {
            std::string utf8((size_t)bytes, '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, l.c_str(), (int)l.size(), &utf8[0], bytes, nullptr, nullptr);
            fwrite(utf8.data(), 1, utf8.size(), fp);
            fflush(fp);
        }
    }

    void AppendSpeechLog(const std::wstring& text, bool matched, int pos, int total, float score)
    {
        if (!SpeechLogFile()) return;

        SYSTEMTIME st;
        ::GetLocalTime(&st);
        wchar_t line[2048];
        swprintf_s(line, L"%02d:%02d:%02d.%03d\t%s\tpos=%d/%d\tscore=%.2f\r\n",
                   st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                   matched ? L"MATCH" : L"MISS ", pos, total, score);
        std::wstring l = line;
        l += text;
        l += L"\r\n";
        WriteLogLine(l);
    }

    // 语音命令命中记录：CMD<TAB>命令名<TAB>滚动位置（像素）
    void AppendCmdLog(const wchar_t* label, float scrollY, const std::wstring& heard)
    {
        if (!SpeechLogFile()) return;

        wchar_t line[512];
        swprintf_s(line, L"CMD\t%s\ty=%.1f\t", label ? label : L"?", scrollY);
        std::wstring l = line;
        l += heard;
        l += L"\r\n";
        WriteLogLine(l);
    }

    // 测试脚本喂入标记：EVT<TAB>t=<tick><TAB>识别文本
    // 让自动化脚本能把 FOLLOW 采样对齐到「正在念 / 静音」的时间轴上
    void AppendEvtLog(const std::wstring& text)
    {
        if (!SpeechLogFile()) return;

        wchar_t head[80];
        swprintf_s(head, L"EVT\tt=%lu\t", (unsigned long)::GetTickCount());
        std::wstring l = head;
        l += text;
        l += L"\r\n";
        WriteLogLine(l);
    }

    // 跟随状态采样：FOLLOW<TAB>y=<滚动位置><TAB>v=<速度 px/s><TAB>paused=0/1
    // auto=0/1 是「自动滚动是否开着」——用来验证滚到页尾会自动停
    void AppendFollowLog(float y, float v, bool paused, float target, int anchor, bool fa, bool anim,
                         bool autoOn)
    {
        if (!SpeechLogFile()) return;

        wchar_t line[260];
        swprintf_s(line, L"FOLLOW\tt=%lu\ty=%.1f\tv=%.1f\tpaused=%d\ttg=%.0f\tan=%d\tfa=%d\tanim=%d\tauto=%d\r\n",
                   (unsigned long)::GetTickCount(), y, v, paused ? 1 : 0, target, anchor,
                   fa ? 1 : 0, anim ? 1 : 0, autoOn ? 1 : 0);
        WriteLogLine(line);
    }
}

int CControlDlg::S(int v) const
{
    // DPI 缩放 × 自适应缩放（防止高 DPI 小屏下底部按钮跑到屏幕外）
    const UINT effDpi = (UINT)(m_dpi * m_uiScale + 0.5);
    return ::MulDiv(v, (int)effDpi, 96);
}

void CControlDlg::CreateFonts()
{
    const wchar_t* face = L"Microsoft YaHei UI";
    m_fontUi.CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, face);
    m_fontSmall.CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, face);
    m_fontBold.CreateFontW(-S(13), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    m_fontTitle.CreateFontW(-S(19), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, face);
}

BOOL CControlDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 主窗口 / 任务栏图标（exe 文件图标来自 rc 里的同一份 res/app.ico）
    m_hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_APPICON));
    if (m_hIcon)
    {
        SetIcon(m_hIcon, TRUE);        // 大图标（Alt+Tab / 任务栏）
        SetIcon(m_hIcon, FALSE);       // 小图标（标题栏）
    }

    {
        CClientDC dc(this);
        m_dpi = (UINT)dc.GetDeviceCaps(LOGPIXELSX);
        if (m_dpi < 96) m_dpi = 96;
    }

    ApplyWindowCorner();               // 无边框窗口初始圆角

    m_s.Load();

    // 启动时一律不自动滚动：需要时按 Ctrl+Alt+P 手动开启，避免一打开就自己往下跑
    m_s.autoScroll = false;

    // 设计稿为 560 x 700 逻辑单位；若按当前 DPI 放大后超出屏幕工作区，则整体再缩小
    {
        CRect wa;
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        const double unit = m_dpi / 96.0;
        double sc = 1.0;
        if (unit > 0.01)
        {
            sc = min(sc, (wa.Height() - 80.0) / (kDesignH * unit));
            sc = min(sc, (wa.Width() - 40.0) / (kDesignW * unit));
        }
        m_uiScale = max(0.70, min(1.0, sc));
    }

    CreateFonts();
    m_brushBg.CreateSolidBrush(RGB(247, 248, 250));

    // 调整窗口到设计尺寸（有记录时用上次的位置和大小）
    const CRect rcPanel = CalcPanelRect();
    SetWindowPos(nullptr, rcPanel.left, rcPanel.top, rcPanel.Width(), rcPanel.Height(), SWP_NOZORDER);
    m_restorePos = CPoint(rcPanel.left, rcPanel.top);   // 首次显示后还要再校正一次，见 OnShowWindow

    SetWindowText(L"芸豆提词器 · 教学模式");
    ModifyStyle(0, WS_CLIPCHILDREN);   // 拖动边框改大小时少闪一点
    BuildUi();
    SwitchPage(m_page);

    m_overlay.SetSettings(&m_s);
    m_overlay.SetDoc(&m_doc);
    m_overlay.SetSmartFollow(m_s.smartFollow);
    m_overlay.SetToolbarCmd([this](int id) { OnToolbarBtn(id); });
    m_overlay.Create();
    m_overlay.SetInteractive(!m_s.locked);
    m_overlay.ShowOverlay(m_s.visible);

    m_matcher.Reset(&m_doc.Normalized());

    DragAcceptFiles(TRUE);
    RegisterHotkeys();

    // 恢复上次文稿
    if (!m_s.lastScriptPath.empty())
    {
        std::wstring err;
        if (m_doc.LoadFile(m_s.lastScriptPath, err))
        {
            ApplyScriptText(m_doc.Raw(), m_s.lastScriptPath.substr(m_s.lastScriptPath.find_last_of(L"\\/") + 1));
        }
    }

    SyncUiFromSettings();
    RefreshRecentList();

    // 测试钩子：TP_SCRIPT=<路径> 启动后直接载入这篇稿子（txt / docx / pptx 都行）
    {
        wchar_t path[MAX_PATH] = { 0 };
        if (::GetEnvironmentVariableW(L"TP_SCRIPT", path, MAX_PATH) > 0)
            LoadScriptFile(path);
    }

    // 测试钩子：TP_ANCHOR=<原文索引> 直接把高亮锚点放到指定字，便于截图核对逐字边界
    {
        wchar_t buf[32] = { 0 };
        if (::GetEnvironmentVariableW(L"TP_ANCHOR", buf, 32) > 0)
        {
            const int orig = _wtoi(buf);
            if (orig > 0 && orig < m_doc.CharCount())
            {
                m_matcher.SetPos(m_doc.OrigToNorm(orig));
                m_overlay.Reanchor(orig);
            }
        }
    }

    SetTimer(2, 60, nullptr);

    // 自动化测试钩子：TP_VOICE_TEST="800:下一页;2400:上一页" → 到点模拟识别回调
    {
        wchar_t buf[512] = { 0 };
        if (::GetEnvironmentVariableW(L"TP_VOICE_TEST", buf, 512) > 0)
            LoadVoiceTestScript(buf);
        else
        {
            // 长脚本走文件：每行 "毫秒:文本"（UTF-8），避免环境变量 512 字符上限
            wchar_t path[MAX_PATH] = { 0 };
            if (::GetEnvironmentVariableW(L"TP_VOICE_TEST_FILE", path, MAX_PATH) > 0)
            {
                std::ifstream in(path, std::ios::binary);
                if (in)
                {
                    std::string raw((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
                    if (raw.size() > 3 && (unsigned char)raw[0] == 0xEF) raw.erase(0, 3);   // BOM
                    const int n = ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(),
                                                        nullptr, 0);
                    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
                    if (n > 0)
                        ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &w[0], n);
                    for (auto& c : w) if (c == L'\r' || c == L'\n') c = L';';
                    LoadVoiceTestScript(w.c_str());
                }
            }
        }
    }
    return TRUE;
}

void CControlDlg::LoadVoiceTestScript(const wchar_t* spec)
{
    m_voiceTest.clear();
    const std::wstring s(spec ? spec : L"");

    size_t p = 0;
    while (p < s.size())
    {
        size_t q = s.find(L';', p);
        const std::wstring item = s.substr(p, (q == std::wstring::npos) ? std::wstring::npos : q - p);

        const size_t colon = item.find(L':');
        if (colon != std::wstring::npos && colon + 1 < item.size())
        {
            VoiceTestItem it;
            it.at = (DWORD)_wtoi(item.substr(0, colon).c_str());
            it.text = item.substr(colon + 1);
            m_voiceTest.push_back(it);
        }
        if (q == std::wstring::npos) break;
        p = q + 1;
    }

    if (!m_voiceTest.empty())
    {
        // 测试环境没有麦克风：直接置开关，不启动引擎；两个功能是否生效由
        // TP_VOICE_TEST_CMD / TP_VOICE_TEST_FOLLOW（0/1）分别控制
        wchar_t v[8] = { 0 };
        ::GetEnvironmentVariableW(L"TP_VOICE_TEST_CMD", v, 8);
        m_cmdOn = (v[0] != L'0');

        ::GetEnvironmentVariableW(L"TP_VOICE_TEST_FOLLOW", v, 8);
        m_followOn = (v[0] == L'1');

        UpdateSpeechButtons();
        RefreshSpeechStatus();

        m_voiceTestStart = ::GetTickCount();
        SetTimer(3, 100, nullptr);
    }
}

void CControlDlg::AddLayout(CWnd* wnd, const CRect& rcDesign, int page,
                            bool stretchW, bool bottom, bool stretchH, bool slideW)
{
    LayoutItem it;
    it.wnd = wnd;
    it.rc = rcDesign;
    it.page = page;
    it.stretchW = stretchW;
    it.slideW = slideW;
    it.bottom = bottom;
    it.stretchH = stretchH;
    m_layout.push_back(it);
}

// 按当前客户区大小重排所有控件，并按当前页签决定显隐
void CControlDlg::ApplyLayout()
{
    if (!GetSafeHwnd() || m_layout.empty()) return;

    CRect cr;
    GetClientRect(&cr);
    const int extraW = cr.Width() - S(kDesignW);
    const int extraH = cr.Height() - S(kDesignH);

    for (size_t i = 0; i < m_layout.size(); ++i)
    {
        LayoutItem& it = m_layout[i];
        if (!it.wnd || !it.wnd->GetSafeHwnd()) continue;

        CRect r(S(it.rc.left), S(it.rc.top), S(it.rc.right), S(it.rc.bottom));
        if (it.stretchW) r.right += extraW;
        if (it.slideW)   { r.left += extraW; r.right += extraW; }
        if (it.stretchH) r.bottom += extraH;
        if (it.bottom) { r.top += extraH; r.bottom += extraH; }

        it.wnd->MoveWindow(r);

        const bool show = (it.page == 0 || it.page == m_page);
        const bool visible = (it.wnd->GetStyle() & WS_VISIBLE) != 0;
        if (visible != show)
            it.wnd->ShowWindow(show ? SW_SHOWNA : SW_HIDE);
    }
}

void CControlDlg::SwitchPage(int page)
{
    if (page < 1 || page > 4) return;
    m_page = page;
    ApplyLayout();
    if (m_tabScript.GetSafeHwnd())   m_tabScript.Invalidate();
    if (m_tabVoice.GetSafeHwnd())    m_tabVoice.Invalidate();
    if (m_tabSettings.GetSafeHwnd()) m_tabSettings.Invalidate();
    if (m_tabAbout.GetSafeHwnd())    m_tabAbout.Invalidate();
}

void CControlDlg::OnTabClick(UINT nID)
{
    if (nID == IDC_TAB_VOICE)         SwitchPage(3);
    else if (nID == IDC_TAB_SETTINGS) SwitchPage(2);
    else if (nID == IDC_TAB_ABOUT)    SwitchPage(4);
    else                              SwitchPage(1);
}

// 计算面板落点与尺寸：有记录就用记录值，再夹到最近显示器的工作区
// （换显示器 / 改分辨率 / 上次是最大化状态时，都不会跑到屏幕外或大到超出屏幕）
CRect CControlDlg::CalcPanelRect() const
{
    // 最小尺寸 = 设计稿尺寸，与 OnGetMinMaxInfo 保持一致
    CRect rcMin(0, 0, S(kDesignW), S(kDesignH));
    ::AdjustWindowRectEx(&rcMin, GetStyle(), FALSE, GetExStyle());
    int w = rcMin.Width(), h = rcMin.Height();
    int px = (::GetSystemMetrics(SM_CXSCREEN) - w) / 2;   // 没记录过时的默认摆放
    int py = (::GetSystemMetrics(SM_CYSCREEN) - h) / 3;

    if (m_s.panelPosValid)  { px = m_s.panelPos.x;  py = m_s.panelPos.y; }
    if (m_s.panelSizeValid)
    {
        w = max(w, (int)m_s.panelSize.cx);
        h = max(h, (int)m_s.panelSize.cy);
    }

    POINT anchor = { px + w / 2, py + 40 };
    MONITORINFO mi = { sizeof(mi) };
    if (::GetMonitorInfoW(::MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi))
    {
        const RECT& wa = mi.rcWork;
        w  = min(w, (int)(wa.right - wa.left));
        h  = min(h, (int)(wa.bottom - wa.top));
        px = max((int)wa.left, min((int)(wa.right - w), px));
        py = max((int)wa.top,  min((int)(wa.bottom - h), py));
    }
    return CRect(px, py, px + w, py + h);
}

// 记录面板位置与尺寸（拖动、缩放、退出时调用），随 m_s.Save() 落盘，下次启动恢复。
// 只在窗口真正可见时记，避免初始化/隐藏期间的中间值把记录冲掉；
// 最大化/最小化时取「还原后」的矩形，下次启动仍是原来的窗口大小。
void CControlDlg::RecordPanelGeometry()
{
    if (!::IsWindowVisible(GetSafeHwnd())) return;
    CRect rc;
    if (::IsIconic(GetSafeHwnd()) || ::IsZoomed(GetSafeHwnd()))
    {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        if (!::GetWindowPlacement(GetSafeHwnd(), &wp)) return;
        rc = wp.rcNormalPosition;
    }
    else
    {
        GetWindowRect(&rc);
    }
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
    m_s.panelPos = { rc.left, rc.top };
    m_s.panelPosValid = true;
    m_s.panelSize = { rc.Width(), rc.Height() };
    m_s.panelSizeValid = true;
}

void CControlDlg::OnMove(int x, int y)
{
    CDialogEx::OnMove(x, y);
    RecordPanelGeometry();
}

void CControlDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CDialogEx::OnShowWindow(bShow, nStatus);
    // 对话框第一次显示时，按记录再校正一次位置和尺寸：
    // ① 落点正好是 (0,0) 时，系统的对话框管理会把窗口挪到屏幕居中；
    // ② 换 DPI / 改分辨率后，记录的尺寸要按新的工作区复核。
    if (bShow && !m_panelPosApplied)
    {
        m_panelPosApplied = true;
        if (m_s.panelPosValid || m_s.panelSizeValid)
        {
            const CRect want = CalcPanelRect();
            SetWindowPos(nullptr, want.left, want.top, want.Width(), want.Height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            m_restorePos = CPoint(want.left, want.top);
        }
    }
}

void CControlDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    RecordPanelGeometry();             // 拖边框改大小时同步记住尺寸
    if (m_layout.empty())
    {
        // 布局还没建好（初始 SetWindowPos 到设计尺寸时）也必须同步圆角/区域，
        // 否则 OnInitDialog 早期按模板尺寸设置的裁剪区域会一直残留，把窗口右下角裁掉
        ApplyWindowCorner();
        return;
    }
    ApplyLayout();
    // 无边框窗口：还原态圆角、最大化铺满（最大化目标已在 GETMINMAXINFO 改为工作区）
    ApplyWindowCorner();
    // 连同背景一起立即重绘：控件挪走后原位置留下的旧像素必须擦掉，否则出现“残影”
    RedrawWindow(nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CControlDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CDialogEx::OnGetMinMaxInfo(lpMMI);
    // 最小尺寸 = 设计稿尺寸，再小就没法排版了
    CRect rc(0, 0, S(kDesignW), S(kDesignH));
    ::AdjustWindowRectEx(&rc, GetStyle(), FALSE, GetExStyle());
    lpMMI->ptMinTrackSize.x = rc.Width();
    lpMMI->ptMinTrackSize.y = rc.Height();

    // WS_POPUP 无边框窗口最大化会铺满整个显示器（连任务栏一起盖住），
    // 这里把最大化目标尺寸/位置改为显示器工作区，最大化后不挡任务栏
    HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (mon && ::GetMonitorInfoW(mon, &mi))
    {
        lpMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
        lpMMI->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
        lpMMI->ptMaxSize.x     = mi.rcWork.right - mi.rcWork.left;
        lpMMI->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
    }
}

// ===================== 无边框窗口（参考 mfcwnd 工程） =====================

// 标题带右上角三个按钮：0=最小化 1=最大化/还原 2=关闭（客户区坐标，已按 DPI 缩放）
CRect CControlDlg::CaptionButtonRect(int i) const
{
    const int bw = S(36), bh = S(28), gy = S(9);   // 尺寸同样要走 S() 缩放
    // 锚定实际客户区右上角（右边距与整体版式一致），窗口拉伸/最大化时按钮跟随
    CRect rcClient;
    GetClientRect(&rcClient);
    const int right = rcClient.Width() - S(16);
    const int x = right - (3 - i) * bw;
    return CRect(x, gy, x + bw, gy + bh);
}

int CControlDlg::CaptionHitTest(CPoint ptClient) const
{
    for (int i = 0; i < 3; ++i)
        if (CaptionButtonRect(i).PtInRect(ptClient))
            return i;
    return -1;
}

void CControlDlg::InvalidateCaption()
{
    // 注意：RedrawWindow 的矩形是客户区坐标，不要 ClientToScreen
    // 宽度取实际客户区宽：窗口拉宽后按钮位置随之变化，失效区域必须覆盖到
    CRect rcClient;
    GetClientRect(&rcClient);
    CRect rc(0, 0, rcClient.Width(), S(kCaptionH));
    RedrawWindow(&rc, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

// 边缘命中测试支持拉伸；标题带返回 HTCAPTION 交给系统拖动
// （系统拖动顺带获得：双击标题带切换最大化、拖到屏幕边 Aero Snap）
LRESULT CControlDlg::OnNcHitTest(CPoint point)
{
    CRect rcWin;
    GetWindowRect(&rcWin);
    const int edge = S(6);

    const bool l = point.x <= rcWin.left + edge;
    const bool r = point.x >= rcWin.right - edge;
    const bool t = point.y <= rcWin.top + edge;
    const bool b = point.y >= rcWin.bottom - edge;
    if (t && l) return HTTOPLEFT;
    if (t && r) return HTTOPRIGHT;
    if (b && l) return HTBOTTOMLEFT;
    if (b && r) return HTBOTTOMRIGHT;
    if (t) return HTTOP;
    if (b) return HTBOTTOM;
    if (l) return HTLEFT;
    if (r) return HTRIGHT;

    CPoint pt = point - rcWin.TopLeft();
    if (pt.y < S(kCaptionH))
    {
        // 三个标题按钮自己处理点击；其余区域给系统拖动
        if (CaptionHitTest(pt) >= 0) return HTCLIENT;
        return HTCAPTION;
    }
    return CDialogEx::OnNcHitTest(point);
}

// 无边框窗口不需要系统画非客户区激活边框
BOOL CControlDlg::OnNcActivate(BOOL bActive)
{
    InvalidateCaption();
    return TRUE;
}

// 自绘右上角三个标题按钮（最小化 / 最大化(还原) / 关闭），并画黑色外边框
void CControlDlg::OnPaint()
{
    CPaintDC dc(this);
    for (int i = 0; i < 3; ++i) DrawCaptionButton(dc, i);

    // 黑色外边框（客户区即整个窗口）；四条 FillRect 保证四边厚度一致，
    // 厚度随 DPI 取整（96dpi=1px，200%=2px），不受 m_uiScale 影响
    CRect rc;
    GetClientRect(&rc);
    const int bw = max(1, (int)((double)m_dpi / 96.0 + 0.5));
    dc.FillSolidRect(0, 0, rc.Width(), bw, RGB(0, 0, 0));                      // 上
    dc.FillSolidRect(0, rc.Height() - bw, rc.Width(), bw, RGB(0, 0, 0));       // 下
    dc.FillSolidRect(0, 0, bw, rc.Height(), RGB(0, 0, 0));                     // 左
    dc.FillSolidRect(rc.Width() - bw, 0, bw, rc.Height(), RGB(0, 0, 0));       // 右
}

void CControlDlg::DrawCaptionButton(CDC& dc, int i)
{
    const CRect rc = CaptionButtonRect(i);
    const bool hover = (m_capHover == i);
    const bool down  = (m_capPressed == i && hover);

    // 悬停/按下背景：关闭键红色，其它中性灰
    if (down || hover)
    {
        const COLORREF bg = (i == 2) ? (down ? RGB(0xC2, 0x0E, 0x1E) : RGB(0xE8, 0x11, 0x23))
                                     : (down ? RGB(0xD8, 0xDD, 0xE4) : RGB(0xE6, 0xEA, 0xF0));
        CBrush br(bg);
        dc.FillRect(rc, &br);
    }

    const COLORREF fg = (i == 2 && (hover || down)) ? RGB(255, 255, 255)
                                                    : RGB(0x4A, 0x52, 0x5C);
    CPen pen(PS_SOLID, S(1), fg);
    CPen* oldPen = dc.SelectObject(&pen);
    CBrush* oldBr = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.SetBkMode(TRANSPARENT);

    const CRect rcInner = rc;
    if (i == 0)          // 最小化：底部一条横线
    {
        const int y = rcInner.CenterPoint().y + S(4);
        dc.MoveTo(rcInner.CenterPoint().x - S(5), y);
        dc.LineTo(rcInner.CenterPoint().x + S(5), y);
    }
    else if (i == 1)     // 最大化 / 还原
    {
        if (!::IsZoomed(m_hWnd))
        {
            dc.Rectangle(rcInner.CenterPoint().x - S(5), rcInner.CenterPoint().y - S(5),
                         rcInner.CenterPoint().x + S(5), rcInner.CenterPoint().y + S(5));
        }
        else
        {
            // 还原：前后两个小方框
            dc.Rectangle(rcInner.CenterPoint().x - S(5), rcInner.CenterPoint().y - S(2),
                         rcInner.CenterPoint().x + S(2), rcInner.CenterPoint().y + S(5));
            CRect back(rcInner.CenterPoint().x - S(2), rcInner.CenterPoint().y - S(5),
                       rcInner.CenterPoint().x + S(5), rcInner.CenterPoint().y + S(2));
            dc.FillSolidRect(back, RGB(247, 248, 250));
            dc.Rectangle(back);
        }
    }
    else                 // 关闭：×
    {
        dc.MoveTo(rcInner.CenterPoint().x - S(5), rcInner.CenterPoint().y - S(5));
        dc.LineTo(rcInner.CenterPoint().x + S(5), rcInner.CenterPoint().y + S(5));
        dc.MoveTo(rcInner.CenterPoint().x + S(5), rcInner.CenterPoint().y - S(5));
        dc.LineTo(rcInner.CenterPoint().x - S(5), rcInner.CenterPoint().y + S(5));
    }

    dc.SelectObject(oldPen);
    dc.SelectObject(oldBr);
}

void CControlDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    const int btn = (point.y < S(kCaptionH)) ? CaptionHitTest(point) : -1;
    if (btn >= 0)
    {
        m_capPressed = btn;
        SetCapture();
        InvalidateCaption();
        return;
    }
    CDialogEx::OnLButtonDown(nFlags, point);
}

void CControlDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_capPressed >= 0)
    {
        const int btn = (point.y < S(kCaptionH)) ? CaptionHitTest(point) : -1;
        const int pressed = m_capPressed;
        m_capPressed = -1;
        if (GetCapture() == this) ReleaseCapture();
        InvalidateCaption();
        if (btn == pressed)
        {
            // 最小化 / 关闭都等于「隐藏面板」：窗口收进托盘，浮层和语音跟读继续工作，
            // 不会打断正在上的课。真要退出用面板里的「退出」按钮、托盘右键或 Ctrl+Alt+Q。
            if (btn == 0 || btn == 2) OnHidePanel();
            else if (btn == 1) ::SendMessageW(m_hWnd, WM_SYSCOMMAND,
                                              ::IsZoomed(m_hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
        }
        return;
    }
    CDialogEx::OnLButtonUp(nFlags, point);
}

void CControlDlg::OnMouseMove(UINT nFlags, CPoint point)
{
    const int h = (point.y < S(kCaptionH)) ? CaptionHitTest(point) : -1;
    if (h != m_capHover)
    {
        m_capHover = h;
        InvalidateCaption();
    }
    if (!m_capTracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        if (::TrackMouseEvent(&tme)) m_capTracking = true;
    }
    CDialogEx::OnMouseMove(nFlags, point);
}

void CControlDlg::OnMouseLeave()
{
    m_capTracking = false;
    if (m_capHover != -1 || m_capPressed != -1)
    {
        m_capHover = -1;
        m_capPressed = -1;
        InvalidateCaption();
    }
}

// 窗口圆角：Win11 用 DWM 圆角（保留阴影），失败则退回 SetWindowRgn（参考 mfcwnd）
void CControlDlg::ApplyWindowCorner()
{
    typedef HRESULT(WINAPI* PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    static PFN_DwmSetWindowAttribute pfn = []() -> PFN_DwmSetWindowAttribute
    {
        HMODULE dwm = ::GetModuleHandleW(L"dwmapi.dll");
        return dwm ? (PFN_DwmSetWindowAttribute)::GetProcAddress(dwm, "DwmSetWindowAttribute")
                   : nullptr;
    }();
    if (pfn)
    {
        // DWMWA_WINDOW_CORNER_PREFERENCE = 33；DWMWCP_ROUND=2，最大化时 DONOTROUND=1
        const int pref = ::IsZoomed(m_hWnd) ? 1 : 2;
        if (SUCCEEDED(pfn(m_hWnd, 33, &pref, sizeof(pref))))
        {
            if (m_rgnApplied)       // 清掉回退路径残留的旧区域，否则窗口被旧区域裁剪
            {
                ::SetWindowRgn(m_hWnd, nullptr, TRUE);
                m_rgnApplied = false;
            }
            return;
        }
    }

    if (::IsZoomed(m_hWnd))     // rgn 回退路径：最大化铺满，不设圆角
    {
        ::SetWindowRgn(m_hWnd, nullptr, TRUE);
        m_rgnApplied = false;
        return;
    }

    CRect rc;
    GetWindowRect(&rc);
    const int r = S(10);
    HRGN hRgn = ::CreateRoundRectRgn(0, 0, rc.Width() + 1, rc.Height() + 1, r, r);
    if (hRgn)
    {
        ::SetWindowRgn(m_hWnd, hRgn, TRUE);   // 区域由系统接管，不要 DeleteObject
        m_rgnApplied = true;
    }
}

// 左侧竖排页签：自绘（选中=白色卡片 + 左侧高亮竖条）
void CControlDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis)
{
    if (nIDCtl == IDC_ST_QR)
    {
        if (!dis) return;
        CDC dcQr;
        dcQr.Attach(dis->hDC);
        DrawQrBox(dcQr, dis->rcItem);
        dcQr.Detach();
        return;
    }

    if (nIDCtl != IDC_TAB_SCRIPT && nIDCtl != IDC_TAB_VOICE &&
        nIDCtl != IDC_TAB_SETTINGS && nIDCtl != IDC_TAB_ABOUT)
    {
        CDialogEx::OnDrawItem(nIDCtl, dis);
        return;
    }
    if (!dis || dis->itemID == (UINT)-1) return;

    CDC dc;
    dc.Attach(dis->hDC);

    CRect rc = dis->rcItem;
    const int tabPage = (nIDCtl == IDC_TAB_SCRIPT)   ? 1
                      : (nIDCtl == IDC_TAB_VOICE)    ? 3
                      : (nIDCtl == IDC_TAB_SETTINGS) ? 2 : 4;
    const bool sel = (m_page == tabPage);
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    dc.FillSolidRect(rc, RGB(247, 248, 250));

    CRect card = rc;
    card.DeflateRect(0, 2);

    CBrush brSel(RGB(255, 255, 255));
    CBrush brHover(RGB(238, 241, 246));
    CPen penBorder(PS_SOLID, 1, RGB(224, 228, 234));
    CBrush* oldBr = dc.SelectObject(sel ? &brSel : &brHover);
    CPen*   oldPen = dc.SelectObject(&penBorder);
    dc.RoundRect(card, CPoint(10, 10));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBr);

    if (sel)
    {
        // 左侧高亮竖条
        CRect bar(card.left + 2, card.top + 10, card.left + 5, card.bottom - 10);
        dc.FillSolidRect(bar, RGB(58, 132, 246));
    }

    const wchar_t* txt = (nIDCtl == IDC_TAB_SCRIPT)   ? L"稿件"
                       : (nIDCtl == IDC_TAB_VOICE)    ? L"语音"
                       : (nIDCtl == IDC_TAB_SETTINGS) ? L"设置" : L"关于";
    CFont* oldFont = dc.SelectObject(sel ? &m_fontBold : &m_fontUi);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(sel ? RGB(24, 30, 40) : RGB(108, 116, 128));
    CRect tr = card;
    if (pressed) tr.OffsetRect(0, 1);
    dc.DrawText(txt, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    dc.SelectObject(oldFont);
    dc.Detach();
}

// 关于页的二维码块：程序目录有 wechat.png 就等比画出来，没有就画一个占位说明
void CControlDlg::DrawQrBox(CDC& dc, const CRect& rc)
{
    dc.FillSolidRect(rc, RGB(247, 248, 250));

    CBrush brCard(RGB(255, 255, 255));
    CPen   penBorder(PS_SOLID, 1, RGB(224, 228, 234));
    CBrush* oldBr  = dc.SelectObject(&brCard);
    CPen*   oldPen = dc.SelectObject(&penBorder);
    dc.RoundRect(rc, CPoint(S(10), S(10)));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBr);

    if (!m_qrTried)
    {
        m_qrTried = true;
        if (!m_qrPath.empty() &&
            ::GetFileAttributesW(m_qrPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(m_qrPath.c_str());
            if (bmp->GetLastStatus() == Gdiplus::Ok && bmp->GetWidth() > 0 && bmp->GetHeight() > 0)
                m_qrBmp = bmp;
            else
                delete bmp;
        }
    }

    CRect inner = rc;
    inner.DeflateRect(S(7), S(7));

    if (m_qrBmp)
    {
        const int side = min(inner.Width(), inner.Height());
        const int x = inner.left + (inner.Width() - side) / 2;
        const int y = inner.top + (inner.Height() - side) / 2;

        Gdiplus::Graphics g(dc.GetSafeHdc());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(m_qrBmp, Gdiplus::Rect(x, y, side, side));
        return;
    }

    // 占位：虚线框 + 一句说明，提示怎么换成真二维码
    CPen penDash(PS_DASH, 1, RGB(205, 211, 220));
    oldPen = dc.SelectObject(&penDash);
    CRect dash = inner;
    dc.MoveTo(dash.left, dash.top);
    dc.LineTo(dash.right, dash.top);
    dc.LineTo(dash.right, dash.bottom);
    dc.LineTo(dash.left, dash.bottom);
    dc.LineTo(dash.left, dash.top);
    dc.SelectObject(oldPen);

    CFont* oldFont = dc.SelectObject(&m_fontSmall);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(152, 159, 170));
    dc.DrawText(L"二维码位置\n\n把 contact.png\n放进程序目录\n这里就会显示",
                inner, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    dc.SelectObject(oldFont);
}

void CControlDlg::BuildUi()
{
    m_layout.clear();

    auto mkBtn = [&](CButton& b, int id, const wchar_t* txt, int x, int y, int w, int h,
                     int page, bool stretchW = false)
    {
        b.Create(txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                 CRect(S(x), S(y), S(x + w), S(y + h)), this, id);
        b.SetFont(&m_fontUi);
        AddLayout(&b, CRect(x, y, x + w, y + h), page, stretchW);
    };
    auto mkLabel = [&](CStatic& st, int id, const wchar_t* txt, int x, int y, int w, int h,
                       CFont* f, int page, bool stretchW = false, bool stretchH = false,
                       DWORD style = SS_LEFT)
    {
        st.Create(txt, style | WS_CHILD | WS_VISIBLE,
                  CRect(S(x), S(y), S(x + w), S(y + h)), this, id);
        st.SetFont(f ? f : &m_fontUi);
        AddLayout(&st, CRect(x, y, x + w, y + h), page, stretchW, false, stretchH);
    };

    // ---- 顶部标题（两页共用） ----
    m_stTitle.Create(L"芸豆提词器", WS_CHILD | WS_VISIBLE,
                     CRect(S(16), S(12), S(300), S(42)), this, 900);
    m_stTitle.SetFont(&m_fontTitle);
    AddLayout(&m_stTitle, CRect(16, 12, 300, 42), 0);

    m_stSubtitle.Create(L"热键：Ctrl+Alt+H 浮层 · M 面板 · V 跟读 · C 命令 · Q 退出",
                        WS_CHILD | WS_VISIBLE, CRect(S(16), S(44), S(624), S(62)), this, 901);
    m_stSubtitle.SetFont(&m_fontSmall);
    AddLayout(&m_stSubtitle, CRect(16, 44, 624, 62), 0, true);

    // ---- 左侧竖排页签：稿件 / 语音 / 设置 ----
    m_tabScript.Create(L"稿件", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                       CRect(S(kTabX), S(kTab1Y), S(kTabX + kTabW), S(kTab1Y + kTabH)),
                       this, IDC_TAB_SCRIPT);
    m_tabScript.SetFont(&m_fontBold);
    AddLayout(&m_tabScript, CRect(kTabX, kTab1Y, kTabX + kTabW, kTab1Y + kTabH), 0);

    m_tabVoice.Create(L"语音", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                      CRect(S(kTabX), S(kTab2Y), S(kTabX + kTabW), S(kTab2Y + kTabH)),
                      this, IDC_TAB_VOICE);
    m_tabVoice.SetFont(&m_fontUi);
    AddLayout(&m_tabVoice, CRect(kTabX, kTab2Y, kTabX + kTabW, kTab2Y + kTabH), 0);

    m_tabSettings.Create(L"设置", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                         CRect(S(kTabX), S(kTab3Y), S(kTabX + kTabW), S(kTab3Y + kTabH)),
                         this, IDC_TAB_SETTINGS);
    m_tabSettings.SetFont(&m_fontUi);
    AddLayout(&m_tabSettings, CRect(kTabX, kTab3Y, kTabX + kTabW, kTab3Y + kTabH), 0);

    m_tabAbout.Create(L"关于", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                      CRect(S(kTabX), S(kTab4Y), S(kTabX + kTabW), S(kTab4Y + kTabH)),
                      this, IDC_TAB_ABOUT);
    m_tabAbout.SetFont(&m_fontUi);
    AddLayout(&m_tabAbout, CRect(kTabX, kTab4Y, kTabX + kTabW, kTab4Y + kTabH), 0);

    // ================= 页 1：稿件 =================
    mkLabel(m_sec1, 902, L"①  稿件", kCx, 76, 220, 20, &m_fontBold, 1);

    constexpr int kBtn3W = (kCw - 2 * 12) / 3;          // 一行三个按钮均分
    mkBtn(m_btnOpen, IDC_BTN_OPEN, L"添加稿件", kCx, 102, kBtn3W, 32, 1);
    mkBtn(m_btnClip, IDC_BTN_CLIPBOARD, L"从剪贴板", kCx + kBtn3W + 12, 102, kBtn3W, 32, 1);
    mkBtn(m_btnSample, IDC_BTN_SAMPLE, L"示例文稿", kCx + 2 * (kBtn3W + 12), 102, kBtn3W, 32, 1, true);

    mkLabel(m_stScript, IDC_ST_SCRIPT, L"还没有稿件 · 点「添加稿件」，或把 txt / docx / pptx 拖到本窗口",
            kCx, 146, kCw, 40, &m_fontSmall, 1, true);

    // 最近文稿：与浮层顶部「切换稿件」共用同一份列表，双击直接载入
    mkLabel(m_secRecent, 906, L"②  最近文稿", kCx, 200, 200, 20, &m_fontBold, 1);
    mkLabel(m_stRecentHint, 907, L"双击列表里的文稿即可载入", kCx, 200, kCw, 20,
            &m_fontSmall, 1, true, false, SS_RIGHT);

    m_lstRecent.Create(this, IDC_LST_RECENT, CRect(S(kCx), S(230), S(kCx + kCw), S(560)));
    m_lstRecent.SetFonts(&m_fontUi, &m_fontBold, &m_fontSmall);
    m_lstRecent.SetScale(m_dpi / 96.0 * m_uiScale);
    m_lstRecent.SetOnOpen([this](int idx) { OnRecentOpen(idx); });
    m_lstRecent.SetOnDelete([this](int idx) { OnRecentDelete(idx); });
    m_lstRecent.SetOnOpenFolder([this](int idx) { OnRecentOpenFolder(idx); });
    m_lstRecent.SetOnGoto([this](int idx) { OnRecentGoto(idx); });
    AddLayout(&m_lstRecent, CRect(kCx, 230, kCx + kCw, 560), 1, true, false, true);

    // ================= 页 3：语音 =================
    mkLabel(m_sec2, 903, L"②  语音（跟读 / 命令 相互独立）", kCx, 76, 380, 20, &m_fontBold, 3);

    mkBtn(m_btnSpeech, IDC_BTN_SPEECH, L"开始跟读", kCx, 102, 120, 34, 3);

    m_cmbLang.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                     CRect(S(kCx + 132), S(104), S(kCx + 292), S(164)), this, IDC_CMB_LANG);
    m_cmbLang.SetFont(&m_fontUi);
    m_cmbLang.AddString(L"中文（普通话）");
    m_cmbLang.AddString(L"英语（美国）");
    m_cmbLang.AddString(L"系统默认");
    m_cmbLang.SetItemData(0, 0x0804);
    m_cmbLang.SetItemData(1, 0x0409);
    m_cmbLang.SetItemData(2, 0);
    AddLayout(&m_cmbLang, CRect(kCx + 132, 104, kCx + 292, 164), 3);

    // 识别引擎：默认走离线模型（系统自带的中文听写错字太多，只作兼容兜底）
    m_cmbEngine.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                       CRect(S(kCx + 304), S(104), S(kCx + kCw), S(164)), this, IDC_CMB_ENGINE);
    m_cmbEngine.SetFont(&m_fontUi);
    m_cmbEngine.AddString(CSpeechEngine::OfflineModelAvailable() ? L"离线识别" : L"离线识别（缺模型）");
    m_cmbEngine.AddString(L"系统识别");
    // 缺模型时下拉仍可用：可以临时切「系统识别」兜底，选离线项会引导下载
    AddLayout(&m_cmbEngine, CRect(kCx + 304, 104, kCx + kCw, 164), 3, true);

    // 麦克风选择：会议软件用「默认通信设备」，SAPI 默认用「默认录音设备」，
    // 两者常常不是同一个麦克风，所以必须让用户能明确指定。
    mkLabel(m_stMicHint, 908, L"麦克风（会议软件一般用「默认通信设备」）",
            kCx, 170, kCw, 18, &m_fontSmall, 3, true);
    m_cmbMic.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                    CRect(S(kCx), S(192), S(kCx + kCw), S(332)), this, IDC_CMB_MIC);
    m_cmbMic.SetFont(&m_fontUi);
    AddLayout(&m_cmbMic, CRect(kCx, 192, kCx + kCw, 332), 3, true);

    mkLabel(m_stLevel, IDC_ST_LEVEL, L"", kCx, 222, kCw, 18, &m_fontSmall, 3, true);
    mkLabel(m_stSpeech, IDC_ST_SPEECH, L"未启动语音识别", kCx, 242, kCw, 18, &m_fontSmall, 3, true,
            false, SS_LEFT | SS_NOTIFY);
    mkLabel(m_stHeard, IDC_ST_HEARD, L"", kCx, 262, kCw, 46, &m_fontSmall, 3, true);

    // 语音命令：与跟读完全独立的一路开关（共用麦克风，但各自启停、互不影响）
    mkBtn(m_btnCmd, IDC_BTN_VOICECMD, L"打开语音命令", kCx, 322, 140, 28, 3);
    mkLabel(m_stCmdHint, IDC_ST_CMDHINT, L"说「下一页 / 上一页」翻页", kCx + 152, 326, kCw - 152, 18,
            &m_fontSmall, 3, true);

    mkLabel(m_stNote, IDC_ST_NOTE,
            L"跟读：照稿子念，读到哪滚到哪，停读自动暂停（速度由语速决定）。\n"
            L"命令：说「下一页 / 上一页」「回到顶部」，与跟读各自独立启停。\n"
            L"识别不准：先看「输入电平」有没有跳动 —— 不动就是麦克风选错了，\n"
            L"或系统「隐私和安全性 > 麦克风」没允许桌面应用访问。\n"
            L"没有麦克风也能用：Ctrl+Alt+P 开自动滚动，速度在「设置」页调。",
            kCx, 360, kCw, 120, &m_fontSmall, 3, true, true);

    // ================= 页 2：设置 =================
    mkLabel(m_sec3, 904, L"③  显示调节", kCx, 76, 220, 20, &m_fontBold, 2);

    auto mkRow = [&](CStatic& lbl, CStatic& val, CSliderCtrl& sl, int idLbl, int idVal, int idSl,
                     const wchar_t* name, int y, int lo, int hi, int pos)
    {
        lbl.Create(name, WS_CHILD | WS_VISIBLE, CRect(S(kCx), S(y + 2), S(kCx + 62), S(y + 24)), this, idLbl);
        lbl.SetFont(&m_fontUi);
        AddLayout(&lbl, CRect(kCx, y + 2, kCx + 62, y + 24), 2);

        sl.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                  CRect(S(kCx + 64), S(y), S(kCx + 358), S(y + 26)), this, idSl);
        sl.SetRange(lo, hi);
        sl.SetPos(pos);
        sl.SetFont(&m_fontUi);
        AddLayout(&sl, CRect(kCx + 64, y, kCx + 358, y + 26), 2, true);

        val.Create(L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                   CRect(S(kCx + 364), S(y + 4), S(kCx + kCw), S(y + 24)), this, idVal);
        val.SetFont(&m_fontSmall);
        // 数值文本整体随窗口右移（不能 stretchW：会把右半段滑块盖住）
        AddLayout(&val, CRect(kCx + 364, y + 4, kCx + kCw, y + 24), 2, false, false, false, true);
    };

    mkRow(m_stFont, m_valFont, m_slFont, IDC_ST_FONT, IDC_VAL_FONT, IDC_SL_FONT, L"字号", 102, 16, 120, m_s.fontSize);
    mkRow(m_stSpacing, m_valSpacing, m_slSpacing, IDC_ST_SPACING, IDC_VAL_SPACING, IDC_SL_SPACING, L"行距", 136, 100, 300, m_s.lineSpacing);
    mkRow(m_stAlpha, m_valAlpha, m_slAlpha, IDC_ST_ALPHA, IDC_VAL_ALPHA, IDC_SL_ALPHA, L"不透明度", 170, 0, 100, m_s.bgAlpha * 100 / 255);
    mkRow(m_stSpeed, m_valSpeed, m_slSpeed, IDC_ST_SPEED, IDC_VAL_SPEED, IDC_SL_SPEED, L"滚动速度", 204, 1, 10, m_s.autoSpeed);

    m_chkHighlight.Create(L"高亮当前行", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                          CRect(S(kCx), S(238), S(kCx + 120), S(260)), this, IDC_CHK_HIGHLIGHT);
    m_chkMirror.Create(L"文字镜像", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                       CRect(S(kCx + 124), S(238), S(kCx + 244), S(260)), this, IDC_CHK_MIRROR);
    m_chkShadow.Create(L"阴影描边", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                       CRect(S(kCx + 248), S(238), S(kCx + 368), S(260)), this, IDC_CHK_SHADOW);
    m_chkHighlight.SetFont(&m_fontUi);
    m_chkMirror.SetFont(&m_fontUi);
    m_chkShadow.SetFont(&m_fontUi);
    AddLayout(&m_chkHighlight, CRect(kCx, 238, kCx + 164, 260), 2);
    AddLayout(&m_chkMirror, CRect(kCx + 172, 238, kCx + 336, 260), 2);
    AddLayout(&m_chkShadow, CRect(kCx + 344, 238, kCx + 508, 260), 2);

    mkBtn(m_btnColor, IDC_BTN_COLOR, L"文字颜色", kCx, 272, kBtn3W, 30, 2);
    mkBtn(m_btnHiColor, IDC_BTN_HICOLOR, L"高亮颜色", kCx + kBtn3W + 12, 272, kBtn3W, 30, 2);
    mkBtn(m_btnBgColor, IDC_BTN_BGCOLOR, L"背景颜色", kCx + 2 * (kBtn3W + 12), 272, kBtn3W, 30, 2, true);

    mkLabel(m_sec4, 905, L"④  浮层控制", kCx, 322, 330, 20, &m_fontBold, 2);

    constexpr int kBtn4W = (kCw - 3 * 8) / 4;           // 一行四个按钮均分
    mkBtn(m_btnShow, IDC_BTN_SHOW, L"隐藏浮层", kCx, 348, kBtn4W, 32, 2);
    mkBtn(m_btnLock, IDC_BTN_LOCK, L"调整位置", kCx + kBtn4W + 8, 348, kBtn4W, 32, 2);
    mkBtn(m_btnFull, IDC_BTN_FULLSCREEN, L"铺满", kCx + 2 * (kBtn4W + 8), 348, kBtn4W, 32, 2);
    mkBtn(m_btnReset, IDC_BTN_RESETPOS, L"重置", kCx + 3 * (kBtn4W + 8), 348, kBtn4W, 32, 2);

    mkBtn(m_btnWMinus, IDC_BTN_W_MINUS, L"宽 −", kCx, 388, kBtn4W, 30, 2);
    mkBtn(m_btnWPlus, IDC_BTN_W_PLUS, L"宽 ＋", kCx + kBtn4W + 8, 388, kBtn4W, 30, 2);
    mkBtn(m_btnHMinus, IDC_BTN_H_MINUS, L"高 −", kCx + 2 * (kBtn4W + 8), 388, kBtn4W, 30, 2);
    mkBtn(m_btnHPlus, IDC_BTN_H_PLUS, L"高 ＋", kCx + 3 * (kBtn4W + 8), 388, kBtn4W, 30, 2);

    m_chkPass.Create(L"鼠标穿透（关闭后浮层可用滚轮 / 点击）", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                     CRect(S(kCx), S(426), S(kCx + kCw), S(448)), this, IDC_CHK_PASSTHRU);
    m_chkPass.SetFont(&m_fontUi);
    AddLayout(&m_chkPass, CRect(kCx, 426, kCx + kCw, 448), 2, true);

    m_stTips.Create(
        L"私密模式：浮层不进任务栏、不抢焦点，并已在录屏 / 屏幕共享 / 截图中隐藏。\n"
        L"鼠标穿透默认开启（不挡下面的点击）；勾掉后浮层可用滚轮 / 按住拖动内容。\n"
        L"改大小：「调整位置」下拖动边缘或四角，或用按钮 / 热键微调。\n"
        L"顶部按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动 / 切换稿件。\n"
        L"以下均带 Ctrl+Alt+ 前缀：↑↓ 滚动　←→ 字号　Shift+←→ 调宽　Shift+↑↓ 调高\n"
        L"Shift+M 镜像　P 自动滚动　Home 回顶部　M 面板显隐",
        WS_CHILD | WS_VISIBLE, CRect(S(kCx), S(458), S(kCx + kCw), S(640)), this, IDC_ST_TIPS);
    m_stTips.SetFont(&m_fontSmall);
    AddLayout(&m_stTips, CRect(kCx, 458, kCx + kCw, 560), 2, true, false, true);

    // ================= 页 4：关于 =================
    mkLabel(m_secAbout, 909, L"关于", kCx, 76, 200, 20, &m_fontBold, 4);

    mkLabel(m_stAppName, 910, L"芸豆提词器 · 桌面提词工具", kCx, 104, kCw, 24,
            &m_fontBold, 4, true);
    mkLabel(m_stAboutDesc, 911,
            L"给讲课、直播、录课用的桌面提词器。浮层不抢焦点、不进任务栏，\n"
            L"并已在录屏 / 屏幕共享 / 截图中隐藏。支持 txt / docx / pptx，\n"
            L"可以语音跟读（读到哪滚到哪），也可以说「下一页」翻页。",
            kCx, 132, kCw, 60, &m_fontSmall, 4, true);

    mkLabel(m_secContact, 912, L"作者邮箱", kCx, 208, 200, 20, &m_fontBold, 4);

    // 邮箱本身可点：点一下用系统默认邮件客户端开一封新信（mailto:）
    m_stContact.Create(m_s.authorEmail.c_str(), SS_LEFT | SS_NOTIFY | WS_CHILD | WS_VISIBLE,
                       CRect(S(kCx), S(238), S(kCx + 340), S(272)), this, IDC_ST_CONTACT);
    m_stContact.SetFont(&m_fontTitle);
    AddLayout(&m_stContact, CRect(kCx, 238, kCx + 340, 272), 4);

    mkBtn(m_btnCopyContact, IDC_BTN_COPY_CONTACT, L"复制邮箱", kCx, 282, 120, 30, 4);
    mkLabel(m_stContactHint, 913,
            L"点上面的邮箱地址，会用系统邮件客户端直接开一封新信。\n"
            L"报 bug、提需求、要批量授权，直接发邮件就行。",
            kCx, 322, 340, 44, &m_fontSmall, 4);

    // 二维码：程序目录放了 contact.png 就显示图片，没有就显示占位说明
    {
        wchar_t exe[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p(exe);
        const size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos) m_qrPath = p.substr(0, slash) + L"\\contact.png";
    }
    m_stQr.Create(L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                  CRect(S(484), S(230), S(624), S(370)), this, IDC_ST_QR);
    AddLayout(&m_stQr, CRect(484, 230, 624, 370), 4);

    mkLabel(m_stAboutNote, 914,
            L"版本 v1.2.1 · 绿色免安装，配置与模型放在程序目录或 %APPDATA%\\TeacherPrompter。\n"
            L"离线语音模型在第一次跟读时自动下载（约 740MB），可暂停，下次接着下。",
            kCx, 392, kCw, 60, &m_fontSmall, 4, true, true);

    // ================= 底部（两页共用，钉在窗口底部） =================
    mkBtn(m_btnQuit, IDC_BTN_QUIT, L"退出", 16, 586, 100, 30, 0);
    mkBtn(m_btnHidePanel, IDC_BTN_HIDE_PANEL, L"隐藏面板", 124, 586, 110, 30, 0);
    m_btnQuit.SetFont(&m_fontUi);
    m_btnHidePanel.SetFont(&m_fontUi);
    // 这两个按钮要跟着窗口底边走
    for (size_t i = 0; i < m_layout.size(); ++i)
        if (m_layout[i].wnd == &m_btnQuit || m_layout[i].wnd == &m_btnHidePanel)
            m_layout[i].bottom = true;

    m_stVersion.Create(L"v1.2.1 · MFC 芸豆提词器", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                       CRect(S(240), S(652), S(544), S(672)), this, IDC_ST_VERSION);
    m_stVersion.SetFont(&m_fontSmall);
    AddLayout(&m_stVersion, CRect(240, 592, 624, 612), 0, true, true);

    RefreshMicList();
}

// 稿件页的「最近文稿」列表：与浮层顶部「切换稿件」同源（AppSettings::recentScripts）
void CControlDlg::RefreshRecentList()
{
    if (!m_lstRecent.GetSafeHwnd()) return;

    std::vector<CRecentListWnd::Item> items;
    items.reserve(m_s.recentScripts.size());
    for (const auto& p : m_s.recentScripts)
    {
        CRecentListWnd::Item it;
        it.path = p;

        const size_t slash = p.find_last_of(L"\\/");
        it.name = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
        it.dir  = (slash == std::wstring::npos) ? p : p.substr(0, slash);

        it.current = (p == m_s.lastScriptPath);
        it.missing = (::GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES);
        it.preset  = m_s.IsPresetRecent(p);

        // 内容预览：文件前 160 字符（按修改时间缓存，避免每次刷新都解包 docx/pptx）
        if (!it.missing)
        {
            ULONGLONG mtime = 0;
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (::GetFileAttributesExW(p.c_str(), ::GetFileExInfoStandard, &fad))
                mtime = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32)
                      | fad.ftLastWriteTime.dwLowDateTime;

            auto f = m_previewCache.find(p);
            if (f != m_previewCache.end() && f->second.first == mtime)
            {
                it.preview = f->second.second;
            }
            else
            {
                it.preview = CScriptDoc::PreviewFile(p, 160);
                m_previewCache[p] = { mtime, it.preview };
            }
        }
        items.push_back(std::move(it));
    }
    m_lstRecent.SetItems(std::move(items));

    m_stRecentHint.SetWindowText(m_s.recentScripts.empty()
                                     ? L"双击载入"
                                     : L"双击载入（不改变顺序） · 行底「去提词」直开浮层 ·「×」移除");
}

// 双击 / 回车：与「去提词」一致 —— 载入这一条并确保浮层显示，不改变最近列表顺序
void CControlDlg::OnRecentOpen(int idx)
{
    if (idx < 0 || idx >= (int)m_s.recentScripts.size()) return;

    const std::wstring path = m_s.recentScripts[idx];
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ::MessageBoxW(GetSafeHwnd(), (L"文件已不存在：\n" + path).c_str(), L"打开失败", MB_ICONWARNING);
        RefreshRecentList();
        return;
    }
    LoadScriptFile(path, /*touchRecent=*/false);
    EnsureOverlayVisible();
}

// 行内「打开文件夹」：在资源管理器中定位该文件
void CControlDlg::OnRecentOpenFolder(int idx)
{
    if (idx < 0 || idx >= (int)m_s.recentScripts.size()) return;

    const std::wstring path = m_s.recentScripts[idx];
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        // 文件没了就退而打开其所在目录（目录也可能没了，explorer 自己会报错）
        const size_t slash = path.find_last_of(L"\\/");
        const std::wstring dir = (slash == std::wstring::npos) ? path : path.substr(0, slash);
        ::ShellExecuteW(GetSafeHwnd(), L"open", L"explorer.exe", dir.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    // /select 定位到文件本身
    std::wstring param = L"/select,\"" + path + L"\"";
    ::ShellExecuteW(GetSafeHwnd(), L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
}

// 行内「去提词」：载入这条稿件并确保浮层显示（不改变最近列表顺序）
void CControlDlg::OnRecentGoto(int idx)
{
    if (idx < 0 || idx >= (int)m_s.recentScripts.size()) return;
    LoadScriptFile(m_s.recentScripts[idx], /*touchRecent=*/false);
    EnsureOverlayVisible();     // 浮层没开就自动打开
}

// 点「×」/ Delete 键：从列表移除
//   · 程序自己记录过的 → 从 sidecar 删掉，永久生效
//   · 来自 INI [file] 的预设 → 只在本次运行隐藏（INI 是用户定义，程序不改写）
void CControlDlg::OnRecentDelete(int idx)
{
    if (idx < 0 || idx >= (int)m_s.recentScripts.size()) return;

    const std::wstring path = m_s.recentScripts[idx];
    const bool preset = m_s.IsPresetRecent(path);

    m_s.RemoveRecentScript(path);
    if (preset) m_s.HidePresetRecent(path);

    RefreshRecentList();
    SetStatus(preset
                  ? L"已从列表隐藏（该条目写在 INI 的 [file] 段，程序不修改 INI；要永久删除请编辑 INI）"
                  : L"已从最近文稿中移除");
}

void CControlDlg::SyncUiFromSettings()
{
    m_slFont.SetPos(m_s.fontSize);
    m_slSpacing.SetPos(m_s.lineSpacing);
    m_slAlpha.SetPos(m_s.bgAlpha * 100 / 255);
    m_slSpeed.SetPos(m_s.autoSpeed);

    wchar_t buf[64];
    swprintf_s(buf, L"%d px", m_s.fontSize);
    m_valFont.SetWindowText(buf);
    swprintf_s(buf, L"%d%%", m_s.lineSpacing);
    m_valSpacing.SetWindowText(buf);
    swprintf_s(buf, L"%d%%", m_s.bgAlpha * 100 / 255);
    m_valAlpha.SetWindowText(buf);
    if (m_followOn)
        m_valSpeed.SetWindowText(L"跟随语音");   // 跟读时速度由语速决定
    else
    {
        swprintf_s(buf, L"%d 级", m_s.autoSpeed);
        m_valSpeed.SetWindowText(buf);
    }

    m_chkHighlight.SetCheck(m_s.highlightCur ? BST_CHECKED : BST_UNCHECKED);
    m_chkMirror.SetCheck(m_s.mirror ? BST_CHECKED : BST_UNCHECKED);
    m_chkShadow.SetCheck(m_s.shadow ? BST_CHECKED : BST_UNCHECKED);
    m_chkPass.SetCheck(m_s.clickThrough ? BST_CHECKED : BST_UNCHECKED);

    m_btnShow.SetWindowText(m_s.visible ? L"隐藏浮层" : L"显示浮层");
    m_btnLock.SetWindowText(m_s.locked ? L"调整位置" : L"完成锁定");

    int idx = 0;
    for (int i = 0; i < m_cmbLang.GetCount(); ++i)
    {
        if ((int)m_cmbLang.GetItemData(i) == m_s.langId) { idx = i; break; }
    }
    m_cmbLang.SetCurSel(idx);

    if (m_cmbEngine.GetSafeHwnd())
        m_cmbEngine.SetCurSel(m_s.asrEngine == 1 ? 1 : 0);

    for (int i = 0; i < (int)m_micIds.size(); ++i)
    {
        const bool match = m_s.inputDeviceId.empty() ? (i == 0) : (m_micIds[i] == m_s.inputDeviceId);
        if (match) { m_cmbMic.SetCurSel(i); break; }
    }

    UpdateSizeLabel();
    UpdateLevelLabel();
    m_overlay.MarkDirty();
}

void CControlDlg::RefreshMicList()
{
    if (!m_cmbMic.GetSafeHwnd()) return;

    m_micIds.clear();
    m_cmbMic.ResetContent();

    // 第 0 项：自动（默认通信设备优先）
    m_cmbMic.AddString(L"自动（优先默认通信设备 = 会议软件用的麦克风）");
    m_micIds.push_back(L"");

    const auto devices = CSpeechEngine::ListDevices();
    for (const auto& d : devices)
    {
        std::wstring label = d.name;
        if (d.isDefaultComm)      label += L"　[默认通信设备]";
        else if (d.isDefaultConsole) label += L"　[默认录音设备]";
        m_cmbMic.AddString(label.c_str());
        m_micIds.push_back(d.id);
    }

    // 按设置里的设备 ID 选中
    int sel = 0;
    for (size_t i = 0; i < m_micIds.size(); ++i)
    {
        if (!m_s.inputDeviceId.empty() && m_micIds[i] == m_s.inputDeviceId) { sel = (int)i; break; }
    }
    m_cmbMic.SetCurSel(sel);
}

void CControlDlg::OnMicChanged()
{
    const int i = m_cmbMic.GetCurSel();
    if (i < 0 || i >= (int)m_micIds.size()) return;
    m_s.inputDeviceId = m_micIds[i];

    CString name;
    m_cmbMic.GetLBText(i, name);

    if (m_speech.IsRunning())
    {
        // 运行中换设备：自动重启识别
        RestartSpeechEngine();
    }
    else
    {
        SetStatus(std::wstring(L"已选择麦克风：") + (LPCWSTR)name);
    }
}

void CControlDlg::OnLangChanged()
{
    const int i = m_cmbLang.GetCurSel();
    if (i < 0) return;
    m_s.langId = (int)m_cmbLang.GetItemData(i);
    RestartSpeechEngine();
}

void CControlDlg::OnEngineChanged()
{
    const int i = m_cmbEngine.GetCurSel();
    if (i < 0) return;
    m_s.asrEngine = (i == 1) ? 1 : 0;

    // 选了离线但模型还没装：直接把下载引导出来（装好后自动切回，不用再动手）
    if (m_s.asrEngine == 0 && !CSpeechEngine::OfflineModelAvailable())
    {
        StartModelDownload(false);
        m_cmbEngine.SetCurSel(1);          // 先落到「系统识别」，避免设置与实际不符
        m_s.asrEngine = 1;
        return;
    }

    if (m_speech.IsRunning())
    {
        RestartSpeechEngine();
    }
    else
    {
        SetStatus(m_s.asrEngine == 0
                      ? L"离线识别：本地运行，中文准确率高（首次加载约几秒）"
                      : L"系统识别：兼容模式，中文准确率明显偏低");
    }
}

void CControlDlg::UpdateLevelLabel()
{
    if (!m_stLevel.GetSafeHwnd()) return;

    std::wstring text;
    if (!m_speech.IsRunning())
    {
        text = L"输入电平 —（未启动语音识别）";
    }
    else
    {
        const int lv = m_speech.InputLevel();
        wchar_t bar[32] = { 0 };
        const int cells = 12;
        int filled = lv * cells / 100;
        if (filled > cells) filled = cells;
        for (int i = 0; i < cells; ++i)
            bar[i] = (i < filled) ? L'▮' : L'▯';

        wchar_t buf[400];
        if (lv <= 1)
            swprintf_s(buf, L"输入电平 %s 0%%　无声（确认上方麦克风选对了，并对着它说话）", bar);
        else
            swprintf_s(buf, L"输入电平 %s %d%%　%s", bar, lv, m_speech.InputName().c_str());
        text = buf;
    }

    if (text == m_levelTextCache) return;
    m_levelTextCache = text;
    m_stLevel.SetWindowText(text.c_str());
}

void CControlDlg::UpdateSizeLabel()
{
    if (!m_sec4.GetSafeHwnd() || !m_overlay.GetSafeHwnd()) return;
    const CRect rc = m_overlay.CurrentRect();
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
    if (rc == m_lastOverlayRect) return;
    m_lastOverlayRect = rc;

    wchar_t buf[96];
    swprintf_s(buf, L"④  浮层控制　　%d × %d", rc.Width(), rc.Height());
    m_sec4.SetWindowText(buf);
}

void CControlDlg::RegisterHotkeys()
{
    struct KeyDef { int id; UINT vk; UINT mods; };
    const UINT base = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    const UINT shifted = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
    const KeyDef keys[] = {
        { HK_TOGGLE_SHOW,      'H',        base },
        { HK_TOGGLE_SPEECH,    'V',        base },
        { HK_TOGGLE_CMD,       'C',        base },
        { HK_TOGGLE_LOCK,      'L',        base },
        { HK_SCROLL_UP,        VK_UP,      base },
        { HK_SCROLL_DOWN,      VK_DOWN,    base },
        { HK_FONT_UP,          VK_RIGHT,   base },
        { HK_FONT_DOWN,        VK_LEFT,    base },
        { HK_TOGGLE_MIRROR,    'M',        shifted },
        { HK_TOGGLE_AUTOSCROLL,'P',        base },
        { HK_GOTO_TOP,         VK_HOME,    base },
        { HK_TOGGLE_HIGHLIGHT, 'B',        base },
        { HK_SIZE_W_INC,       VK_RIGHT,   shifted },
        { HK_SIZE_W_DEC,       VK_LEFT,    shifted },
        { HK_SIZE_H_INC,       VK_DOWN,    shifted },
        { HK_SIZE_H_DEC,       VK_UP,      shifted },
        { HK_TOGGLE_PANEL,     'M',        base },
        { HK_QUIT,             'Q',        base },
    };

    m_hotkeyCount = 0;
    for (const auto& k : keys)
    {
        if (::RegisterHotKey(GetSafeHwnd(), k.id, k.mods, k.vk))
            ++m_hotkeyCount;
    }
}

void CControlDlg::UnregisterHotkeys()
{
    for (int id = HK_TOGGLE_SHOW; id <= HK_QUIT; ++id)
        ::UnregisterHotKey(GetSafeHwnd(), id);
    m_hotkeyCount = 0;
}

void CControlDlg::SetStatus(const std::wstring& s)
{
    if (!m_stSpeech.GetSafeHwnd()) return;
    if (s == m_statusText) return;     // 避免定时器重复刷新
    m_statusText = s;
    m_stSpeech.SetWindowText(s.c_str());
}

void CControlDlg::ApplyScriptText(const std::wstring& text, const std::wstring& label)
{
    m_doc.SetText(text);
    m_matcher.Reset(&m_doc.Normalized());
    m_overlay.SetDoc(&m_doc);
    m_overlay.ScrollToTop();

    wchar_t buf[512];
    swprintf_s(buf, L"%s · %d 字", label.c_str(), m_doc.CharCount());
    m_stScript.SetWindowText(buf);
}

void CControlDlg::LoadScriptFile(const std::wstring& path, bool touchRecent)
{
    std::wstring err;
    if (!m_doc.LoadFile(path, err))
    {
        ::MessageBoxW(GetSafeHwnd(), err.c_str(), L"打开失败", MB_ICONWARNING);
        return;
    }
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    ApplyScriptText(m_doc.Raw(), name);
    m_s.lastScriptPath = path;

    // 记进「最近文稿」（浮层顶部「切换稿件」列表的数据源）。
    // 只写 sidecar（%APPDATA%\TeacherPrompter\recent.ini），绝不改写主 INI 的 [file] 段 ——
    // 那是用户手写的稿件定义（lastScript / recentN），程序只读。
    // touchRecent=false（「去提词」）：只载入，不重排最近列表。
    if (touchRecent)
    {
        m_s.AddRecentScript(path);
        m_s.SaveRecentStore();
    }
    RefreshRecentList();

    SetStatus(OfficeDoc::IsOfficeFile(path)
                  ? L"Office 文档已载入（已提取文字），可以开始跟读"
                  : L"文稿已载入，可以开始跟读");
}

void CControlDlg::OnOpenScript()
{
    CFileDialog dlg(TRUE, L"txt", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"文稿与 Office 文档 (*.txt;*.md;*.docx;*.pptx)|*.txt;*.md;*.markdown;*.docx;*.docm;*.pptx;*.pptm|Word 文档 (*.docx;*.docm)|*.docx;*.docm|PPT (*.pptx;*.pptm)|*.pptx;*.pptm|字幕 (*.srt)|*.srt|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"添加稿件（txt / md / docx / pptx）";
    if (dlg.DoModal() == IDOK)
        LoadScriptFile(dlg.GetPathName().GetString());
}

void CControlDlg::OnClipboardScript()
{
    std::wstring text;
    if (::OpenClipboard(GetSafeHwnd()))
    {
        HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
        if (h)
        {
            const wchar_t* p = (const wchar_t*)::GlobalLock(h);
            if (p) { text = p; ::GlobalUnlock(h); }
        }
        ::CloseClipboard();
    }
    if (text.empty())
    {
        ::MessageBoxW(GetSafeHwnd(), L"剪贴板里没有可用的文本。", L"提示", MB_ICONINFORMATION);
        return;
    }
    ApplyScriptText(text, L"剪贴板文稿");
    m_s.lastScriptPath.clear();
    SetStatus(L"已从剪贴板载入文稿");
}

void CControlDlg::OnSampleScript()
{
    ApplyScriptText(kSampleScript, L"示例文稿");
    m_s.lastScriptPath.clear();
}

void CControlDlg::OnDropFiles(HDROP hDropInfo)
{
    wchar_t path[MAX_PATH] = { 0 };
    if (::DragQueryFileW(hDropInfo, 0, path, MAX_PATH) > 0)
        LoadScriptFile(path);
    ::DragFinish(hDropInfo);
}

void CControlDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (pScrollBar)
    {
        const HWND h = pScrollBar->GetSafeHwnd();
        if (h == m_slFont.GetSafeHwnd())
        {
            m_s.fontSize = m_slFont.GetPos();
        }
        else if (h == m_slSpacing.GetSafeHwnd())
        {
            m_s.lineSpacing = m_slSpacing.GetPos();
        }
        else if (h == m_slAlpha.GetSafeHwnd())
        {
            m_s.bgAlpha = m_slAlpha.GetPos() * 255 / 100;
        }
        else if (h == m_slSpeed.GetSafeHwnd())
        {
            m_s.autoSpeed = m_slSpeed.GetPos();
        }
        SyncUiFromSettings();
    }
    CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CControlDlg::OnCheckChanged(UINT nID)
{
    if (nID == IDC_CHK_HIGHLIGHT)     m_s.highlightCur = (m_chkHighlight.GetCheck() == BST_CHECKED);
    else if (nID == IDC_CHK_MIRROR)   m_s.mirror = (m_chkMirror.GetCheck() == BST_CHECKED);
    else if (nID == IDC_CHK_SHADOW)   m_s.shadow = (m_chkShadow.GetCheck() == BST_CHECKED);
    else if (nID == IDC_CHK_PASSTHRU)
    {
        m_s.clickThrough = (m_chkPass.GetCheck() == BST_CHECKED);
        m_overlay.SetClickThrough(m_s.clickThrough);
        SetStatus(m_s.clickThrough ? L"鼠标穿透已开启：浮层不挡下面的点击"
                                   : L"鼠标穿透已关闭：浮层上可直接用滚轮 / 点击");
        return;
    }
    m_overlay.MarkDirty();
}

void CControlDlg::OnPickTextColor()
{
    CColorDialog dlg(m_s.textColor, CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dlg.DoModal() == IDOK)
    {
        m_s.textColor = dlg.GetColor();
        m_overlay.MarkDirty();
    }
}

void CControlDlg::OnPickHiColor()
{
    // “高亮颜色”按钮复用文字颜色选择逻辑
    CColorDialog dlg(m_s.hiColor, CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dlg.DoModal() == IDOK)
    {
        m_s.hiColor = dlg.GetColor();
        m_overlay.MarkDirty();
    }
}

void CControlDlg::OnPickBgColor()
{
    CColorDialog dlg(m_s.bgColor, CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dlg.DoModal() == IDOK)
    {
        m_s.bgColor = dlg.GetColor();
        // 背景完全透明时选颜色看不出效果，顺手给一个能看见的底
        if (m_s.bgAlpha < 20)
        {
            m_s.bgAlpha = 90;
            SyncUiFromSettings();
        }
        m_overlay.MarkDirty();
    }
}

void CControlDlg::OnToggleSpeech()
{
    ToggleSpeech();
}

void CControlDlg::OnToggleCmd()
{
    ToggleCmd();
}

// 跟读 / 命令共用一路麦克风与识别引擎：任一功能打开就启动，
// 两个都关掉才真正停止采集（避免开两个识别器抢设备、白占一倍内存和 CPU）。
void CControlDlg::SyncSpeechEngine()
{
    const bool need = m_followOn || m_cmdOn;

    if (need && !m_speech.IsRunning())
    {
        if (!StartSpeechEngine())
        {
            m_followOn = false;
            m_cmdOn = false;
        }
    }
    else if (!need && m_speech.IsRunning())
    {
        m_speech.Stop();
        m_speechStartedTick = 0;
        if (m_stHeard.GetSafeHwnd()) m_stHeard.SetWindowText(L"");
        if (m_stLevel.GetSafeHwnd()) m_levelTextCache.clear();
    }

    UpdateSpeechButtons();
    RefreshSpeechStatus();
}

bool CControlDlg::StartSpeechEngine()
{
    // 离线模型是纯中文的：选英语时只能用系统引擎
    const bool nonChinese = (m_s.langId != 0 && m_s.langId != 0x0804);
    m_speech.SetBackend((m_s.asrEngine == 1 || nonChinese)
                            ? CSpeechEngine::Backend::Sapi
                            : CSpeechEngine::Backend::Auto);

    std::wstring err;
    if (!m_speech.Start(m_s.langId, m_s.inputDeviceId,
                        [this](const std::wstring& t, bool fin) { OnSpeechText(t, fin); }, err))
    {
        std::wstring msg = (err.empty() ? m_speech.LastError() : err) +
            L"\n\n排查建议：\n"
            L"1. 在「语音」页的麦克风下拉里选对设备（会议软件一般用「默认通信设备」）；\n"
            L"2. 离线识别需要 models 目录下的模型文件完整；系统识别需要装「中文(简体)」语音识别语言包；\n"
            L"3. 设置 > 隐私和安全性 > 麦克风 > 允许桌面应用访问麦克风；\n"
            L"4. 若会议软件独占麦克风，请在其音频设置里改用共享模式。\n"
            L"暂时不可用也没关系，可先用「自动滚动」模式（Ctrl+Alt+P）。";
        ::MessageBoxW(GetSafeHwnd(), msg.c_str(), L"语音识别不可用", MB_ICONWARNING);
        return false;
    }

    m_lastMatchTick = ::GetTickCount();
    m_speechStartedTick = ::GetTickCount();
    m_levelTextCache.clear();
    m_engineReadyNotified = false;
    return true;
}

void CControlDlg::RestartSpeechEngine()
{
    const bool wasRunning = m_speech.IsRunning();
    if (wasRunning) m_speech.Stop();
    if (m_followOn || m_cmdOn) SyncSpeechEngine();
    else RefreshSpeechStatus();
}

void CControlDlg::StopSpeech()
{
    m_followOn = false;
    m_cmdOn = false;
    if (m_speech.IsRunning()) m_speech.Stop();
    m_speechStartedTick = 0;
    if (m_stHeard.GetSafeHwnd()) m_stHeard.SetWindowText(L"");
    m_levelTextCache.clear();
    UpdateSpeechButtons();
    RefreshSpeechStatus();
}

void CControlDlg::ToggleSpeech()
{
    SetFollowOn(!m_followOn);
}

void CControlDlg::ToggleCmd()
{
    SetCmdOn(!m_cmdOn);
}

void CControlDlg::SetFollowOn(bool on)
{
    if (on && m_doc.Empty())
    {
        ::MessageBoxW(GetSafeHwnd(), L"请先添加稿件：点「添加稿件」或「从剪贴板」，也可以把 txt、docx、pptx 拖进窗口。",
                      L"还没有稿件", MB_ICONINFORMATION);
        return;
    }

    // 跟读要用离线模型，但它不随安装包一起发 —— 没装就先把下载引导出来
    const bool nonChinese = (m_s.langId != 0 && m_s.langId != 0x0804);
    if (on && m_s.asrEngine == 0 && !nonChinese && !CSpeechEngine::OfflineModelAvailable())
    {
        StartModelDownload(true);
        return;
    }

    m_followOn = on;
    m_lastMatchTick = ::GetTickCount();
    SyncSpeechEngine();
}

// ---------------------------------------------------------------- 离线模型按需下载

void CControlDlg::StartModelDownload(bool autoFollow)
{
    if (m_installer.Running()) return;

    if (CSpeechEngine::OfflineModelAvailable())
    {
        RefreshEngineCombo();
        if (autoFollow) SetFollowOn(true);
        return;
    }

    const std::wstring msg =
        L"离线语音模型没有随安装包一起发布，第一次用跟读需要联网下载。\n\n"
        L"下载内容：中文识别模型（sherpa-onnx zipformer xlarge int8，约 740 MB）\n"
        L"安装位置：" + CModelInstaller::InstallDir() + L"\n\n"
        L"下载期间可以正常用自动滚动提词，不需要一直等着。\n"
        L"如果下载很慢或断网，下次点这里会从断点继续。\n\n"
        L"现在开始下载吗？";

    if (::MessageBoxW(GetSafeHwnd(), msg.c_str(), L"下载语音模型",
                      MB_YESNO | MB_ICONINFORMATION) != IDYES)
        return;

    m_pendingFollowAfterDownload = autoFollow;
    m_installer.Start(GetSafeHwnd(), WM_APP_MODEL_EVENT);
    SetStatus(L"正在启动下载…");
}

bool CControlDlg::UpdateModelStatus()
{
    // 引擎已经跑起来（说明模型在），状态行交回识别逻辑
    if (m_speech.IsRunning()) return false;

    const bool installed = CSpeechEngine::OfflineModelAvailable();

    // 下载/解压线程刚刚结束：收尾一次（PostMessage 丢了也能靠定时器兜住）
    if (!m_installer.Running() && m_installer.StateNow() == CModelInstaller::State::Done &&
        !m_modelDoneTick)
    {
        HandleModelInstalled();
    }

    if (m_installer.Running())
    {
        std::wstring s = m_installer.Status();
        if (m_installer.StateNow() == CModelInstaller::State::Downloading)
            s += L"　（点这里暂停）";
        SetStatus(s);
        return true;
    }

    if (!installed)
    {
        if (m_installer.Failed())
            SetStatus(m_installer.Status() + L" · 点这里重试");
        else if (m_installer.Cancelled())
            SetStatus(L"下载已暂停 · 点这里继续（已下载的部分不会丢）");
        else
            SetStatus(L"离线语音模型未安装 · 点这里下载（约 740 MB，需联网）");
        return true;
    }

    if (m_modelDoneTick && ::GetTickCount() < m_modelDoneTick)
    {
        SetStatus(L"语音模型已安装 · 可以开始跟读了");
        return true;
    }
    m_modelDoneTick = 0;
    return false;
}

void CControlDlg::HandleModelInstalled()
{
    if (m_modelDoneTick) return;                              // 已经收过尾
    if (!CSpeechEngine::OfflineModelAvailable()) return;      // 装完但文件不齐，交给 Failed 分支

    RefreshEngineCombo();
    m_modelDoneTick = ::GetTickCount() + 8000;

    const bool autoFollow = m_pendingFollowAfterDownload;
    m_pendingFollowAfterDownload = false;

    if (autoFollow)
    {
        if (!m_doc.Empty())
        {
            SetFollowOn(true);                                // 用户点跟读时被拦下，装好后自动接上
            return;
        }
        ::MessageBoxW(GetSafeHwnd(),
                      L"语音模型已装好。现在添加一篇稿件，就可以点「开始跟读」了。",
                      L"语音模型安装完成", MB_ICONINFORMATION);
        return;
    }

    ::MessageBoxW(GetSafeHwnd(), L"语音模型已装好，可以开始跟读了。",
                  L"语音模型安装完成", MB_ICONINFORMATION);
}

void CControlDlg::RefreshEngineCombo()
{
    if (!m_cmbEngine.GetSafeHwnd()) return;

    const bool avail = CSpeechEngine::OfflineModelAvailable();
    const int cur = m_cmbEngine.GetCurSel();
    if (m_cmbEngine.GetCount() > 0)
        m_cmbEngine.DeleteString(0);
    m_cmbEngine.InsertString(0, avail ? L"离线识别" : L"离线识别（缺模型）");
    m_cmbEngine.EnableWindow(TRUE);      // 「系统识别」始终可选
    m_cmbEngine.SetCurSel(cur < 0 ? 0 : cur < 2 ? cur : 0);
}

void CControlDlg::OnModelStatusClick()
{
    if (m_installer.Running())
    {
        if (::MessageBoxW(GetSafeHwnd(),
                          L"要暂停下载吗？\n已下载的部分会保留，下次点状态行会接着下。",
                          L"暂停下载", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            m_installer.Cancel();
            SetStatus(L"正在暂停下载…");
        }
        return;
    }
    StartModelDownload(false);
}

LRESULT CControlDlg::OnModelEvent(WPARAM, LPARAM)
{
    UpdateModelStatus();
    return 0;
}


void CControlDlg::SetCmdOn(bool on)
{
    // 命令不要求先有稿件：没稿件时说命令只会提示「请先加载稿件」，识别本身照常起
    m_cmdOn = on;
    SyncSpeechEngine();
}

void CControlDlg::UpdateSpeechButtons()
{
    if (m_btnSpeech.GetSafeHwnd())
        m_btnSpeech.SetWindowText(m_followOn ? L"停止跟读" : L"开始跟读");
    if (m_btnCmd.GetSafeHwnd())
        m_btnCmd.SetWindowText(m_cmdOn ? L"关闭语音命令" : L"打开语音命令");
    if (m_stCmdHint.GetSafeHwnd())
        m_stCmdHint.SetWindowText(m_cmdOn ? L"已开启：说「下一页 / 上一页」翻页"
                                          : L"关闭状态：翻页命令不生效");

    // 跟读时滚动速度由语速决定，速度滑块只对「自动滚动」（无语音时的兜底）有效
    if (m_slSpeed.GetSafeHwnd()) m_slSpeed.EnableWindow(!m_followOn);
    if (m_valSpeed.GetSafeHwnd())
    {
        if (m_followOn) m_valSpeed.SetWindowText(L"跟随语音");
        else
        {
            wchar_t b[32];
            swprintf_s(b, L"%d 级", m_s.autoSpeed);
            m_valSpeed.SetWindowText(b);
        }
    }
}

std::wstring CControlDlg::ModePrefix() const
{
    if (m_followOn && m_cmdOn) return L"跟读 + 命令 · ";
    if (m_cmdOn)               return L"语音命令 · ";
    if (m_followOn)            return L"跟读中 · ";
    return L"";
}

void CControlDlg::RefreshSpeechStatus()
{
    std::wstring s;
    if (!m_speech.IsRunning())
        s = L"未启动语音识别";
    else if (m_speech.IsPreparing())
        s = L"正在加载离线识别模型（约 7 秒，首次可能更久）…";
    else
        s = ModePrefix() + m_speech.EngineName() + L" · 输入：" + m_speech.InputName();

    m_engineReadyNotified = m_speech.IsRunning() && m_speech.IsPreparing();
    m_levelTextCache.clear();
    SetStatus(s);
    UpdateLevelLabel();
}

void CControlDlg::OnSpeechText(const std::wstring& text, bool isFinal)
{
    (void)isFinal;
    if (m_stHeard.GetSafeHwnd()) m_stHeard.SetWindowText(text.c_str());

    // 语音命令：命中「下一页 / 上一页 / 回到开头」就当成控制指令执行，
    // 这一句不再参与跟读匹配，避免把跟读位置带偏
    if (m_cmdOn)
    {
        const wchar_t* label = nullptr;
        const VoiceCmd cmd = MatchVoiceCommand(text, &label);
        if (cmd != VoiceCmd::None && RunVoiceCommand(cmd, label, text))
            return;
    }

    // 只开了语音命令、没开跟读：普通句子直接忽略，不做匹配、不报未匹配
    if (!m_followOn) return;

    int pos = 0;
    float score = 0.0f;
    const bool matched = m_matcher.Feed(text, pos, score);
    AppendSpeechLog(text, matched, pos, (int)m_doc.Normalized().size(), score);

    if (matched)
    {
        m_lastMatchTick = ::GetTickCount();
        const int orig = m_doc.NormToOrig(pos);
        m_overlay.Reanchor(orig);

        wchar_t buf[160];
        swprintf_s(buf, L"跟读中 · 匹配度 %d%% · 已到第 %d / %d 字",
                   (int)(score * 100.0f), pos, (int)m_doc.Normalized().size());
        SetStatus(buf);
    }
    else if (::GetTickCount() - m_lastMatchTick > 8000)
    {
        SetStatus(L"跟读中 · 暂未匹配到稿件内容，请靠近麦克风或检查语言包");
    }
}

bool CControlDlg::RunVoiceCommand(VoiceCmd cmd, const wchar_t* label, const std::wstring& heard)
{
    if (cmd == VoiceCmd::None) return false;

    const DWORD now = ::GetTickCount();

    // 同一句话会先来中间结果、再来最终结果，1.2 秒内只认第一次，避免一次翻两页。
    // 期间返回 true：这句话仍然算命令，不能当成正文去匹稿。
    if (m_lastCmdTick && now - m_lastCmdTick < 1200)
        return true;

    if (m_doc.Empty())
    {
        SetStatus(L"语音命令：请先加载稿件");
        return true;
    }

    m_lastCmdTick = now;
    EnsureOverlayVisible();

    int topChar = -1;
    switch (cmd)
    {
    case VoiceCmd::PageDown:
        topChar = m_overlay.ScrollPage(+1);
        break;
    case VoiceCmd::PageUp:
        topChar = m_overlay.ScrollPage(-1);
        break;
    case VoiceCmd::ToTop:
        m_overlay.ScrollToTop();
        m_matcher.SetPos(0);
        topChar = 0;
        break;
    default:
        return false;
    }

    // 翻页后让跟读从新的一页开头继续：否则下一句匹配会把画面拽回原处
    if (topChar > 0 && cmd != VoiceCmd::ToTop)
        m_matcher.SetPos(m_doc.OrigToNorm(topChar));

    AppendCmdLog(label, m_overlay.ScrollY(), heard);

    std::wstring st = L"语音命令：";
    st += (label ? label : L"?");
    SetStatus(st);

    m_lastMatchTick = now;   // 刚翻过页，先别急着提示「未匹配」
    return true;
}

void CControlDlg::OnToggleShow()
{
    m_s.visible = !m_s.visible;
    m_overlay.ShowOverlay(m_s.visible);
    m_btnShow.SetWindowText(m_s.visible ? L"隐藏浮层" : L"显示浮层");
}

// 浮层顶部按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动
void CControlDlg::OnToolbarBtn(int id)
{
    EnsureOverlayVisible();
    switch (id)
    {
    case 0:     // 返回页首：跟读位置一并归零，下一句从开头匹配
        m_overlay.ScrollToTop();
        m_matcher.SetPos(0);
        m_lastMatchTick = ::GetTickCount();
        SetStatus(L"已回到页首");
        break;

    case 1:     // 到页尾：锚点落在最后一行行首，跟读从页尾继续
    {
        const int c = m_overlay.ScrollToBottom();
        if (c >= 0) m_matcher.SetPos(m_doc.OrigToNorm(c));
        m_lastMatchTick = ::GetTickCount();
        // 已经在页尾了，再保持自动滚动也没有意义 —— 直接停掉
        if (m_s.autoScroll) StopAutoScroll(L"已到页尾（自动滚动已停止）");
        else                SetStatus(L"已到页尾");
        break;
    }

    case 2:     // 滚动：开自动滚动（与语音跟读互斥，先把跟读关掉）
        if (m_followOn) SetFollowOn(false);
        m_s.autoScroll = true;
        SyncUiFromSettings();
        m_overlay.RefreshAuxBars();
        SetStatus(L"自动滚动已开启（速度用「滚动速度」滑块调）");
        break;

    case 3:     // 停止滚动：关自动滚动；跟读不动（它本来就「停读即暂停」）
        StopAutoScroll(m_followOn ? L"已停止自动滚动（语音跟读仍开启：停读即暂停）"
                                  : L"已停止滚动");
        break;

    case 4:     // 切换稿件：弹出最近文稿列表（点窗外 / 再点一次即收起）
        m_overlay.ToggleScriptMenu(
            m_s.recentScripts, m_s.lastScriptPath,
            [this](int idx)
            {
                if (idx >= 0 && idx < (int)m_s.recentScripts.size())
                    LoadScriptFile(m_s.recentScripts[idx]);
                else
                    OnOpenScript();          // 末尾那项「打开其他文稿…」
            });
        break;

    default:
        break;
    }
}

void CControlDlg::StopAutoScroll(const std::wstring& status)
{
    m_s.autoScroll = false;
    SyncUiFromSettings();
    m_overlay.RefreshAuxBars();   // 按钮条「滚动」高亮回落
    if (!status.empty()) SetStatus(status);
}

void CControlDlg::ToggleLock()
{
    m_s.locked = !m_s.locked;
    if (m_s.locked) ::GetWindowRect(m_overlay.GetSafeHwnd(), &m_s.overlayRect);
    m_overlay.SetInteractive(!m_s.locked);
    if (!m_s.visible)
    {
        m_s.visible = true;
        m_overlay.ShowOverlay(true);
        m_btnShow.SetWindowText(L"隐藏浮层");
    }
    m_btnLock.SetWindowText(m_s.locked ? L"调整位置" : L"完成锁定");
    SetStatus(m_s.locked ? L"浮层已锁定：鼠标可穿透"
                         : L"调整模式：拖动内部移动、拖动边缘或四角改大小，完成后点「完成锁定」");
}

void CControlDlg::OnToggleLock() { ToggleLock(); }

void CControlDlg::OnFullscreen()
{
    CRect work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    m_overlay.ApplyRect(work);
    ::GetWindowRect(m_overlay.GetSafeHwnd(), &m_s.overlayRect);
    m_s.visible = true;
    m_overlay.ShowOverlay(true);
    m_btnShow.SetWindowText(L"隐藏浮层");
    UpdateSizeLabel();
}

void CControlDlg::OnResetPos()
{
    m_overlay.ApplyDefaultRect();
    ::GetWindowRect(m_overlay.GetSafeHwnd(), &m_s.overlayRect);
    m_s.visible = true;
    m_overlay.ShowOverlay(true);
    m_btnShow.SetWindowText(L"隐藏浮层");
    UpdateSizeLabel();
}

void CControlDlg::OnSizeWMinus()
{
    EnsureOverlayVisible();
    m_overlay.ResizeStep(-1, 0);
    UpdateSizeLabel();
    SetStatus(L"浮层宽度已缩小");
}

void CControlDlg::OnSizeWPlus()
{
    EnsureOverlayVisible();
    m_overlay.ResizeStep(+1, 0);
    UpdateSizeLabel();
    SetStatus(L"浮层宽度已放大");
}

void CControlDlg::OnSizeHMinus()
{
    EnsureOverlayVisible();
    m_overlay.ResizeStep(0, -1);
    UpdateSizeLabel();
    SetStatus(L"浮层高度已缩小");
}

void CControlDlg::OnSizeHPlus()
{
    EnsureOverlayVisible();
    m_overlay.ResizeStep(0, +1);
    UpdateSizeLabel();
    SetStatus(L"浮层高度已放大");
}

void CControlDlg::EnsureOverlayVisible()
{
    if (!m_s.visible)
    {
        m_s.visible = true;
        m_overlay.ShowOverlay(true);
        if (m_btnShow.GetSafeHwnd()) m_btnShow.SetWindowText(L"隐藏浮层");
    }
}

// ---------------------------------------------------------------------------
//  主面板显示 / 隐藏 + 托盘图标
// ---------------------------------------------------------------------------

bool CControlDlg::IsPanelVisible() const
{
    if (!GetSafeHwnd()) return false;
    return ::IsWindowVisible(m_hWnd) != FALSE && ::IsIconic(m_hWnd) == FALSE;
}

void CControlDlg::AddTrayIcon()
{
    if (!GetSafeHwnd()) return;

    if (!m_hIcon) m_hIcon = CreateTrayIcon(32);
    if (!m_hIcon) m_hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAY;
    m_nid.hIcon = m_hIcon;
    lstrcpynW(m_nid.szTip, L"芸豆提词器 · 双击显示面板（Ctrl+Alt+M）", ARRAYSIZE(m_nid.szTip));

    // 静默上托盘：不加气泡提示，隐藏面板时不打扰用户

    if (::Shell_NotifyIconW(NIM_ADD, &m_nid)) { m_trayAdded = true; return; }
    if (::Shell_NotifyIconW(NIM_MODIFY, &m_nid)) { m_trayAdded = true; }
}

void CControlDlg::RemoveTrayIcon()
{
    if (!m_trayAdded) return;
    ::Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_trayAdded = false;
}

void CControlDlg::ShowPanel(bool show)
{
    if (!GetSafeHwnd() || m_closing) return;

    if (show)
    {
        if (::IsIconic(m_hWnd))
            ::ShowWindow(m_hWnd, SW_RESTORE);
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        ::BringWindowToTop(m_hWnd);

        // 热键触发时前台切换一般能成功；万一被系统拦下，闪烁任务栏提示
        if (::GetForegroundWindow() != m_hWnd)
        {
            FLASHWINFO fi = { sizeof(fi), m_hWnd, FLASHW_ALL, 3, 0 };
            ::FlashWindowEx(&fi);
        }

        RemoveTrayIcon();       // 面板回来了就不用常驻托盘，尽量不显眼
        RefreshSpeechStatus();
    }
    else
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
        RemoveTrayIcon();
        AddTrayIcon();          // 留一个静默的托盘入口，忘记热键也能找回来

        // 第一次隐藏时给个气泡：不然点 × 的人会以为程序已经退出、课也停了
        if (!m_s.trayHintShown)
        {
            m_s.trayHintShown = true;
            NOTIFYICONDATAW nid = m_nid;
            nid.uFlags = NIF_INFO;
            nid.dwInfoFlags = NIIF_INFO;
            lstrcpynW(nid.szInfoTitle, L"芸豆提词器已隐藏，仍在后台运行", ARRAYSIZE(nid.szInfoTitle));
            lstrcpynW(nid.szInfo,
                      L"提词浮层和语音跟读都还在。双击托盘图标，或按 Ctrl+Alt+M 调出面板；"
                      L"要退出请用托盘右键菜单或 Ctrl+Alt+Q。",
                      ARRAYSIZE(nid.szInfo));
            ::Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
    }
}

void CControlDlg::TogglePanel()
{
    ShowPanel(!IsPanelVisible());
}

void CControlDlg::OnHidePanel()
{
    ShowPanel(false);
}

// 关于页：把邮箱复制到剪贴板（最多试 5 次，别的程序占着剪贴板时不至于静默失败）
void CControlDlg::OnCopyContact()
{
    const std::wstring wx = m_s.authorEmail;
    if (wx.empty()) return;

    bool ok = false;
    for (int i = 0; i < 5 && !ok; ++i)
    {
        if (::OpenClipboard(GetSafeHwnd()))
        {
            ::EmptyClipboard();
            const size_t bytes = (wx.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                if (void* p = ::GlobalLock(h))
                {
                    memcpy(p, wx.c_str(), bytes);
                    ::GlobalUnlock(h);
                    if (::SetClipboardData(CF_UNICODETEXT, h)) ok = true;
                    else ::GlobalFree(h);
                }
                else
                {
                    ::GlobalFree(h);
                }
            }
            ::CloseClipboard();
        }
        if (!ok) ::Sleep(30);
    }

    if (ok)
    {
        // 面板上没有独立的提示行，直接让按钮自己反馈一下
        if (m_btnCopyContact.GetSafeHwnd())
        {
            m_btnCopyContact.SetWindowText(L"已复制 ✓");
            SetTimer(4, 1400, nullptr);
        }
    }
    else if (m_btnCopyContact.GetSafeHwnd())
    {
        m_btnCopyContact.SetWindowText(L"复制失败，请重试");
        SetTimer(4, 1800, nullptr);
    }
}

// 关于页：点邮箱 → 用系统默认邮件客户端开一封新信；打不开就退回「复制地址」
void CControlDlg::OnContactClick()
{
    const std::wstring mail = L"mailto:" + m_s.authorEmail;
    const HINSTANCE r = ::ShellExecuteW(GetSafeHwnd(), L"open", mail.c_str(),
                                        nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32)
        OnCopyContact();     // 没有邮件客户端时，至少把地址复制下来
}

void CControlDlg::ShowTrayMenu()
{
    CMenu menu;
    if (!menu.CreatePopupMenu()) return;

    const bool panelVisible = IsPanelVisible();
    menu.AppendMenu(MF_STRING, IDM_TRAY_SHOW_PANEL,
                    panelVisible ? L"隐藏控制面板\tCtrl+Alt+M" : L"显示控制面板\tCtrl+Alt+M");
    menu.AppendMenu(MF_STRING, IDM_TRAY_SHOW_OVERLAY,
                    m_s.visible ? L"隐藏提词浮层\tCtrl+Alt+H" : L"显示提词浮层\tCtrl+Alt+H");
    menu.AppendMenu(MF_STRING, IDM_TRAY_SPEECH,
                    m_followOn ? L"停止语音跟读\tCtrl+Alt+V" : L"开始语音跟读\tCtrl+Alt+V");
    menu.AppendMenu(MF_STRING, IDM_TRAY_CMD,
                    m_cmdOn ? L"关闭语音命令\tCtrl+Alt+C" : L"打开语音命令\tCtrl+Alt+C");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDM_TRAY_QUIT, L"退出程序\tCtrl+Alt+Q");
    menu.SetDefaultItem(IDM_TRAY_SHOW_PANEL);

    CPoint pt;
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(m_hWnd);   // 否则点别处菜单不会收起

    const UINT cmd = (UINT)menu.TrackPopupMenu(
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, this);
    ::PostMessage(m_hWnd, WM_NULL, 0, 0);

    switch (cmd)
    {
    case IDM_TRAY_SHOW_PANEL:    TogglePanel();    break;
    case IDM_TRAY_SHOW_OVERLAY:  OnToggleShow();   break;
    case IDM_TRAY_SPEECH:        ToggleSpeech();   break;
    case IDM_TRAY_CMD:           ToggleCmd();      break;
    case IDM_TRAY_QUIT:          SaveAndClose();   break;
    default: break;
    }
}

LRESULT CControlDlg::OnTrayMessage(WPARAM /*wParam*/, LPARAM lParam)
{
    switch (LOWORD(lParam))
    {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case NIN_BALLOONUSERCLICK:
        ShowPanel(true);
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowTrayMenu();
        break;
    default:
        break;
    }
    return 0;
}

LRESULT CControlDlg::OnTaskbarCreated(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // 资源管理器重启后托盘图标会消失：若当前面板是隐藏状态，重新加回来
    m_trayAdded = false;
    if (!IsPanelVisible())
        AddTrayIcon();
    return 0;
}

LRESULT CControlDlg::OnShowPanelRequest(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // 又一次双击了程序图标：把已隐藏的面板重新显示出来
    ShowPanel(true);
    return 0;
}

void CControlDlg::OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2)
{
    (void)nKey1; (void)nKey2;
    switch (nHotKeyId)
    {
    case HK_TOGGLE_SHOW:       OnToggleShow(); break;
    case HK_TOGGLE_SPEECH:     ToggleSpeech(); break;
    case HK_TOGGLE_CMD:        ToggleCmd(); break;
    case HK_TOGGLE_LOCK:       ToggleLock(); break;
    case HK_SCROLL_UP:         m_overlay.ScrollByPixels(-70.0f); break;
    case HK_SCROLL_DOWN:       m_overlay.ScrollByPixels(70.0f); break;
    case HK_FONT_UP:
        m_s.fontSize = min(120, m_s.fontSize + 4);
        SyncUiFromSettings();
        break;
    case HK_FONT_DOWN:
        m_s.fontSize = max(16, m_s.fontSize - 4);
        SyncUiFromSettings();
        break;
    case HK_TOGGLE_MIRROR:
        m_s.mirror = !m_s.mirror;
        SyncUiFromSettings();
        break;
    case HK_TOGGLE_AUTOSCROLL:
        m_s.autoScroll = !m_s.autoScroll;
        m_overlay.RefreshAuxBars();
        SetStatus(m_s.autoScroll ? L"自动滚动：开" : L"自动滚动：关");
        break;
    case HK_GOTO_TOP:
        m_overlay.ScrollToTop();
        m_matcher.SetPos(0);
        break;
    case HK_TOGGLE_HIGHLIGHT:
        m_s.highlightCur = !m_s.highlightCur;
        SyncUiFromSettings();
        break;
    case HK_SIZE_W_INC:
        EnsureOverlayVisible();
        m_overlay.ResizeStep(+1, 0);
        UpdateSizeLabel();
        SetStatus(L"浮层宽度已放大");
        break;
    case HK_SIZE_W_DEC:
        EnsureOverlayVisible();
        m_overlay.ResizeStep(-1, 0);
        UpdateSizeLabel();
        SetStatus(L"浮层宽度已缩小");
        break;
    case HK_SIZE_H_INC:
        EnsureOverlayVisible();
        m_overlay.ResizeStep(0, +1);
        UpdateSizeLabel();
        SetStatus(L"浮层高度已放大");
        break;
    case HK_SIZE_H_DEC:
        EnsureOverlayVisible();
        m_overlay.ResizeStep(0, -1);
        UpdateSizeLabel();
        SetStatus(L"浮层高度已缩小");
        break;
    case HK_TOGGLE_PANEL:
        TogglePanel();
        break;
    case HK_QUIT:
        SaveAndClose();
        break;
    default:
        break;
    }
}

void CControlDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 4)
    {
        // 「复制微信号」按钮的临时反馈文字，1.4 秒后恢复
        KillTimer(4);
        if (m_btnCopyContact.GetSafeHwnd()) m_btnCopyContact.SetWindowText(L"复制邮箱");
        CDialogEx::OnTimer(nIDEvent);
        return;
    }

    if (nIDEvent == 3)
    {
        if (!m_voiceTest.empty() &&
            ::GetTickCount() - m_voiceTestStart >= m_voiceTest.front().at)
        {
            const std::wstring t = m_voiceTest.front().text;
            m_voiceTest.erase(m_voiceTest.begin());
            AppendEvtLog(t);
            // 中间结果 + 最终结果各来一次：验证同一句话不会被翻两页
            OnSpeechText(t, false);
            OnSpeechText(t, true);
        }
        if (m_voiceTest.empty()) KillTimer(3);
    }

    if (nIDEvent == 2)
    {
        const DWORD nowTick = ::GetTickCount();
        DWORD soundAgo = 0xFFFFFFFF;

        // 轮询语音识别事件（非阻塞）
        if (m_speech.IsRunning())
        {
            m_speech.Poll();

            if (m_speech.LastSoundTick())
                soundAgo = nowTick - m_speech.LastSoundTick();
        }

        // 把「现在是否有人在说话」喂给浮层：语音跟随据此决定继续滚还是刹停。
        // 静音 0.6 秒就算停读（识别本身还有几百毫秒延迟，浮层内部另外给了宽限）。
        m_overlay.SetVoiceActive(m_followOn && m_speech.IsRunning() && soundAgo < 600);

        // 离线模型下载/安装进度（引擎没跑时接管状态行）
        UpdateModelStatus();

        if (m_speech.IsRunning())
        {
            const DWORD now = nowTick;

            if (m_speech.IsPreparing())
            {
                // 700MB 模型在后台线程加载，界面保持可操作，只提示一次
                if (!m_engineReadyNotified)
                {
                    SetStatus(L"正在加载离线识别模型（约 7 秒，首次可能更久）…");
                    m_engineReadyNotified = true;
                }
            }
            else if (m_engineReadyNotified)
            {
                m_engineReadyNotified = false;
                SetStatus(ModePrefix() + m_speech.EngineName() + L" · 输入：" + m_speech.InputName());
            }
            else if (m_followOn && (now - m_lastMatchTick) > 1500 && soundAgo >= 600)
            {
                SetStatus(L"跟读中 · 静音暂停（继续念就会自动往下滚）");
            }
            else if (m_followOn && soundAgo < 1200 && (now - m_lastMatchTick) > 3000)
            {
                SetStatus(L"检测到声音，正在识别…（若始终无文字，请检查麦克风是否被占用）");
            }
            else if (m_followOn && m_speech.InputLevel() <= 1 && (now - m_speechStartedTick) > 4000 &&
                     (now - m_lastMatchTick) > 4000 && !m_speech.IsSelfTest())
            {
                // 电平一直为 0：几乎可以确定选错了麦克风或设备被静音
                SetStatus(L"没有收到任何音频信号：请在上方麦克风下拉里换一个设备");
            }
        }

        if (m_s.autoScroll && !m_followOn)
        {
            const float pxPerTick = (12.0f + m_s.autoSpeed * 14.0f) * 0.06f;
            // 已经滚到页尾：再滚也动不了 → 等同于点了「停止滚动」，
            // 「滚动」高亮自动熄灭，不会一直空转
            if (!m_overlay.ScrollByPixels(pxPerTick) && m_overlay.AtBottom())
                StopAutoScroll(L"已滚到页尾，自动停止滚动");
        }

        // 跟随/滚动采样（仅 TP_SPEECH_LOG 打开时有输出），5 次/秒。
        // 无条件采样：按钮条/跟随/自动滚动的自动化测试都要靠它观察滚动位置。
        {
            static DWORD lastFollowLog = 0;
            if (lastFollowLog == 0 || nowTick - lastFollowLog >= 200)
            {
                lastFollowLog = nowTick;
                float tg = 0; int an = 0; bool fa = false, anim = false;
                m_overlay.DebugFollowState(tg, an, fa, anim);
                AppendFollowLog(m_overlay.ScrollY(), m_overlay.FollowSpeed(),
                                m_overlay.FollowPaused(), tg, an, fa, anim, m_s.autoScroll);
            }
        }

        UpdateSizeLabel();   // 拖动缩放后同步显示尺寸（未变化时内部直接返回）
        UpdateLevelLabel();  // 实时输入电平（未变化时内部直接返回）
    }
    CDialogEx::OnTimer(nIDEvent);
}

HBRUSH CControlDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
    if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN || nCtlColor == CTLCOLOR_DLG)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(RGB(31, 35, 40));
        if (pWnd)
        {
            const HWND h = pWnd->GetSafeHwnd();
            if (h == m_stTips.GetSafeHwnd() || h == m_stHeard.GetSafeHwnd() ||
                h == m_stSpeech.GetSafeHwnd() || h == m_stScript.GetSafeHwnd() ||
                h == m_stLevel.GetSafeHwnd() || h == m_stCmdHint.GetSafeHwnd() ||
                h == m_stNote.GetSafeHwnd() || h == m_stMicHint.GetSafeHwnd() ||
                h == m_stRecentHint.GetSafeHwnd() || h == m_stAboutDesc.GetSafeHwnd() ||
                h == m_stContactHint.GetSafeHwnd() || h == m_stAboutNote.GetSafeHwnd())
            {
                pDC->SetTextColor(RGB(96, 104, 116));
            }
            if (h == m_stContact.GetSafeHwnd())     // 邮箱地址用链接色，提示可以点
                pDC->SetTextColor(RGB(47, 112, 214));
        }
        hbr = (HBRUSH)m_brushBg.GetSafeHandle();
    }
    return hbr;
}

BOOL CControlDlg::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(&rc, RGB(247, 248, 250));
    return TRUE;
}

BOOL CControlDlg::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
        return TRUE;    // 防止误按 Esc 关掉控制台
    return CDialogEx::PreTranslateMessage(pMsg);
}

void CControlDlg::OnQuit()
{
    SaveAndClose();
}

void CControlDlg::OnClose()
{
    SaveAndClose();
}

void CControlDlg::OnCancel()
{
    // 交给 OnClose / 退出按钮处理
}

void CControlDlg::SaveAndClose()
{
    if (m_closing) return;
    m_closing = true;

    StopSpeech();
    m_installer.Cancel();      // 下载线程尽快退出（已下载的 .part 保留，下次续传）
    UnregisterHotkeys();
    RemoveTrayIcon();

    if (m_overlay.GetSafeHwnd())
    {
        CRect rc;
        ::GetWindowRect(m_overlay.GetSafeHwnd(), &rc);
        m_s.overlayRect = rc;
        m_overlay.ShowOverlay(false);
        ::DestroyWindow(m_overlay.GetSafeHwnd());
    }

    // 主面板位置与尺寸：以退出这一刻的实际值为准（隐藏到托盘时也保留最后状态）
    if (GetSafeHwnd())
    {
        if (::IsWindowVisible(GetSafeHwnd()))
        {
            RecordPanelGeometry();     // 最大化/最小化时取「还原后」的矩形
        }
        else
        {
            CRect rc;
            GetWindowRect(&rc);
            if (rc.Width() > 0 && rc.Height() > 0)
            {
                m_s.panelPos = { rc.left, rc.top };
                m_s.panelPosValid = true;
                m_s.panelSize = { rc.Width(), rc.Height() };
                m_s.panelSizeValid = true;
            }
        }
    }

    m_s.Save();
    EndDialog(IDOK);
}

void CControlDlg::OnDestroy()
{
    KillTimer(2);
    KillTimer(3);
    KillTimer(4);
    m_installer.Cancel();
    StopSpeech();
    UnregisterHotkeys();
    RemoveTrayIcon();
    if (m_qrBmp) { delete m_qrBmp; m_qrBmp = nullptr; }
    if (m_hIcon) { ::DestroyIcon(m_hIcon); m_hIcon = nullptr; }
    CDialogEx::OnDestroy();
}
