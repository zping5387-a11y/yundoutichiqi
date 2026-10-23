// ControlDlg.h - 控制台（设置面板 + 全局热键 + 语音跟读控制）
#pragma once

#include "framework.h"
#include "AppSettings.h"
#include "ScriptDoc.h"
#include "ScriptMatcher.h"
#include "OverlayWnd.h"
#include "SpeechEngine.h"
#include "VoiceCommand.h"
#include "RecentListWnd.h"
#include "ModelInstaller.h"

class CControlDlg : public CDialogEx
{
public:
    explicit CControlDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_CONTROL_DIALOG };

protected:
    BOOL OnInitDialog() override;
    void OnOK() override {}
    void OnCancel() override;
    BOOL PreTranslateMessage(MSG* pMsg) override;

    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnDropFiles(HDROP hDropInfo);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnMove(int x, int y);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

    // ---- 无边框窗口（参考 C:\ddzprj\mfcwnd 的做法） ----
    // WS_POPUP 去掉标题栏/边框；边缘命中测试支持拉伸；标题带返回 HTCAPTION
    // 交给系统拖动（顺带获得双击最大化 / Aero Snap）；右上角三个标题按钮自绘。
    afx_msg LRESULT OnNcHitTest(CPoint point);
    afx_msg BOOL  OnNcActivate(BOOL bActive);
    afx_msg void  OnPaint();
    afx_msg void  OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void  OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void  OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    void ApplyWindowCorner();              // 还原态圆角（Win11 DWM，失败退回 SetWindowRgn）
    bool m_rgnApplied = false;             // 当前是否处于 SetWindowRgn 回退状态
    CRect CaptionButtonRect(int i) const;  // 客户区坐标：0=最小化 1=最大化/还原 2=关闭
    int   CaptionHitTest(CPoint ptClient) const;
    void  InvalidateCaption();
    void  DrawCaptionButton(CDC& dc, int i);
    afx_msg void OnTabClick(UINT nID);

    afx_msg void OnOpenScript();
    afx_msg void OnClipboardScript();
    afx_msg void OnSampleScript();
    afx_msg void OnMicChanged();
    afx_msg void OnLangChanged();
    afx_msg void OnEngineChanged();
    afx_msg void OnToggleSpeech();
    afx_msg void OnToggleCmd();
    afx_msg void OnModelStatusClick();
    afx_msg void OnCopyContact();         // 关于页：复制作者邮箱到剪贴板
    afx_msg void OnContactClick();        // 关于页：点邮箱直接用系统邮件客户端写信
    afx_msg LRESULT OnModelEvent(WPARAM wParam, LPARAM lParam);
    afx_msg void OnToggleShow();
    afx_msg void OnToggleLock();
    afx_msg void OnFullscreen();
    afx_msg void OnResetPos();
    afx_msg void OnSizeWMinus();
    afx_msg void OnSizeWPlus();
    afx_msg void OnSizeHMinus();
    afx_msg void OnSizeHPlus();
    afx_msg void OnPickTextColor();
    afx_msg void OnPickHiColor();
    afx_msg void OnPickBgColor();
    afx_msg void OnHidePanel();
    afx_msg void OnQuit();
    afx_msg void OnCheckChanged(UINT nID);
    afx_msg LRESULT OnTrayMessage(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTaskbarCreated(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnShowPanelRequest(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    int  S(int v) const;
    CRect CalcPanelRect() const;      // 计算面板落点+尺寸（恢复上次记录，夹到工作区）
    void  RecordPanelGeometry();      // 记录当前面板位置与尺寸（拖动/缩放时调用）
    void BuildUi();
    void CreateFonts();
    void SyncUiFromSettings();
    void RegisterHotkeys();
    void UnregisterHotkeys();

    // ---- 页签 + 可缩放布局 ----
    // 设计稿坐标（逻辑单位，S() 负责 DPI/自适应缩放），窗口变大时按规则伸展：
    //   stretchW  右边界跟着窗口右边走（整行的标签 / 滑块 / 下拉框）
    //   slideW    整个控件水平平移 extraW（右侧贴边的数值文本，避免叠在滑块上）
    //   stretchH  下边界跟着窗口下边走（说明文字）
    //   bottom    整体跟着窗口底边走（底部按钮、版本号）
    struct LayoutItem
    {
        CWnd* wnd;
        CRect rc;          // 设计稿坐标
        int   page;        // 0=四页都显示，1=稿件，2=设置，3=语音，4=关于
        bool  stretchW;
        bool  slideW;
        bool  bottom;
        bool  stretchH;
    };
    void AddLayout(CWnd* wnd, const CRect& rcDesign, int page,
                   bool stretchW = false, bool bottom = false, bool stretchH = false,
                   bool slideW = false);
    void ApplyLayout();
    void SwitchPage(int page);
    int  m_page = 1;                       // 1=稿件，2=设置，3=语音，4=关于
    std::vector<LayoutItem> m_layout;

    // 标题栏自绘按钮状态（无边框窗口）
    int  m_capHover = -1;                  // 悬停按钮（-1 无）
    int  m_capPressed = -1;                // 按下中的按钮
    bool m_capTracking = false;            // 已申请 WM_MOUSELEAVE 跟踪

    void RefreshRecentList();
    void OnRecentOpen(int idx);
    void OnRecentDelete(int idx);
    void OnRecentOpenFolder(int idx);   // 行内「打开文件夹」：资源管理器定位文件
    void OnRecentGoto(int idx);         // 行内「去提词」：载入并确保浮层显示
    void LoadScriptFile(const std::wstring& path, bool touchRecent = true);
    void ApplyScriptText(const std::wstring& text, const std::wstring& label);

    // 语音：跟读与语音命令是两个独立功能，共用同一路麦克风 / 识别引擎，
    // 任何一个打开都会启动引擎，两个都关掉才真正停止采集。
    bool StartSpeechEngine();
    void StopSpeech();
    void RestartSpeechEngine();
    void SyncSpeechEngine();
    void ToggleSpeech();
    void ToggleCmd();
    void SetFollowOn(bool on);
    void SetCmdOn(bool on);
    void UpdateSpeechButtons();
    void RefreshSpeechStatus();
    std::wstring ModePrefix() const;

    // 离线语音模型：按需下载（发布包不带 570MB 模型）
    void StartModelDownload(bool autoFollow);   // 弹确认 → 后台下载 → 装好后自动开跟读
    bool UpdateModelStatus();                   // 下载中/缺模型时接管状态行，返回 true 表示已写状态
    void HandleModelInstalled();                // 装好后的收尾（启用离线引擎 / 接上跟读 / 提示）
    void RefreshEngineCombo();                  // 装好模型后把「离线识别」解除禁用

    void ToggleLock();
    void EnsureOverlayVisible();
    void SetStatus(const std::wstring& s);
    void OnToolbarBtn(int id);   // 浮层顶部按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动
    void StopAutoScroll(const std::wstring& status);   // 停止滚动（按钮 / 滚到页尾自动停 共用）
    void OnSpeechText(const std::wstring& text, bool isFinal);
    bool RunVoiceCommand(VoiceCmd cmd, const wchar_t* label, const std::wstring& heard);
    void LoadVoiceTestScript(const wchar_t* spec);
    void UpdateSizeLabel();
    void RefreshMicList();
    void UpdateLevelLabel();
    void SaveAndClose();

    // 主面板显示 / 隐藏（含托盘图标）
    void TogglePanel();
    void ShowPanel(bool show);
    bool IsPanelVisible() const;
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    AppSettings   m_s;
    CScriptDoc    m_doc;
    CScriptMatcher m_matcher;
    COverlayWnd   m_overlay;
    CSpeechEngine m_speech;
    CModelInstaller m_installer;      // 离线模型下载器（后台线程）

    UINT  m_dpi = 96;
    double m_uiScale = 1.0;   // 面板自适应缩放（防止高 DPI 下超出屏幕）
    CFont m_fontUi, m_fontBold, m_fontTitle, m_fontSmall;
    CBrush m_brushBg;

    CButton      m_btnOpen, m_btnClip, m_btnSample, m_btnSpeech, m_btnCmd, m_btnColor, m_btnHiColor, m_btnBgColor;
    CButton      m_btnShow, m_btnLock, m_btnFull, m_btnReset, m_btnQuit, m_btnHidePanel;
    CButton      m_btnWMinus, m_btnWPlus, m_btnHMinus, m_btnHPlus;
    CButton      m_tabScript, m_tabVoice, m_tabSettings, m_tabAbout;   // 左侧竖排页签（自绘）
    CButton      m_chkHighlight, m_chkMirror, m_chkShadow, m_chkPass;
    CComboBox    m_cmbLang, m_cmbMic, m_cmbEngine;
    CSliderCtrl  m_slFont, m_slSpacing, m_slAlpha, m_slSpeed;
    CStatic      m_stFont, m_stSpacing, m_stAlpha, m_stSpeed;
    CStatic      m_valFont, m_valSpacing, m_valAlpha, m_valSpeed;
    CStatic      m_stTitle, m_stSubtitle, m_sec1, m_sec2, m_sec3, m_sec4;
    CStatic      m_stScript, m_stSpeech, m_stHeard, m_stTips, m_stVersion, m_stLevel;
    CStatic      m_stCmdHint;         // 语音命令按钮右侧的一句话说明
    CStatic      m_stNote;            // 语音页底部说明（跟读/命令/排查）
    CStatic      m_stMicHint;         // 麦克风下拉上方的说明
    CStatic      m_secRecent, m_stRecentHint;   // 稿件页：最近文稿区块
    CRecentListWnd m_lstRecent;       // 稿件页：最近文稿自绘列表（双击载入 / × 删除）
    // 关于页
    CStatic      m_secAbout, m_stAppName, m_stAboutDesc, m_stAboutNote;
    CStatic      m_secContact, m_stContact, m_stContactHint;
    CStatic      m_stQr;              // 二维码（程序目录有 contact.png 才显示图片）
    CButton      m_btnCopyContact;
    std::wstring m_qrPath;            // <exe>\contact.png
    Gdiplus::Bitmap* m_qrBmp = nullptr;
    bool m_qrTried = false;
    void DrawQrBox(CDC& dc, const CRect& rc);
    // 列表内容预览缓存：path -> (文件修改时间, 预览文本)，避免每次刷新都解包 docx
    std::map<std::wstring, std::pair<ULONGLONG, std::wstring>> m_previewCache;

    // 两个互相独立的语音功能开关（运行态，不持久化）
    bool m_followOn = false;          // 语音跟读：识别文本 → 匹配稿件 → 高亮 / 跟随
    bool m_cmdOn = false;             // 语音命令：识别文本 → 翻页 / 回顶部

    std::vector<std::wstring> m_micIds;
    std::wstring m_levelTextCache;
    bool m_engineReadyNotified = false;   // 离线模型加载完成的提示只刷一次
    bool m_pendingFollowAfterDownload = false;   // 下载完成后自动开始跟读
    DWORD m_modelDoneTick = 0;                   // 模型刚装好的提示保留到什么时候

    int  m_hotkeyCount = 0;
    DWORD m_lastMatchTick = 0;
    DWORD m_speechStartedTick = 0;
    DWORD m_lastCmdTick = 0;      // 语音命令去重（同一句的中间结果 + 最终结果）
    bool m_closing = false;
    bool m_trayAdded = false;
    bool m_panelPosApplied = false;       // 恢复位置是否已在首次显示后校正过
    CPoint m_restorePos { 0, 0 };         // 计算好的恢复落点
    HICON m_hIcon = nullptr;
    NOTIFYICONDATAW m_nid {};
    CRect m_lastOverlayRect { 0, 0, 0, 0 };
    std::wstring m_statusText;

    // 自动化测试钩子（TP_VOICE_TEST）：按毫秒模拟若干次识别回调
    struct VoiceTestItem { DWORD at; std::wstring text; };
    std::vector<VoiceTestItem> m_voiceTest;
    DWORD m_voiceTestStart = 0;
};
