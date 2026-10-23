// OverlayWnd.h - 芸豆提词浮层
//
// 关键技术：
//  1. WS_EX_LAYERED + UpdateLayeredWindow   -> 逐像素透明，半透明黑底 + 抗锯齿文字
//  2. WS_EX_TRANSPARENT                     -> 鼠标点击穿透，不挡下面的 PPT / 白板 / 浏览器
//  3. WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE   -> 不出现在任务栏与 Alt+Tab，永不抢焦点
//  4. SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) -> 录屏、共享屏幕、截图里"看不见"
#pragma once

#include "framework.h"
#include "ScriptDoc.h"
#include "AppSettings.h"
#include <functional>

class COverlayWnd;

// 浮层右侧的滚动条。
//
// 为什么要单独开一个窗口？
//   浮层锁定态带 WS_EX_TRANSPARENT，鼠标完全穿透到下层 PPT，因此浮层自己收不到任何点击；
//   滚动条要"看得见又点得到"，只能做成贴在浮层右侧的独立分层小窗口。
//   代价仅是一条 ~14px 的窄带会挡住下层窗口，浮层其余区域依旧完全穿透。
class COverlayBar : public CWnd
{
public:
    bool Create(COverlayWnd* owner);
    void SyncTo(const CRect& overlayRect, bool interactive);
    void SetMetrics(float scrollY, float maxScroll, float viewH, float contentH);
    void ShowBar(bool show);
    bool BarVisible() const { return m_shown; }

    static int BarWidthForDpi(int dpi) { return dpi > 0 ? ::MulDiv(14, dpi, 96) : 14; }

protected:
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    DECLARE_MESSAGE_MAP()

private:
    void Render();
    CRect TrackRect() const;
    CRect ThumbRect() const;
    bool  HitThumb(CPoint pt) const;
    void  ScrollToThumbTop(int y);

    COverlayWnd* m_owner = nullptr;
    bool  m_shown = false;
    bool  m_hover = false;
    bool  m_drag = false;
    bool  m_tracking = false;
    int   m_dragGrab = 0;          // 按下点相对滑块顶部的像素偏移
    int   m_dpi = 96;
    bool  m_interactive = false;

    float m_scrollY = 0.0f, m_maxScroll = 0.0f, m_viewH = 0.0f, m_contentH = 0.0f;
    bool  m_dirty = true;

    Gdiplus::Bitmap* m_bmp = nullptr;
    int m_bmpW = 0, m_bmpH = 0;
    HDC     m_memDC = nullptr;
    HBITMAP m_hDib = nullptr;
    HBITMAP m_oldBmp = nullptr;
    void*   m_pBits = nullptr;
    int     m_dibW = 0, m_dibH = 0;
};

// 浮层顶部的按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动 / 切换稿件。
// 为什么也是独立小窗？和滚动条同理——锁定态浮层完全点击穿透，
// 按钮要「看得见又点得到」，只能自己开一个贴在浮层顶边的分层窗口。
class COverlayToolbar : public CWnd
{
public:
    // 与 COverlayWnd::OnToolbarCmd / 对话框处理的约定顺序
    // （不能用 TB_ 前缀，CommCtrl.h 里 TB_TOP/TB_BOTTOM 是轨迹条消息宏）
    enum { OBT_TOP = 0, OBT_BOTTOM, OBT_START, OBT_STOP, OBT_SCRIPTS, OBT_COUNT };

    bool Create(COverlayWnd* owner);
    void SyncTo(const CRect& overlayRect, bool interactive);
    void ShowBar(bool show);
    void SetAutoScroll(bool on);                       // 切换 滚动/停止 的可用态
    void SetScriptsOpen(bool on);                      // 「切换稿件」按钮高亮（弹出列表时）
    CRect ButtonScreenRect(int i) const;               // 按钮在屏幕坐标里的矩形（弹窗定位用）
    static int HeightForDpi(int dpi) { return dpi > 0 ? ::MulDiv(30, dpi, 96) : 30; }

protected:
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()

private:
    int  HitButton(CPoint pt) const;
    CRect ButtonRect(int i) const;
    void Render();
    // 图标按钮：GDI+ 矢量绘制（不依赖图片资源，任意 DPI 都清晰）
    void DrawIcon(Gdiplus::Graphics& g, int i, const CRect& box, const Gdiplus::Color& color) const;

    COverlayWnd* m_owner = nullptr;
    bool  m_shown = false;
    bool  m_hover = false;
    bool  m_tracking = false;
    bool  m_autoScroll = false;
    bool  m_scriptsOpen = false;
    int   m_hoverIdx = -1;
    int   m_dpi = 96;
    bool  m_interactive = false;
    bool  m_dirty = true;

    Gdiplus::Bitmap* m_bmp = nullptr;
    int m_bmpW = 0, m_bmpH = 0;
    HDC     m_memDC = nullptr;
    HBITMAP m_hDib = nullptr;
    HBITMAP m_oldBmp = nullptr;
    void*   m_pBits = nullptr;
    int     m_dibW = 0, m_dibH = 0;
};

// 点「切换稿件」后弹出的最近文稿列表。
// 同样是独立分层小窗（浮层锁定穿透，弹窗必须自己收鼠标），
// 点了列表项回调给对话框去载入，点窗口外或再点一次按钮就收起。
// 条目放不下屏幕时窗口压到可用高度，右侧出现细滚动条，滚轮可滚动。
class COverlayScriptMenu : public CWnd
{
public:
    // 列表末尾固定多一项「打开其他文稿…」，回调收到的索引 = m_items.size()
    bool Create(COverlayWnd* owner);
    bool ShowMenu(const std::vector<std::wstring>& items, const std::wstring& current,
                  const CRect& anchorScreen, std::function<void(int)> onPick);
    void HideMenu();
    bool MenuVisible() const { return m_shown; }

protected:
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    DECLARE_MESSAGE_MAP()

private:
    int   RowCount() const;                 // 含末尾「打开其他文稿…」
    int   HitRow(CPoint pt) const;
    CRect RowRect(int i) const;
    void  Relayout(const CRect& anchorScreen);
    void  Render();

    COverlayWnd* m_owner = nullptr;
    std::vector<std::wstring> m_items;
    std::wstring m_current;                 // 当前稿件，列表里打个勾
    std::function<void(int)> m_onPick;
    bool  m_shown = false;
    bool  m_waitRelease = true;             // 从按钮按下那一刻弹出 → 忽略这次抬起
    bool  m_tracking = false;
    int   m_hoverIdx = -1;
    int   m_dpi = 96;
    bool  m_dirty = true;
    int   m_rowH = 0, m_titleH = 0, m_pad = 0, m_extraH = 0, m_w = 0;

    // 滚动：条目放不下屏幕时启用（m_scrollTop = 首个可视行号）
    bool  m_scrollable = false;
    int   m_scrollTop = 0;
    int   m_maxRows = 0;                    // 当前窗口高度内放得下的行数

    Gdiplus::Bitmap* m_bmp = nullptr;
    int m_bmpW = 0, m_bmpH = 0;
    HDC     m_memDC = nullptr;
    HBITMAP m_hDib = nullptr;
    HBITMAP m_oldBmp = nullptr;
    void*   m_pBits = nullptr;
    int     m_dibW = 0, m_dibH = 0;
};

class COverlayWnd : public CWnd
{
public:
    COverlayWnd();
    virtual ~COverlayWnd();

    bool Create();
    void ApplyDefaultRect();
    void ApplyRect(const CRect& rc);
    CRect CurrentRect() const;

    void SetDoc(CScriptDoc* doc) { m_doc = doc; MarkDirty(); }
    void SetSettings(AppSettings* s) { m_settings = s; MarkDirty(); }

    void ShowOverlay(bool show);
    void SetInteractive(bool on);
    bool IsInteractive() const { return m_interactive; }
    void SetClickThrough(bool on);   // 关掉后锁定态浮层也能接收鼠标（滚轮滚动）

    void Reanchor(int origChar);
    void ResetAnchor();
    // 相对滚动 dy 像素（正=向下）。返回位置是否真的变了：
    // 已经顶到页尾（或页首）再往下（上）滚时会返回 false —— 自动滚动据此自动停。
    bool ScrollByPixels(float dy);
    bool AtBottom() const { return m_maxScrollCur > 0.5f && m_scrollY >= m_maxScrollCur - 0.5f; }
    void ScrollToPixel(float y);
    void ScrollToTop();

    // 语音跟随（智能提词）：目标位置由「读到第几个字」连续推导，
    // 读到哪滚到哪；麦克风静音一段时间后自动刹停，继续念再平滑接上。
    void SetSmartFollow(bool on) { m_smartFollow = on; m_dirty = true; }
    bool SmartFollow() const { return m_smartFollow; }
    void SetVoiceActive(bool on);
    bool VoiceActive() const { return m_voiceActive; }
    bool FollowPaused() const { return m_followPaused; }   // 是否已因静音暂停
    float FollowSpeed() const { return m_followV; }        // 当前滚动速度 px/s（调试用）

    // 整页滚动：dir > 0 向下翻一页，dir < 0 向上翻一页。
    // 返回值：翻页后窗口首行对应的原文字符索引（无稿件时 -1）
    int   ScrollPage(int dir);
    int   ScrollToBottom();     // 滚到页尾；返回页尾首行的原文字符索引（无稿件时 -1）
    int   TopCharIndex() const;
    float ScrollY() const { return m_scrollY; }
    float MaxScroll() const { return m_maxScrollCur; }
    void MarkDirty() { m_dirty = true; }

    // 顶部按钮条：对话框注册处理器（返回页首/到页尾/滚动/停止滚动/切换稿件）
    void SetToolbarCmd(std::function<void(int)> f) { m_toolbarCmd = std::move(f); }
    void RefreshAuxBars() { SyncBar(); }   // autoScroll 等状态变化后刷新按钮高亮

    // 「切换稿件」弹出最近文稿列表。onPick 参数：< items.size() 为选中的路径索引，
    // == items.size() 表示点了末尾的「打开其他文稿…」。返回列表当前是否打开。
    bool ToggleScriptMenu(const std::vector<std::wstring>& items, const std::wstring& current,
                          std::function<void(int)> onPick);
    void HideScriptMenu();
    bool ScriptMenuVisible() const { return m_scriptMenu.MenuVisible(); }
    void DebugFollowState(float& target, int& anchor, bool& fa, bool& anim) const
    { target = m_targetScroll; anchor = m_anchorChar; fa = m_followAnchor; anim = m_animating; }

    // 尺寸调整：dw / dh 为 +1 放大、-1 缩小、0 不变
    void ResizeStep(int dw, int dh);
    void SetSizeRatio(int wPercent, int hPercent);   // 相对工作区百分比

    static const int kMinW = 240;
    static const int kMinH = 170;

protected:
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
    afx_msg LRESULT OnNcHitTest(CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    DECLARE_MESSAGE_MAP()

public:
    // 仅供顶部按钮条回调
    void OnToolbarCmd(int id);
    // 仅供文稿列表：自己收起时把「切换稿件」按钮的高亮复位
    void OnScriptMenuClosed();

private:
    // 缩放手柄命中区域（位标志，可组合成角）
    enum
    {
        RZ_NONE = 0,
        RZ_L = 1, RZ_R = 2, RZ_T = 4, RZ_B = 8
    };
    int  HitZone(CPoint clientPt) const;
    void Render();
    void EnsureBackBuffer(int w, int h);
    void DrawContent(Gdiplus::Graphics& g, int W, int H);
    void SyncBar();
    void UpdateBarMetrics(int H, int contentH, float maxScroll);

    // 顶部按钮条悬停显隐：鼠标在浮层（或已显示的按钮条）上才显示，离开后延迟隐藏
    bool MouseOverOverlayArea() const;
    void UpdateToolbarHover();
    void ApplyToolbarVisibility();

    CScriptDoc*    m_doc = nullptr;
    AppSettings*   m_settings = nullptr;
    COverlayBar    m_bar;
    COverlayToolbar m_toolbar;
    COverlayScriptMenu m_scriptMenu;
    std::function<void(int)> m_toolbarCmd;   // 顶部按钮 → 对话框
    int   m_dpi = 96;

    float m_scrollY = 0.0f;
    float m_targetScroll = 0.0f;
    float m_maxScrollCur = 0.0f;
    bool  m_followAnchor = true;
    int   m_anchorChar = 0;
    bool  m_anchorValid = false;   // 只有语音匹配过 / 手动定位过才高亮当前行
    bool  m_animating = false;
    bool  m_dirty = true;
    bool  m_interactive = false;
    bool  m_clickThrough = true;   // 锁定态鼠标穿透（可关闭）

    // 语音跟随
    bool  m_smartFollow = true;    // 关掉就退回「按行跳动」的旧行为
    bool  m_voiceActive = false;   // 麦克风当前有人声
    bool  m_followPaused = false;  // 静音刹停中
    float m_followV = 0.0f;        // 平滑后的滚动速度（px/s）
    DWORD m_lastTick = 0;          // 用于计算真实帧间隔
    DWORD m_voiceOffTick = 0;
    DWORD m_lastAnchorTick = 0;    // 最近一次匹配到新位置的时间

    // 顶部按钮条悬停显隐
    float m_lastContentH = 0.0f;   // 最近一次内容高度（>0 才有可显示的按钮条）
    bool  m_tbHover = false;       // 鼠标当前是否在浮层 / 按钮条区域上
    DWORD m_tbLeaveAt = 0;         // 鼠标离开后的延迟隐藏时刻（0=不在倒计时）

    Gdiplus::Bitmap* m_pBmp = nullptr;
    int m_bmpW = 0, m_bmpH = 0;

    int    m_dragZone = 0;
    bool   m_panning = false;        // 锁定 + 非穿透：按住拖动 = 拖动内容
    CPoint m_panLastScreen;
    CRect  m_dragStartRect;
    CPoint m_dragStartScreen;

    HDC     m_memDC = nullptr;
    HBITMAP m_hDib = nullptr;
    HBITMAP m_oldBmp = nullptr;
    void*   m_pBits = nullptr;
    int     m_dibW = 0, m_dibH = 0;
};
