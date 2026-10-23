// OverlayWnd.cpp
#include "OverlayWnd.h"

using namespace Gdiplus;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif

// ---------------------------------------------------------------------------
//  分层窗口呈现helper：ARGB 位图 -> 预乘 alpha -> UpdateLayeredWindow
// ---------------------------------------------------------------------------
namespace
{
    void PresentLayeredBitmap(HWND hwnd, Bitmap* bmp, int W, int H,
                              HDC& memDC, HBITMAP& hDib, HBITMAP& oldBmp, void*& pBits,
                              int& dibW, int& dibH)
    {
        if (!bmp || W <= 0 || H <= 0) return;

        if (!memDC)
        {
            HDC screen = ::GetDC(nullptr);
            memDC = ::CreateCompatibleDC(screen);
            ::ReleaseDC(nullptr, screen);
        }

        if (!hDib || dibW != W || dibH != H)
        {
            if (oldBmp) { ::SelectObject(memDC, oldBmp); oldBmp = nullptr; }
            if (hDib) { ::DeleteObject(hDib); hDib = nullptr; }

            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = W;
            bi.bmiHeader.biHeight = -H;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            pBits = nullptr;
            hDib = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
            dibW = W;
            dibH = H;
            if (hDib) oldBmp = (HBITMAP)::SelectObject(memDC, hDib);
        }
        if (!hDib || !pBits) return;

        Rect lockRect(0, 0, W, H);
        BitmapData bd;
        if (bmp->LockBits(&lockRect, ImageLockModeRead | ImageLockModeWrite,
                          PixelFormat32bppARGB, &bd) == Ok)
        {
            BYTE* base = (BYTE*)bd.Scan0;
            const int stride = bd.Stride;
            BYTE* dst = (BYTE*)pBits;
            const int dstStride = W * 4;

            for (int y = 0; y < H; ++y)
            {
                BYTE* row = (stride > 0) ? (base + y * stride) : (base + (H - 1 - y) * (-stride));
                BYTE* out = dst + y * dstStride;
                for (int x = 0; x < W; ++x)
                {
                    BYTE* px = row + x * 4;
                    const BYTE a = px[3];
                    if (a == 0) { out[x * 4] = out[x * 4 + 1] = out[x * 4 + 2] = out[x * 4 + 3] = 0; continue; }
                    out[x * 4 + 0] = (BYTE)((px[0] * a + 127) / 255);
                    out[x * 4 + 1] = (BYTE)((px[1] * a + 127) / 255);
                    out[x * 4 + 2] = (BYTE)((px[2] * a + 127) / 255);
                    out[x * 4 + 3] = a;
                }
            }
            bmp->UnlockBits(&bd);
        }

        HDC screen = ::GetDC(nullptr);
        POINT ptSrc = { 0, 0 };
        RECT wr = {};
        ::GetWindowRect(hwnd, &wr);
        POINT ptDst = { wr.left, wr.top };
        SIZE  size = { W, H };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::UpdateLayeredWindow(hwnd, screen, &ptDst, &size, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
        ::ReleaseDC(nullptr, screen);
    }
}

BEGIN_MESSAGE_MAP(COverlayBar, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_MOUSEWHEEL()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

bool COverlayBar::Create(COverlayWnd* owner)
{
    m_owner = owner;

    HDC dc = ::GetDC(nullptr);
    m_dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ::ReleaseDC(nullptr, dc);
    if (m_dpi < 96) m_dpi = 96;

    LPCTSTR cls = ::AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                        ::LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);
    // 注意：不能带 WS_EX_TRANSPARENT，否则点击又穿透了；NOACTIVATE 保证不抢焦点
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    const int w = BarWidthForDpi(m_dpi);
    if (!CreateEx(exStyle, cls, L"提词滚动条", WS_POPUP, CRect(0, 0, w, 200), owner, 0))
        return false;

    wchar_t allow[8] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_CAPTURE_ALLOWED", allow, 8) == 0 || allow[0] != L'1')
    {
        if (!::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE))
            ::SetWindowDisplayAffinity(m_hWnd, WDA_MONITOR);
    }
    return true;
}

void COverlayBar::SyncTo(const CRect& overlayRect, bool interactive)
{
    if (!GetSafeHwnd()) return;

    m_interactive = interactive;

    const int dpi = m_dpi;
    const int barW = BarWidthForDpi(dpi);
    const int gapY = ::MulDiv(4, dpi, 96);
    // 调整模式下整体往左让开 16px，别和右侧缩放手柄抢鼠标
    const int gapRight = interactive ? ::MulDiv(16, dpi, 96) : ::MulDiv(2, dpi, 96);

    CRect rc(overlayRect.right - gapRight - barW,
             overlayRect.top + gapY,
             overlayRect.right - gapRight,
             overlayRect.bottom - gapY);
    if (rc.Height() < 40) rc.bottom = rc.top + 40;

    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, rc.Width(), rc.Height(),
                   SWP_NOACTIVATE | (m_shown ? SWP_SHOWWINDOW : 0));

    if (m_bmpW != rc.Width() || m_bmpH != rc.Height()) m_dirty = true;
    if (m_dirty) Render();
}

void COverlayBar::SetMetrics(float scrollY, float maxScroll, float viewH, float contentH)
{
    const bool changed = fabsf(scrollY - m_scrollY) > 0.4f ||
                         fabsf(maxScroll - m_maxScroll) > 0.4f ||
                         fabsf(viewH - m_viewH) > 0.4f ||
                         fabsf(contentH - m_contentH) > 0.4f;
    m_scrollY = scrollY;
    m_maxScroll = maxScroll;
    m_viewH = viewH;
    m_contentH = contentH;
    if (changed) { m_dirty = true; Render(); }
}

void COverlayBar::ShowBar(bool show)
{
    if (!GetSafeHwnd()) return;
    if (show == m_shown) return;

    m_shown = show;
    ::ShowWindow(m_hWnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show)
    {
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_dirty = true;
        Render();
    }
}

CRect COverlayBar::TrackRect() const
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int tw = max(4, ::MulDiv(6, m_dpi, 96));
    const int padY = ::MulDiv(3, m_dpi, 96);
    CRect t((rc.Width() - tw) / 2, padY, (rc.Width() - tw) / 2 + tw, rc.bottom - padY);
    if (t.bottom - t.top < 30) t.bottom = t.top + 30;
    return t;
}

CRect COverlayBar::ThumbRect() const
{
    CRect track = TrackRect();
    const int trackH = track.Height();
    const int minThumb = max(20, ::MulDiv(30, m_dpi, 96));

    int thumbH = trackH;
    if (m_contentH > 1.0f && m_viewH > 1.0f)
        thumbH = (int)(trackH * (m_viewH / m_contentH) + 0.5f);
    if (thumbH < minThumb) thumbH = minThumb;
    if (thumbH > trackH)   thumbH = trackH;

    float frac = 0.0f;
    if (m_maxScroll > 0.5f) frac = m_scrollY / m_maxScroll;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    const int y = track.top + (int)((trackH - thumbH) * frac + 0.5f);
    return CRect(track.left, y, track.right, y + thumbH);
}

bool COverlayBar::HitThumb(CPoint pt) const
{
    CRect t = ThumbRect();
    t.InflateRect(::MulDiv(3, m_dpi, 96), 0);   // 稍微放宽，方便抓
    return t.PtInRect(pt) != FALSE;
}

void COverlayBar::ScrollToThumbTop(int y)
{
    if (!m_owner) return;
    CRect track = TrackRect();
    CRect thumb = ThumbRect();
    const int span = track.Height() - thumb.Height();
    if (span <= 0) return;

    float frac = (float)(y - track.top) / (float)span;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    m_owner->ScrollToPixel(frac * m_maxScroll);
}

void COverlayBar::Render()
{
    if (!GetSafeHwnd() || !m_shown) return;

    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int W = rc.Width(), H = rc.Height();
    if (W < 4 || H < 16) return;

    if (!m_bmp || m_bmpW != W || m_bmpH != H)
    {
        delete m_bmp;
        m_bmp = new Bitmap(W, H, PixelFormat32bppARGB);
        m_bmpW = W;
        m_bmpH = H;
    }
    if (!m_bmp) return;

    Graphics g(m_bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    CRect track = TrackRect();
    CRect thumb = ThumbRect();
    const float r = (REAL)track.Width() * 0.5f;

    auto roundRect = [](const CRect& r0, REAL rad, GraphicsPath& p)
    {
        const REAL x = (REAL)r0.left, y = (REAL)r0.top;
        const REAL w = (REAL)r0.Width(), h = (REAL)r0.Height();
        const REAL d = min(rad * 2.0f, min(w, h));
        if (d <= 1.0f)
        {
            p.AddRectangle(RectF(x, y, w, h));
            return;
        }
        p.AddArc(x, y, d, d, 180.0f, 90.0f);
        p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
    };

    // 轨道
    GraphicsPath trackPath;
    roundRect(track, r, trackPath);
    SolidBrush trackBrush(Color(m_hover || m_drag ? 60 : 40, 255, 255, 255));
    g.FillPath(&trackBrush, &trackPath);

    // 滑块
    GraphicsPath thumbPath;
    roundRect(thumb, r, thumbPath);
    BYTE ta = 150;
    if (m_drag)       ta = 235;
    else if (m_hover) ta = 195;
    SolidBrush thumbBrush(Color(ta, 255, 255, 255));
    g.FillPath(&thumbBrush, &thumbPath);

    PresentLayeredBitmap(m_hWnd, m_bmp, W, H, m_memDC, m_hDib, m_oldBmp, m_pBits, m_dibW, m_dibH);
    m_dirty = false;
}

BOOL COverlayBar::OnEraseBkgnd(CDC*) { return TRUE; }

void COverlayBar::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
    if (!m_owner) return;

    if (HitThumb(point))
    {
        m_drag = true;
        m_dragGrab = point.y - ThumbRect().top;
        SetCapture();
        m_dirty = true;
        Render();
        return;
    }

    // 点轨道空白：翻页
    const int page = (int)(m_viewH * 0.82f);
    m_owner->ScrollByPixels(point.y < ThumbRect().top ? -(float)page : (float)page);
}

void COverlayBar::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
    if (m_drag)
    {
        m_drag = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        m_dirty = true;
        Render();
    }
}

void COverlayBar::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
    if (m_drag)
    {
        ScrollToThumbTop(point.y - m_dragGrab);
        return;
    }

    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        if (::TrackMouseEvent(&tme)) m_tracking = true;
    }

    const bool hover = HitThumb(point) != FALSE;
    if (hover != m_hover)
    {
        m_hover = hover;
        ::SetCursor(::LoadCursorW(nullptr, hover ? IDC_HAND : IDC_ARROW));
        Render();
        return;
    }
    ::SetCursor(::LoadCursorW(nullptr, hover ? IDC_HAND : IDC_ARROW));
}

void COverlayBar::OnMouseLeave()
{
    m_tracking = false;
    if (m_hover)
    {
        m_hover = false;
        Render();
    }
}

BOOL COverlayBar::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    if (m_owner) m_owner->ScrollByPixels(-zDelta * 0.55f);
    return TRUE;
}

void COverlayBar::OnCaptureChanged(CWnd* pWnd)
{
    m_drag = false;
    m_dirty = true;
    CWnd::OnCaptureChanged(pWnd);
}

// ------------------------------------------------------------------
//  顶部按钮条：返回页首 / 到页尾 / 滚动 / 停止滚动
// ------------------------------------------------------------------
BEGIN_MESSAGE_MAP(COverlayToolbar, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

bool COverlayToolbar::Create(COverlayWnd* owner)
{
    m_owner = owner;

    HDC dc = ::GetDC(nullptr);
    m_dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ::ReleaseDC(nullptr, dc);
    if (m_dpi < 96) m_dpi = 96;

    LPCTSTR cls = ::AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                        ::LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);
    // 不能带 WS_EX_TRANSPARENT（否则点击穿透）；NOACTIVATE 保证不抢焦点
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    if (!CreateEx(exStyle, cls, L"提词按钮条", WS_POPUP, CRect(0, 0, 300, HeightForDpi(m_dpi)),
                  owner, 0))
        return false;

    wchar_t allow[8] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_CAPTURE_ALLOWED", allow, 8) == 0 || allow[0] != L'1')
    {
        if (!::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE))
            ::SetWindowDisplayAffinity(m_hWnd, WDA_MONITOR);
    }
    return true;
}

void COverlayToolbar::SyncTo(const CRect& overlayRect, bool interactive)
{
    if (!GetSafeHwnd()) return;

    m_interactive = interactive;

    const int dpi = m_dpi;
    const int h = HeightForDpi(dpi);
    const int gap = ::MulDiv(4, dpi, 96);
    const int w = ::MulDiv(48 * COverlayToolbar::OBT_COUNT, dpi, 96);   // 5 个图标按钮，每个 48px

    // 默认贴在浮层上边缘外侧；浮层已顶到屏幕上沿时收进浮层内，别跑出屏幕
    int top = overlayRect.top - h - gap;
    MONITORINFO mi = { sizeof(mi) };
    ::GetMonitorInfoW(::MonitorFromRect(&overlayRect, MONITOR_DEFAULTTONEAREST), &mi);
    if (top < mi.rcWork.top) top = overlayRect.top + gap;

    // 水平也要夹回工作区：浮层贴着屏幕右缘（或比按钮条还窄）时，
    // 按钮条不能把「切换稿件」挤到屏幕外面去
    int left = overlayRect.left;
    if (left + w > mi.rcWork.right) left = mi.rcWork.right - w;
    if (left < mi.rcWork.left) left = mi.rcWork.left;

    CRect rc(left, top, left + w, top + h);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, rc.left, rc.top, rc.Width(), rc.Height(),
                   SWP_NOACTIVATE | (m_shown ? SWP_SHOWWINDOW : 0));

    if (m_bmpW != rc.Width() || m_bmpH != rc.Height()) m_dirty = true;
    if (m_dirty) Render();
}

void COverlayToolbar::ShowBar(bool show)
{
    if (!GetSafeHwnd() || show == m_shown) return;

    m_shown = show;
    ::ShowWindow(m_hWnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show)
    {
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_dirty = true;
        Render();
    }
}

void COverlayToolbar::SetAutoScroll(bool on)
{
    if (on == m_autoScroll) return;
    m_autoScroll = on;
    m_dirty = true;
    Render();
}

void COverlayToolbar::SetScriptsOpen(bool on)
{
    if (on == m_scriptsOpen) return;
    m_scriptsOpen = on;
    m_dirty = true;
    Render();
}

CRect COverlayToolbar::ButtonScreenRect(int i) const
{
    if (!GetSafeHwnd() || i < 0 || i >= OBT_COUNT) return CRect(0, 0, 0, 0);
    CRect rc = ButtonRect(i);
    ClientToScreen(&rc);
    return rc;
}

int COverlayToolbar::HitButton(CPoint pt) const
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    if (!rc.PtInRect(pt)) return -1;
    int i = pt.x * OBT_COUNT / max(1, rc.Width());
    if (i < 0) i = 0;
    if (i >= OBT_COUNT) i = OBT_COUNT - 1;
    return i;
}

CRect COverlayToolbar::ButtonRect(int i) const
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int cell = rc.Width() / OBT_COUNT;
    const int pad = ::MulDiv(3, m_dpi, 96);
    return CRect(i * cell + pad, pad, (i + 1) * cell - pad, rc.bottom - pad);
}

void COverlayToolbar::Render()
{
    if (!GetSafeHwnd() || !m_shown) return;

    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int W = rc.Width(), H = rc.Height();
    if (W < 40 || H < 12) return;

    if (!m_bmp || m_bmpW != W || m_bmpH != H)
    {
        delete m_bmp;
        m_bmp = new Bitmap(W, H, PixelFormat32bppARGB);
        m_bmpW = W;
        m_bmpH = H;
    }
    if (!m_bmp) return;

    Graphics g(m_bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    auto roundRect = [](const CRect& r0, REAL rad, GraphicsPath& p)
    {
        const REAL x = (REAL)r0.left, y = (REAL)r0.top;
        const REAL w = (REAL)r0.Width(), h = (REAL)r0.Height();
        const REAL d = min(rad * 2.0f, min(w, h));
        if (d <= 1.0f) { p.AddRectangle(RectF(x, y, w, h)); return; }
        p.AddArc(x, y, d, d, 180.0f, 90.0f);
        p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
    };

    // 底条
    GraphicsPath strip;
    roundRect(rc, (REAL)::MulDiv(8, m_dpi, 96), strip);
    SolidBrush stripBrush(Color(190, 16, 20, 30));
    g.FillPath(&stripBrush, &strip);

    // 图标按钮：返回页首 / 到页尾 / 滚动 / 停止滚动 / 切换稿件
    const int iconPx = max(12, ::MulDiv(15, m_dpi, 96));

    for (int i = 0; i < OBT_COUNT; ++i)
    {
        const CRect br = ButtonRect(i);
        GraphicsPath bp;
        roundRect(br, (REAL)::MulDiv(5, m_dpi, 96), bp);

        const bool active = (i == OBT_START && m_autoScroll) ||
                            (i == OBT_SCRIPTS && m_scriptsOpen);
        BYTE bgA = 36;
        if (i == m_hoverIdx)    bgA = 90;
        else if (active)        bgA = 64;
        SolidBrush bg(Color(active ? 255 : bgA,
                            active ? 90 : 255, active ? 200 : 255, active ? 120 : 255));
        g.FillPath(&bg, &bp);

        // 图标居中：15px 见方的绘制框
        const CPoint c = br.CenterPoint();
        const CRect ib(c.x - iconPx / 2, c.y - iconPx / 2, c.x - iconPx / 2 + iconPx, c.y - iconPx / 2 + iconPx);
        DrawIcon(g, i, ib, Color(i == m_hoverIdx || active ? 255 : 215, 255, 255, 255));
    }

    PresentLayeredBitmap(m_hWnd, m_bmp, W, H, m_memDC, m_hDib, m_oldBmp, m_pBits, m_dibW, m_dibH);
    m_dirty = false;
}

// 五个按钮的矢量图标：坐标都按绘制框 0..1 归一化，随 DPI 自动缩放
//   TOP      ↑ 带回顶横线
//   BOTTOM   ↓ 带到底横线
//   START    ▶ 播放三角
//   STOP     ■ 圆角方块
//   SCRIPTS  ≡ 列表（圆点 + 横线）
void COverlayToolbar::DrawIcon(Graphics& g, int i, const CRect& box, const Color& fg) const
{
    const REAL w = (REAL)box.Width(), h = (REAL)box.Height();
    auto X = [&](REAL t) { return (REAL)box.left + t * w; };
    auto Y = [&](REAL t) { return (REAL)box.top + t * h; };

    const REAL lw = max(1.4f, 2.0f * (REAL)m_dpi / 96.0f);
    Pen pen(fg, lw);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);

    switch (i)
    {
    case OBT_TOP:
        g.DrawLine(&pen, X(0.14f), Y(0.16f), X(0.86f), Y(0.16f));   // 顶部横线
        g.DrawLine(&pen, X(0.50f), Y(0.90f), X(0.50f), Y(0.42f));   // 竖杆
        g.DrawLine(&pen, X(0.24f), Y(0.62f), X(0.50f), Y(0.36f));   // 箭头左
        g.DrawLine(&pen, X(0.76f), Y(0.62f), X(0.50f), Y(0.36f));   // 箭头右
        break;

    case OBT_BOTTOM:
        g.DrawLine(&pen, X(0.14f), Y(0.84f), X(0.86f), Y(0.84f));
        g.DrawLine(&pen, X(0.50f), Y(0.10f), X(0.50f), Y(0.58f));
        g.DrawLine(&pen, X(0.24f), Y(0.38f), X(0.50f), Y(0.64f));
        g.DrawLine(&pen, X(0.76f), Y(0.38f), X(0.50f), Y(0.64f));
        break;

    case OBT_START:
    {
        SolidBrush b(fg);
        PointF tri[3] = { PointF(X(0.30f), Y(0.12f)),
                          PointF(X(0.84f), Y(0.50f)),
                          PointF(X(0.30f), Y(0.88f)) };
        g.FillPolygon(&b, tri, 3);
        break;
    }

    case OBT_STOP:
    {
        SolidBrush b(fg);
        const REAL x0 = X(0.24f), y0 = Y(0.22f), x1 = X(0.76f), y1 = Y(0.78f);
        const REAL d = min((REAL)::MulDiv(4, m_dpi, 96), min(x1 - x0, y1 - y0));
        GraphicsPath p;
        p.AddArc(x0, y0, d, d, 180.0f, 90.0f);
        p.AddArc(x1 - d, y0, d, d, 270.0f, 90.0f);
        p.AddArc(x1 - d, y1 - d, d, d, 0.0f, 90.0f);
        p.AddArc(x0, y1 - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
        g.FillPath(&b, &p);
        break;
    }

    case OBT_SCRIPTS:
    {
        SolidBrush b(fg);
        for (int k = 0; k < 3; ++k)
        {
            const REAL cy = Y(0.26f + 0.24f * k);
            const REAL r = w * 0.075f;
            g.FillEllipse(&b, X(0.16f) - r, cy - r, r * 2, r * 2);
            g.DrawLine(&pen, X(0.40f), cy, X(0.86f), cy);
        }
        break;
    }

    default:
        break;
    }
}

BOOL COverlayToolbar::OnEraseBkgnd(CDC*) { return TRUE; }

void COverlayToolbar::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
    const int i = HitButton(point);
    if (i >= 0 && m_owner) m_owner->OnToolbarCmd(i);
}

void COverlayToolbar::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        if (::TrackMouseEvent(&tme)) m_tracking = true;
    }

    const int i = HitButton(point);
    if (i != m_hoverIdx)
    {
        m_hoverIdx = i;
        m_hover = (i >= 0);
        ::SetCursor(::LoadCursorW(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
        Render();
    }
}

void COverlayToolbar::OnMouseLeave()
{
    m_tracking = false;
    if (m_hoverIdx >= 0)
    {
        m_hoverIdx = -1;
        m_hover = false;
        Render();
    }
}

// ---------------------------------------------------------------------------
//  「切换稿件」弹出的最近文稿列表
// ---------------------------------------------------------------------------
namespace
{
    std::wstring FileNameOf(const std::wstring& path)
    {
        const size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? path : path.substr(p + 1);
    }

    std::wstring DirOf(const std::wstring& path)
    {
        const size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? std::wstring() : path.substr(0, p);
    }

    // 超宽就截断加省略号（GDI+ 的 NoWrap 不会自己省）
    std::wstring Ellipsize(Graphics& g, const std::wstring& s, Font& font, REAL maxW)
    {
        if (s.empty()) return s;
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        RectF box(0, 0, 4096, 1000);
        auto widthOf = [&](const std::wstring& t) -> REAL
        {
            RectF out;
            g.MeasureString(t.c_str(), (INT)t.size(), &font, box, &fmt, &out);
            return out.Width;
        };
        if (widthOf(s) <= maxW) return s;

        std::wstring t = s;
        const std::wstring dots = L"…";
        while (!t.empty() && widthOf(t + dots) > maxW) t.pop_back();
        return t + dots;
    }
}

BEGIN_MESSAGE_MAP(COverlayScriptMenu, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

bool COverlayScriptMenu::Create(COverlayWnd* owner)
{
    m_owner = owner;

    HDC dc = ::GetDC(nullptr);
    m_dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ::ReleaseDC(nullptr, dc);
    if (m_dpi < 96) m_dpi = 96;

    m_pad    = ::MulDiv(8, m_dpi, 96);
    m_titleH = ::MulDiv(30, m_dpi, 96);
    m_rowH   = ::MulDiv(46, m_dpi, 96);
    m_extraH = ::MulDiv(36, m_dpi, 96);
    m_w      = ::MulDiv(320, m_dpi, 96);

    LPCTSTR cls = ::AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                        ::LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    if (!CreateEx(exStyle, cls, L"提词文稿列表", WS_POPUP, CRect(0, 0, m_w, 200), owner, 0))
        return false;

    // 与浮层/按钮条一致：不进录屏与截图（调试时可放行）
    wchar_t allow[8] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_CAPTURE_ALLOWED", allow, 8) == 0 || allow[0] != L'1')
    {
        if (!::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE))
            ::SetWindowDisplayAffinity(m_hWnd, WDA_MONITOR);
    }
    return true;
}

int COverlayScriptMenu::RowCount() const
{
    // 没有最近文稿时多一条灰色提示行，占第 0 行
    return (int)m_items.size() + (m_items.empty() ? 2 : 1);
}

CRect COverlayScriptMenu::RowRect(int i) const
{
    // 行坐标减去滚动偏移（m_scrollTop = 首个可视行号）
    const int top = m_pad + m_titleH + (i - m_scrollTop) * m_rowH;
    return CRect(m_pad, top, m_w - m_pad, top + m_rowH);
}

int COverlayScriptMenu::HitRow(CPoint pt) const
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    if (!rc.PtInRect(pt)) return -1;
    const int vis = (pt.y - m_pad - m_titleH) / m_rowH;   // 可视行号
    if (vis < 0 || vis >= m_maxRows) return -1;
    const int i = vis + m_scrollTop;
    if (i < 0 || i >= RowCount()) return -1;
    return RowRect(i).PtInRect(pt) ? i : -1;
}

void COverlayScriptMenu::Relayout(const CRect& anchor)
{
    const int gap = ::MulDiv(4, m_dpi, 96);
    const int rows = RowCount();
    int h = m_pad * 2 + m_titleH + rows * m_rowH;

    MONITORINFO mi = { sizeof(mi) };
    ::GetMonitorInfoW(::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);

    // 按钮下方放不下、且上方空间更大时翻到按钮上方
    const int availBelow = mi.rcWork.bottom - (anchor.bottom + gap);
    const int availAbove = anchor.top - gap - mi.rcWork.top;
    bool above = false;
    int avail = availBelow;
    if (h > availBelow && availAbove > availBelow) { avail = availAbove; above = true; }

    // 屏幕放不下所有行：窗口压到可用高度，只显示放得下的行，其余靠滚轮滚动
    m_scrollable = false;
    m_maxRows = rows;
    if (h > avail)
    {
        const int contentAvail = avail - m_pad * 2 - m_titleH;
        m_maxRows = max(1, contentAvail / m_rowH);
        m_scrollable = true;
        h = m_pad * 2 + m_titleH + m_maxRows * m_rowH;
    }
    if (m_scrollTop > rows - m_maxRows) m_scrollTop = max(0, rows - m_maxRows);
    if (m_scrollTop < 0) m_scrollTop = 0;

    int top = above ? (anchor.top - gap - h) : (anchor.bottom + gap);
    if (top < mi.rcWork.top) top = mi.rcWork.top;
    if (top + h > mi.rcWork.bottom) top = mi.rcWork.bottom - h;

    int left = anchor.right - m_w;                                // 右边缘对齐按钮
    if (left < mi.rcWork.left) left = mi.rcWork.left;
    if (left + m_w > mi.rcWork.right) left = mi.rcWork.right - m_w;

    ::SetWindowPos(m_hWnd, HWND_TOPMOST, left, top, m_w, h,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (m_bmpW != m_w || m_bmpH != h) m_dirty = true;
}

bool COverlayScriptMenu::ShowMenu(const std::vector<std::wstring>& items,
                                  const std::wstring& current,
                                  const CRect& anchorScreen,
                                  std::function<void(int)> onPick)
{
    if (!GetSafeHwnd()) return false;

    m_items = items;
    m_current = current;
    m_onPick = std::move(onPick);
    m_hoverIdx = -1;
    m_scrollTop = 0;                // 每次弹出都回到列表顶部
    m_dirty = true;
    m_shown = true;

    Relayout(anchorScreen);
    Render();

    // 列表要自己收鼠标（浮层是穿透的，点不到）；先扣住捕获，
    // 并忽略「按下按钮那一下」的抬起，否则菜单会立刻被自己关掉
    m_waitRelease = true;
    ::SetCapture(m_hWnd);
    return true;
}

void COverlayScriptMenu::HideMenu()
{
    if (!m_shown) return;
    m_shown = false;
    m_items.clear();
    m_onPick = nullptr;

    if (::GetCapture() == m_hWnd) ::ReleaseCapture();
    ::ShowWindow(m_hWnd, SW_HIDE);
    if (m_owner) m_owner->OnScriptMenuClosed();
}

void COverlayScriptMenu::Render()
{
    if (!GetSafeHwnd() || !m_shown) return;

    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int W = rc.Width(), H = rc.Height();
    if (W < 40 || H < 20) return;

    if (!m_bmp || m_bmpW != W || m_bmpH != H)
    {
        delete m_bmp;
        m_bmp = new Bitmap(W, H, PixelFormat32bppARGB);
        m_bmpW = W;
        m_bmpH = H;
    }
    if (!m_bmp) return;

    Graphics g(m_bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(0, 0, 0, 0));

    auto roundRect = [](const CRect& r0, REAL rad, GraphicsPath& p)
    {
        const REAL x = (REAL)r0.left, y = (REAL)r0.top;
        const REAL w = (REAL)r0.Width(), h = (REAL)r0.Height();
        const REAL d = min(rad * 2.0f, min(w, h));
        if (d <= 1.0f) { p.AddRectangle(RectF(x, y, w, h)); return; }
        p.AddArc(x, y, d, d, 180.0f, 90.0f);
        p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
    };

    // 面板底
    GraphicsPath panel;
    roundRect(rc, (REAL)::MulDiv(9, m_dpi, 96), panel);
    SolidBrush bg(Color(240, 16, 20, 30));
    g.FillPath(&bg, &panel);
    Pen border(Color(70, 255, 255, 255), 1.0f);
    g.DrawPath(&border, &panel);

    FontFamily fam(L"Microsoft YaHei UI");
    Font fTitle(&fam, (REAL)::MulDiv(12, m_dpi, 96), FontStyleBold, UnitPixel);
    Font fName(&fam, (REAL)::MulDiv(13, m_dpi, 96), FontStyleRegular, UnitPixel);
    Font fPath(&fam, (REAL)::MulDiv(10, m_dpi, 96), FontStyleRegular, UnitPixel);
    StringFormat fmtLeft(StringFormat::GenericTypographic());
    fmtLeft.SetAlignment(StringAlignmentNear);
    fmtLeft.SetLineAlignment(StringAlignmentCenter);
    fmtLeft.SetFormatFlags(StringFormatFlagsNoWrap);

    SolidBrush fgTitle(Color(210, 190, 200, 215));
    g.DrawString(L"最近打开的文稿", -1, &fTitle,
                 RectF((REAL)m_pad + 4, 0, (REAL)W - m_pad * 2, (REAL)m_titleH), &fmtLeft, &fgTitle);

    // 标题下的分隔线
    Pen sep(Color(34, 255, 255, 255), 1.0f);
    g.DrawLine(&sep, (REAL)m_pad, (REAL)(m_pad + m_titleH) - 0.5f,
               (REAL)(W - m_pad), (REAL)(m_pad + m_titleH) - 0.5f);

    const int extraIdx = m_items.empty() ? 1 : (int)m_items.size();
    const REAL textLeft = (REAL)(m_pad + ::MulDiv(8, m_dpi, 96));
    const REAL textMaxW = (REAL)(W - m_pad * 2 - ::MulDiv(28, m_dpi, 96));

    // 滚动时只画可视范围内的行（m_maxRows 在 Relayout 里已按窗口高度算好）
    const int total = RowCount();
    if (m_maxRows <= 0) m_maxRows = total;
    const int lastRow = min(total, m_scrollTop + m_maxRows);
    for (int i = m_scrollTop; i < lastRow; ++i)
    {
        const CRect row = RowRect(i);
        const bool isExtra = (i == extraIdx);
        const bool isHint = (m_items.empty() && i == 0);

        if (i == m_hoverIdx && !isHint)
        {
            GraphicsPath hp;
            CRect hr = row;
            hr.DeflateRect(::MulDiv(2, m_dpi, 96), ::MulDiv(2, m_dpi, 96));
            roundRect(hr, (REAL)::MulDiv(6, m_dpi, 96), hp);
            SolidBrush hb(Color(52, 120, 190, 255));
            g.FillPath(&hb, &hp);
        }

        if (isHint)
        {
            SolidBrush fg(Color(150, 190, 200, 215));
            g.DrawString(L"（还没有打开过文稿，可在面板里打开一篇）", -1, &fPath,
                         RectF(textLeft, (REAL)row.top, textMaxW, (REAL)row.Height()),
                         &fmtLeft, &fg);
            continue;
        }

        if (isExtra)
        {
            SolidBrush fg(Color(i == m_hoverIdx ? 255 : 205, 140, 200, 255));
            g.DrawString(L"＋  打开其他文稿…", -1, &fName,
                         RectF(textLeft, (REAL)row.top, textMaxW, (REAL)row.Height()),
                         &fmtLeft, &fg);
            continue;
        }

        // 最近文稿：第一行文件名，第二行灰色目录；正在用的这篇前面打勾
        const std::wstring& p = m_items[i];
        const bool isCur = !m_current.empty() && ::_wcsicmp(p.c_str(), m_current.c_str()) == 0;
        const bool exists = ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;

        if (isCur)
        {
            SolidBrush mark(Color(255, 120, 200, 255));
            RectF mk((REAL)(m_pad + 2), (REAL)row.top, 12.0f, (REAL)row.Height());
            g.DrawString(L"✓", -1, &fName, mk, &fmtLeft, &mark);
        }

        const REAL nameY = (REAL)row.top + ::MulDiv(4, m_dpi, 96);
        const std::wstring name = Ellipsize(g, FileNameOf(p), fName, textMaxW);
        SolidBrush fgName(Color(isCur ? 255 : (exists ? 235 : 130),
                                255, exists ? 255 : 200, exists ? 255 : 200));
        g.DrawString(name.c_str(), -1, &fName,
                     RectF(textLeft, nameY, textMaxW, (REAL)::MulDiv(19, m_dpi, 96)),
                     &fmtLeft, &fgName);

        const std::wstring dir = exists ? DirOf(p) : L"（文件已不存在）";
        SolidBrush fgPath(Color(exists ? 130 : 170, 175, 190, 205));
        g.DrawString(Ellipsize(g, dir, fPath, textMaxW).c_str(), -1, &fPath,
                     RectF(textLeft, (REAL)row.top + ::MulDiv(24, m_dpi, 96), textMaxW,
                           (REAL)::MulDiv(16, m_dpi, 96)),
                     &fmtLeft, &fgPath);
    }

    // 右侧细滚动条：条目超出可视行数时出现（轨道 + 圆角滑块）
    if (m_scrollable && total > m_maxRows)
    {
        const REAL barW = (REAL)::MulDiv(4, m_dpi, 96);
        const REAL trackX = (REAL)W - m_pad / 2 - barW;
        const REAL trackTop = (REAL)(m_pad + m_titleH + ::MulDiv(6, m_dpi, 96));
        const REAL trackH = (REAL)(H - m_pad - ::MulDiv(6, m_dpi, 96)) - trackTop;
        const int maxScrollRows = total - m_maxRows;
        if (trackH > 0 && maxScrollRows > 0)
        {
            const REAL thumbH = max(barW * 3.0f, trackH * m_maxRows / (REAL)total);
            const REAL thumbY = trackTop +
                (trackH - thumbH) * m_scrollTop / (REAL)maxScrollRows;

            GraphicsPath track;
            roundRect(CRect((int)trackX, (int)trackTop, (int)(trackX + barW),
                            (int)(trackTop + trackH)), barW, track);
            SolidBrush brTrack(Color(36, 255, 255, 255));
            g.FillPath(&brTrack, &track);

            GraphicsPath thumb;
            roundRect(CRect((int)trackX, (int)thumbY, (int)(trackX + barW),
                            (int)(thumbY + thumbH)), barW, thumb);
            SolidBrush brThumb(Color(150, 255, 255, 255));
            g.FillPath(&brThumb, &thumb);
        }
    }

    PresentLayeredBitmap(m_hWnd, m_bmp, W, H, m_memDC, m_hDib, m_oldBmp, m_pBits, m_dibW, m_dibH);
    m_dirty = false;
}

BOOL COverlayScriptMenu::OnEraseBkgnd(CDC*) { return TRUE; }

void COverlayScriptMenu::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    if (!rc.PtInRect(point)) { HideMenu(); return; }   // 点窗外就收起

    const int i = HitRow(point);
    if (i < 0) return;

    int pick = -1;
    if (m_items.empty())
    {
        if (i == 1) pick = 0;                          // 只有「打开其他文稿…」
    }
    else
    {
        pick = (i < (int)m_items.size()) ? i : (int)m_items.size();
    }
    if (pick < 0) return;

    auto cb = m_onPick;
    HideMenu();                                        // 先收起再回调（回调可能弹模态对话框）
    if (cb) cb(pick);
}

void COverlayScriptMenu::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
    // 弹出菜单的那一下鼠标抬起会先飘到本窗口，忽略掉，免得刚开就点了第一项
    if (m_waitRelease) m_waitRelease = false;
}

BOOL COverlayScriptMenu::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    // 菜单一直握着鼠标捕获，滚轮消息会送到这里；滚一格 = 一行
    const int total = RowCount();
    if (!m_scrollable || total <= m_maxRows) return TRUE;
    const int maxScrollRows = total - m_maxRows;
    const int st = max(0, min(m_scrollTop - zDelta / 120, maxScrollRows));
    if (st != m_scrollTop)
    {
        m_scrollTop = st;
        if (m_hoverIdx >= m_scrollTop + m_maxRows || m_hoverIdx < m_scrollTop)
        {
            m_hoverIdx = -1;
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        }
        Render();
    }
    return TRUE;
}

void COverlayScriptMenu::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        if (::TrackMouseEvent(&tme)) m_tracking = true;
    }

    const int i = HitRow(point);
    if (i != m_hoverIdx)
    {
        m_hoverIdx = i;
        ::SetCursor(::LoadCursorW(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
        Render();
    }
}

void COverlayScriptMenu::OnMouseLeave()
{
    m_tracking = false;
    if (m_hoverIdx >= 0)
    {
        m_hoverIdx = -1;
        Render();
    }
}

void COverlayScriptMenu::OnCaptureChanged(CWnd* /*pWnd*/)
{
    if (m_waitRelease) return;    // 刚弹出，按钮抬起时系统会重定向，不算丢失
    if (m_shown) HideMenu();
}

BEGIN_MESSAGE_MAP(COverlayWnd, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()    ON_WM_TIMER()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_NCHITTEST()
    ON_WM_MOUSEWHEEL()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_CAPTURECHANGED()
    ON_WM_SETCURSOR()
END_MESSAGE_MAP()

COverlayWnd::COverlayWnd() = default;

COverlayWnd::~COverlayWnd()
{
    if (m_pBmp) { delete m_pBmp; m_pBmp = nullptr; }
    if (m_memDC)
    {
        if (m_oldBmp) ::SelectObject(m_memDC, m_oldBmp);
        ::DeleteDC(m_memDC);
    }
    if (m_hDib) ::DeleteObject(m_hDib);
}

bool COverlayWnd::Create()
{
    LPCTSTR cls = ::AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                       ::LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);

    // 鼠标穿透可选：默认穿透（不挡下层 PPT / 白板），关掉后锁定态也能用滚轮
    m_clickThrough = m_settings ? m_settings->clickThrough : true;
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    if (m_clickThrough) exStyle |= WS_EX_TRANSPARENT;
    CRect rc(0, 0, 620, 460);
    if (!CreateEx(exStyle, cls, L"芸豆提词", WS_POPUP, rc, nullptr, 0))
        return false;

    {
        HDC dc = ::GetDC(nullptr);
        m_dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
        ::ReleaseDC(nullptr, dc);
        if (m_dpi < 96) m_dpi = 96;
    }
    m_bar.Create(this);      // 右侧滚动条（独立小窗，锁定态也能拖）
    m_toolbar.Create(this);  // 顶部按钮条（返回页首 / 到页尾 / 滚动 / 停止滚动）

    // 恢复上次位置；越界则用默认位置
    bool placed = false;
    if (m_settings)
    {
        CRect sr = m_settings->overlayRect;
        if (sr.Width() > 200 && sr.Height() > 150)
        {
            CRect virt(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0);
            virt.right = virt.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            virt.bottom = virt.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            CRect inter;
            if (inter.IntersectRect(&sr, &virt))
            {
                ApplyRect(sr);
                placed = true;
            }
        }
    }
    if (!placed) ApplyDefaultRect();

    return true;
}

void COverlayWnd::ApplyDefaultRect()
{
    CRect work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = (int)(work.Width() * 0.46f);
    int h = (int)(work.Height() * 0.78f);
    if (w < 380) w = 380;
    if (h < 320) h = 320;
    CRect rc(work.right - w - 24, work.top + 48, work.right - 24, work.top + 48 + h);
    ApplyRect(rc);
}

void COverlayWnd::ApplyRect(const CRect& rc)
{
    if (!GetSafeHwnd()) return;
    CRect r = rc;
    if (r.Width() < kMinW) r.right = r.left + kMinW;
    if (r.Height() < kMinH) r.bottom = r.top + kMinH;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, r.left, r.top, r.Width(), r.Height(),
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SyncBar();
    MarkDirty();
}

CRect COverlayWnd::CurrentRect() const
{
    CRect rc(0, 0, 0, 0);
    if (GetSafeHwnd()) ::GetWindowRect(m_hWnd, &rc);
    return rc;
}

int COverlayWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // 录屏 / 屏幕共享 / 截图 中不可见（Win10 2004+），旧系统退化为纯黑遮挡
    // 调试时可设置环境变量 TP_CAPTURE_ALLOWED=1 关闭该保护，方便截图排查渲染问题
    wchar_t allow[8] = { 0 };
    if (::GetEnvironmentVariableW(L"TP_CAPTURE_ALLOWED", allow, 8) == 0 || allow[0] != L'1')
    {
        if (!::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE))
            ::SetWindowDisplayAffinity(m_hWnd, WDA_MONITOR);
    }

    SetTimer(1, 33, nullptr);
    return 0;
}

void COverlayWnd::OnDestroy()
{
    KillTimer(1);
    if (m_scriptMenu.GetSafeHwnd()) m_scriptMenu.DestroyWindow();
    if (m_bar.GetSafeHwnd()) m_bar.DestroyWindow();
    if (m_toolbar.GetSafeHwnd()) m_toolbar.DestroyWindow();
    CWnd::OnDestroy();
}

void COverlayWnd::ShowOverlay(bool show)
{
    if (!GetSafeHwnd()) return;
    if (show)
    {
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SyncBar();
        MarkDirty();
    }
    else
    {
        HideScriptMenu();
        m_tbHover = false;
        m_tbLeaveAt = 0;
        m_bar.ShowBar(false);
        m_toolbar.ShowBar(false);
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
}

void COverlayWnd::SyncBar()
{
    if (!m_bar.GetSafeHwnd() || !GetSafeHwnd()) return;
    CRect rc;
    ::GetWindowRect(m_hWnd, &rc);
    m_bar.SyncTo(rc, m_interactive);

    if (m_toolbar.GetSafeHwnd())
    {
        m_toolbar.SetAutoScroll(m_settings && m_settings->autoScroll);
        m_toolbar.SyncTo(rc, m_interactive);
    }
}

void COverlayWnd::UpdateBarMetrics(int H, int contentH, float maxScroll)
{
    m_maxScrollCur = maxScroll;
    m_lastContentH = (float)contentH;
    const bool show = m_settings && m_settings->visible && contentH > 0 &&
                      maxScroll > 1.0f && ::IsWindowVisible(m_hWnd);
    m_bar.ShowBar(show);
    if (show) m_bar.SetMetrics(m_scrollY, maxScroll, (float)H, (float)contentH);

    // 按钮条是否显示由悬停状态统一裁决（鼠标在浮层上才显示）
    ApplyToolbarVisibility();
}

void COverlayWnd::SetInteractive(bool on)
{
    m_interactive = on;
    if (!GetSafeHwnd()) return;
    LONG_PTR ex = ::GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (on || !m_clickThrough) ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
    else                       ex |= WS_EX_TRANSPARENT;
    ::SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ex);
    ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SyncBar();
    MarkDirty();
}

void COverlayWnd::SetClickThrough(bool on)
{
    if (m_clickThrough == on) return;
    m_clickThrough = on;
    if (!GetSafeHwnd()) return;
    LONG_PTR ex = ::GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
    if (on && !m_interactive) ex |= WS_EX_TRANSPARENT;    // 开启穿透且不在调整模式
    else                      ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
    ::SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ex);
    ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    MarkDirty();
}

void COverlayWnd::Reanchor(int origChar)
{
    m_anchorChar = origChar;
    m_anchorValid = true;
    m_followAnchor = true;
    m_lastAnchorTick = ::GetTickCount();
    m_followPaused = false;
    m_dirty = true;
}

void COverlayWnd::SetVoiceActive(bool on)
{
    if (on == m_voiceActive) return;
    m_voiceActive = on;
    if (!on) m_voiceOffTick = ::GetTickCount();
    else m_followPaused = false;
}

void COverlayWnd::ResetAnchor()
{
    // 换稿 / 回顶部后不再高亮任何行，等语音重新匹配上再恢复
    m_anchorChar = 0;
    m_anchorValid = false;
    m_dirty = true;
}

bool COverlayWnd::ScrollByPixels(float dy)
{
    m_followAnchor = false;
    const float before = m_scrollY;
    m_scrollY += dy;
    if (m_scrollY < 0) m_scrollY = 0;
    if (m_maxScrollCur > 0 && m_scrollY > m_maxScrollCur) m_scrollY = m_maxScrollCur;
    m_targetScroll = m_scrollY;
    m_animating = false;
    m_followV = 0.0f;      // 手动滚动后清掉跟随惯性，避免松手还在滑
    m_followPaused = false;
    MarkDirty();
    return fabsf(m_scrollY - before) > 0.01f;
}

void COverlayWnd::ScrollToPixel(float y)
{
    m_followAnchor = false;
    if (y < 0) y = 0;
    if (m_maxScrollCur > 0.5f && y > m_maxScrollCur) y = m_maxScrollCur;
    m_scrollY = y;
    m_targetScroll = y;
    m_animating = false;
    m_followV = 0.0f;
    m_followPaused = false;
    // 手动拖动滚动条后，把锚点同步到当前首行，避免语音匹配时突然跳回去
    if (m_doc && !m_doc->Empty() && m_doc->LineHeight() > 0)
    {
        const int line = (int)(m_scrollY / m_doc->LineHeight());
        m_anchorChar = m_doc->CharIndexOfLine(line);
    }
    MarkDirty();
}

void COverlayWnd::ScrollToTop()
{
    m_followAnchor = false;
    m_scrollY = m_targetScroll = 0.0f;
    m_anchorChar = 0;
    m_anchorValid = false;
    m_animating = false;
    m_followV = 0.0f;
    m_followPaused = false;
    MarkDirty();
}

// 整页滚动（语音命令「下一页 / 上一页」、点滚动条轨道都走这里）
int COverlayWnd::ScrollPage(int dir)
{
    if (!GetSafeHwnd() || !m_doc || m_doc->Empty()) return -1;

    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int lineH = m_doc->LineHeight();
    if (lineH <= 0 || rc.Height() <= 0) return TopCharIndex();

    // 留一行重叠：翻页后还能看到上一页的最后一行，念到哪儿不丢
    int page = rc.Height() - lineH;
    if (page < lineH * 2) page = lineH * 2;

    float y = m_scrollY + (dir > 0 ? (float)page : -(float)page);
    // 对齐到行边界，翻完页每一行都完整落在窗口里，不会出现半行
    y = floorf(y / (float)lineH + 0.5f) * (float)lineH;

    ScrollToPixel(y);   // 内部会把锚点同步到首行，跟读不会把画面拽回去
    return TopCharIndex();
}

int COverlayWnd::TopCharIndex() const
{
    if (!m_doc || m_doc->Empty() || m_doc->LineHeight() <= 0) return -1;
    const int line = (int)(m_scrollY / (float)m_doc->LineHeight());
    return m_doc->CharIndexOfLine(line);
}

// 滚到页尾：锚点同步到最后一行行首，返回该字符索引（供调用方同步跟读位置）
int COverlayWnd::ScrollToBottom()
{
    if (!GetSafeHwnd() || !m_doc || m_doc->Empty()) return -1;

    m_followAnchor = false;
    m_scrollY = m_targetScroll = m_maxScrollCur;
    m_animating = false;
    m_followV = 0.0f;
    m_followPaused = false;

    const int lineH = m_doc->LineHeight();
    if (lineH <= 0) return -1;
    const int lastLine = max(0, (m_doc->TotalHeight() - 1) / lineH);
    m_anchorChar = m_doc->CharIndexOfLine(lastLine);
    m_anchorValid = true;
    MarkDirty();
    return m_anchorChar;
}

// 顶部按钮条命令：转发给对话框（那里有 matcher / 设置，才能把跟读位置一起带过去）
void COverlayWnd::OnToolbarCmd(int id)
{
    if (m_toolbarCmd) m_toolbarCmd(id);
}

// 「切换稿件」：弹 / 收最近文稿列表
bool COverlayWnd::ToggleScriptMenu(const std::vector<std::wstring>& items,
                                   const std::wstring& current,
                                   std::function<void(int)> onPick)
{
    if (!GetSafeHwnd()) return false;

    if (m_scriptMenu.MenuVisible())
    {
        HideScriptMenu();
        return false;
    }
    if (!m_scriptMenu.GetSafeHwnd() && !m_scriptMenu.Create(this)) return false;

    const CRect anchor = m_toolbar.ButtonScreenRect(COverlayToolbar::OBT_SCRIPTS);
    const bool shown = m_scriptMenu.ShowMenu(items, current, anchor, std::move(onPick));
    if (m_toolbar.GetSafeHwnd()) m_toolbar.SetScriptsOpen(shown);
    return shown;
}

void COverlayWnd::HideScriptMenu()
{
    if (m_scriptMenu.GetSafeHwnd()) m_scriptMenu.HideMenu();
    OnScriptMenuClosed();
}

void COverlayWnd::OnScriptMenuClosed()
{
    if (m_toolbar.GetSafeHwnd()) m_toolbar.SetScriptsOpen(false);
}

BOOL COverlayWnd::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    if (!m_interactive && m_clickThrough) return TRUE;   // 锁定 + 穿透：不响应鼠标
    ScrollByPixels(-zDelta * 0.55f);
    return TRUE;
}

BOOL COverlayWnd::OnEraseBkgnd(CDC* /*pDC*/) { return TRUE; }

int COverlayWnd::HitZone(CPoint pt) const
{
    CRect rc;
    ::GetClientRect(m_hWnd, &rc);
    const int b = 14;      // 边缘抓取带宽度
    if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom) return RZ_NONE;

    int z = RZ_NONE;
    if (pt.x < b)                 z |= RZ_L;
    else if (pt.x > rc.right - b) z |= RZ_R;
    if (pt.y < b)                 z |= RZ_T;
    else if (pt.y > rc.bottom - b) z |= RZ_B;
    return z;
}

LRESULT COverlayWnd::OnNcHitTest(CPoint point)
{
    if (m_interactive)
    {
        CPoint pt = point;
        ScreenToClient(&pt);
        if (HitZone(pt) != RZ_NONE)
            return HTCLIENT;                   // 边缘交给自定义缩放处理
        return HTCAPTION;                      // 内部整块拖动
    }

    if (!m_clickThrough)
        return HTCLIENT;                       // 穿透已关：滚轮/点击都落在浮层上

    return HTTRANSPARENT;                      // 锁定态：鼠标完全穿透
}

BOOL COverlayWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (m_interactive)
    {
        CPoint pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        const int z = (m_dragZone != RZ_NONE) ? m_dragZone : HitZone(pt);

        // 只有贴近边缘/四角的抓取带才换成对应缩放光标；内部保持普通箭头
        // （整块都能拖动，但没必要让整个浮层变成四向箭头，看着像坏掉了）
        LPCTSTR id = IDC_ARROW;
        if (z == (RZ_L | RZ_T) || z == (RZ_R | RZ_B))      id = IDC_SIZENWSE;
        else if (z == (RZ_R | RZ_T) || z == (RZ_L | RZ_B)) id = IDC_SIZENESW;
        else if (z & (RZ_L | RZ_R))                        id = IDC_SIZEWE;
        else if (z & (RZ_T | RZ_B))                        id = IDC_SIZENS;

        ::SetCursor(::LoadCursorW(nullptr, id));
        return TRUE;
    }
    if (!m_clickThrough)
    {
        // 锁定 + 非穿透：这里只能滚轮滚动 / 按住拖动内容，不改窗口大小，
        // 光标保持普通箭头（原来给 SIZENS 会让整块浮层像在等你去拉高度）
        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void COverlayWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (m_interactive)
    {
        const int z = HitZone(point);
        if (z != RZ_NONE)
        {
            m_dragZone = z;
            m_dragStartRect = CurrentRect();
            ::GetCursorPos(&m_dragStartScreen);
            SetCapture();
            return;
        }
    }
    if (!m_clickThrough)
    {
        // 锁定 + 非穿透：按住拖动 = 拖动内容（跟手，向上拖内容上移）
        m_panning = true;
        ::GetCursorPos(&m_panLastScreen);
        SetCapture();
        return;
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void COverlayWnd::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_dragZone != RZ_NONE && (nFlags & MK_LBUTTON))
    {
        CPoint sp;
        ::GetCursorPos(&sp);
        const int dx = sp.x - m_dragStartScreen.x;
        const int dy = sp.y - m_dragStartScreen.y;

        CRect r = m_dragStartRect;
        if (m_dragZone & RZ_L)  r.left = min(m_dragStartRect.right - kMinW, m_dragStartRect.left + dx);
        if (m_dragZone & RZ_R)
        {
            r.left = m_dragStartRect.left;
            r.right = max(m_dragStartRect.left + kMinW, m_dragStartRect.right + dx);
        }
        if (m_dragZone & RZ_T)  r.top = min(m_dragStartRect.bottom - kMinH, m_dragStartRect.top + dy);
        if (m_dragZone & RZ_B)
        {
            r.top = m_dragStartRect.top;
            r.bottom = max(m_dragStartRect.top + kMinH, m_dragStartRect.bottom + dy);
        }

        // 不允许拖出屏幕工作区
        CRect work;
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (r.left < work.left)          r.left = work.left;
        if (r.top < work.top)            r.top = work.top;
        if (r.right > work.right)        r.right = work.right;
        if (r.bottom > work.bottom)      r.bottom = work.bottom;
        if (r.Width() < kMinW)           r.right = r.left + kMinW;
        if (r.Height() < kMinH)          r.bottom = r.top + kMinH;

        ApplyRect(r);
        return;
    }
    if (m_panning && (nFlags & MK_LBUTTON))
    {
        CPoint sp;
        ::GetCursorPos(&sp);
        ScrollByPixels((float)(m_panLastScreen.y - sp.y));   // 1:1 跟手
        m_panLastScreen = sp;
        return;
    }
    CWnd::OnMouseMove(nFlags, point);
}

void COverlayWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_dragZone != RZ_NONE)
    {
        m_dragZone = RZ_NONE;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        if (m_settings) ::GetWindowRect(m_hWnd, &m_settings->overlayRect);
        MarkDirty();
        return;
    }
    if (m_panning)
    {
        m_panning = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        return;
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void COverlayWnd::OnCaptureChanged(CWnd* pWnd)
{
    m_dragZone = RZ_NONE;
    m_panning = false;
    CWnd::OnCaptureChanged(pWnd);
}

void COverlayWnd::ResizeStep(int dw, int dh)
{
    if (!GetSafeHwnd()) return;

    CRect rc = CurrentRect();
    if (rc.Width() < 16 || rc.Height() < 16) return;

    int stepW = rc.Width() / 12;  if (stepW < 56) stepW = 56;
    int stepH = rc.Height() / 12; if (stepH < 40) stepH = 40;

    int nw = rc.Width() + (dw > 0 ? stepW : (dw < 0 ? -stepW : 0));
    int nh = rc.Height() + (dh > 0 ? stepH : (dh < 0 ? -stepH : 0));

    CRect work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    if (nw < kMinW) nw = kMinW;
    if (nh < kMinH) nh = kMinH;
    if (nw > work.Width()) nw = work.Width();
    if (nh > work.Height()) nh = work.Height();

    // 靠屏幕右侧 / 下侧时以右 / 下边为锚点，避免越调越跑出屏幕
    CRect r = rc;
    const bool anchorRight  = (rc.left + rc.right) / 2 > (work.left + work.right) / 2;
    const bool anchorBottom = (rc.top + rc.bottom) / 2 > (work.top + work.bottom) / 2;
    if (anchorRight) { r.right = rc.right; r.left = rc.right - nw; }
    else             { r.left = rc.left;   r.right = rc.left + nw; }
    if (anchorBottom) { r.bottom = rc.bottom; r.top = rc.bottom - nh; }
    else              { r.top = rc.top;       r.bottom = rc.top + nh; }

    if (r.left < work.left) { r.left = work.left; r.right = r.left + nw; }
    if (r.top < work.top) { r.top = work.top; r.bottom = r.top + nh; }
    if (r.right > work.right) { r.right = work.right; r.left = r.right - nw; }
    if (r.bottom > work.bottom) { r.bottom = work.bottom; r.top = r.bottom - nh; }

    ApplyRect(r);
    if (m_settings) ::GetWindowRect(m_hWnd, &m_settings->overlayRect);
}

void COverlayWnd::SetSizeRatio(int wPercent, int hPercent)
{
    if (!GetSafeHwnd()) return;
    CRect work;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    CRect rc = CurrentRect();
    if (wPercent < 5) wPercent = 5;
    if (wPercent > 100) wPercent = 100;
    if (hPercent < 5) hPercent = 5;
    if (hPercent > 100) hPercent = 100;

    const int nw = max(kMinW, work.Width() * wPercent / 100);
    const int nh = max(kMinH, work.Height() * hPercent / 100);
    CRect r(rc.left, rc.top, rc.left + nw, rc.top + nh);
    if (r.right > work.right) r.OffsetRect(work.right - r.right, 0);
    if (r.bottom > work.bottom) r.OffsetRect(0, work.bottom - r.bottom);
    if (r.left < work.left) r.OffsetRect(work.left - r.left, 0);
    if (r.top < work.top) r.OffsetRect(0, work.top - r.top);
    ApplyRect(r);
    if (m_settings) ::GetWindowRect(m_hWnd, &m_settings->overlayRect);
}

void COverlayWnd::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CWnd::OnWindowPosChanged(lpwndpos);
    if ((lpwndpos->flags & (SWP_NOSIZE | SWP_NOMOVE)) != (SWP_NOSIZE | SWP_NOMOVE))
    {
        // 浮层动了，弹出中的文稿列表就不跟手了，直接收起
        if (m_scriptMenu.MenuVisible()) HideScriptMenu();
        // 位置与尺寸都实时保存（包括拖动缩放过程中）
        if (m_settings) ::GetWindowRect(m_hWnd, &m_settings->overlayRect);
        SyncBar();
        MarkDirty();
    }
}

// ---- 顶部按钮条悬停显隐 ----------------------------------------------
// 浮层锁定时是穿透窗口，收不到鼠标消息，所以用轮询 GetCursorPos 的方式
// 判断鼠标是否在浮层区域上（33ms 定时器本来就在跑，开销可忽略）。

// 鼠标是否在「浮层 ∪ 按钮条」区域内。按钮条贴在浮层上边缘外侧，
// 鼠标从浮层移到按钮条上要经过 4px 缝隙，所以按钮条显示时也算在区域内；
// 文稿列表开着时不隐藏（不然按钮条会从菜单脚下溜走）。
bool COverlayWnd::MouseOverOverlayArea() const
{
    if (!GetSafeHwnd()) return false;
    if (m_scriptMenu.MenuVisible()) return true;

    POINT pt;
    if (!::GetCursorPos(&pt)) return false;

    CRect rc;
    if (::GetWindowRect(m_hWnd, &rc) && rc.PtInRect(pt)) return true;

    if (m_toolbar.GetSafeHwnd() && ::IsWindowVisible(m_toolbar.m_hWnd))
    {
        CRect tb;
        if (::GetWindowRect(m_toolbar.m_hWnd, &tb) && tb.PtInRect(pt)) return true;
    }
    return false;
}

void COverlayWnd::UpdateToolbarHover()
{
    if (!m_toolbar.GetSafeHwnd()) return;

    const bool over = MouseOverOverlayArea();
    const DWORD now = ::GetTickCount();

    if (over)
    {
        m_tbLeaveAt = 0;
        if (!m_tbHover) { m_tbHover = true; ApplyToolbarVisibility(); }
    }
    else if (m_tbHover)
    {
        // 离开后延迟 350ms 再隐藏：既给「移向按钮条」留时间，也不至于拖泥带水
        if (m_tbLeaveAt == 0)      m_tbLeaveAt = now + 350;
        else if (now >= m_tbLeaveAt)
        {
            m_tbLeaveAt = 0;
            m_tbHover = false;
            ApplyToolbarVisibility();
        }
    }
}

void COverlayWnd::ApplyToolbarVisibility()
{
    if (!m_toolbar.GetSafeHwnd() || !GetSafeHwnd()) return;

    const bool base = m_settings && m_settings->visible &&
                      m_lastContentH > 0 && ::IsWindowVisible(m_hWnd);
    const bool show = base && m_tbHover;

    if (!show && m_scriptMenu.MenuVisible()) HideScriptMenu();
    m_toolbar.ShowBar(show);
}

void COverlayWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        const DWORD now = ::GetTickCount();
        float dt = m_lastTick ? (now - m_lastTick) / 1000.0f : 0.033f;
        m_lastTick = now;
        if (dt > 0.12f) dt = 0.12f;     // 窗口被遮挡/系统卡顿时不要一步跳很远
        if (dt < 0.005f) dt = 0.005f;

        if (m_smartFollow && m_followAnchor && m_doc && !m_doc->Empty())
        {
            // ---------------- 语音跟随（智能提词） ----------------
            // 目标位置由识别结果决定：读到哪、目标就在哪。
            // 速度用「比例控制 + 加速度限幅」逼近目标，所以画面是连续滑行，
            // 而不是一句一跳；静音后目标不再推进，速度自然衰减到 0 → 暂停。
            const int lineH = m_doc->LineHeight();
            const DWORD sinceAnchor = m_lastAnchorTick ? (now - m_lastAnchorTick) : 0xFFFFFFFF;

            // 「正在读」：麦克风有人声，或刚匹配到新位置（识别本身有 0.3~0.8s 延迟，给点宽限）
            const bool reading = m_voiceActive || sinceAnchor < 1200;
            m_followPaused = !reading;

            float gap = reading ? (m_targetScroll - m_scrollY) : 0.0f;

            float vDes = gap * 4.0f;                        // 比例项：离目标越远走得越快
            const float vMax = (float)lineH * 6.5f;         // 上限≈6.5 行/秒
            if (vDes >  vMax) vDes =  vMax;
            if (vDes < -vMax) vDes = -vMax;

            float dv = vDes - m_followV;
            const float dvMax = (float)lineH * 22.0f * dt;  // 加速度限幅：起步柔和、停读干脆
            if (dv >  dvMax) dv =  dvMax;
            if (dv < -dvMax) dv = -dvMax;
            m_followV += dv;

            if (m_followV > -0.5f && m_followV < 0.5f) m_followV = 0.0f;

            if (m_followV != 0.0f)
            {
                m_scrollY += m_followV * dt;
                if (m_scrollY < 0.0f)
                {
                    m_scrollY = 0.0f;
                    m_followV = 0.0f;
                }
                if (m_maxScrollCur > 0.5f && m_scrollY > m_maxScrollCur)
                {
                    m_scrollY = m_maxScrollCur;
                    if (m_followV > 0.0f) m_followV = 0.0f;   // 到底了别继续攒速度
                }
                m_dirty = true;
            }
            m_animating = false;
        }
        else if (m_animating)
        {
            float d = m_targetScroll - m_scrollY;
            if (fabsf(d) < 0.6f)
            {
                m_scrollY = m_targetScroll;
                m_animating = false;
            }
            else
            {
                m_scrollY += d * 0.24f;
            }
            m_dirty = true;
        }
        else
        {
            m_followV = 0.0f;
            m_followPaused = false;
        }

        if (m_dirty)
        {
            m_dirty = false;
            Render();
        }

        // 顶部按钮条：跟随鼠标悬停显隐（轮询，锁定穿透时也能工作）
        UpdateToolbarHover();
    }
    CWnd::OnTimer(nIDEvent);
}

void COverlayWnd::EnsureBackBuffer(int w, int h)
{
    if (!m_pBmp || m_bmpW != w || m_bmpH != h)
    {
        delete m_pBmp;
        m_pBmp = new Bitmap(w, h, PixelFormat32bppARGB);
        m_bmpW = w;
        m_bmpH = h;
    }

    if (!m_memDC)
    {
        HDC screen = ::GetDC(nullptr);
        m_memDC = ::CreateCompatibleDC(screen);
        ::ReleaseDC(nullptr, screen);
    }

    if (!m_hDib || m_dibW != w || m_dibH != h)
    {
        if (m_oldBmp) { ::SelectObject(m_memDC, m_oldBmp); m_oldBmp = nullptr; }
        if (m_hDib) { ::DeleteObject(m_hDib); m_hDib = nullptr; }

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;      // 自上而下
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        m_hDib = ::CreateDIBSection(m_memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        m_pBits = bits;
        m_dibW = w;
        m_dibH = h;
        if (m_hDib) m_oldBmp = (HBITMAP)::SelectObject(m_memDC, m_hDib);
    }
}

void COverlayWnd::Render()
{
    if (!GetSafeHwnd() || !m_settings) return;

    CRect rc;
    ::GetWindowRect(m_hWnd, &rc);
    const int W = rc.Width();
    const int H = rc.Height();
    if (W < 16 || H < 16) return;

    EnsureBackBuffer(W, H);
    if (!m_pBmp) return;

    Graphics g(m_pBmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(Color(0, 0, 0, 0));

    DrawContent(g, W, H);

    // ---- 逐像素后处理：垂直边缘渐隐 + 预乘 alpha ----
    Rect lockRect(0, 0, W, H);
    BitmapData bd;
    if (m_pBmp->LockBits(&lockRect, ImageLockModeRead | ImageLockModeWrite,
                         PixelFormat32bppARGB, &bd) == Ok)
    {
        BYTE* base = (BYTE*)bd.Scan0;
        const int stride = bd.Stride;
        int fadeH = H / 7;
        if (fadeH > 64) fadeH = 64;
        if (fadeH < 8) fadeH = 8;

        // 边缘渐隐强度跟不透明度挂钩：越透明渐隐越明显（像融进桌面），
        // 完全不透明时彻底不渐隐 —— 否则「不透明度 100%」的上下边缘仍会透出背景。
        // 默认不透明度 70 时强度正好 1.0，观感与之前一致。
        const float fadeStrength = (m_settings->bgAlpha >= 255)
            ? 0.0f
            : min(1.0f, (float)(255 - m_settings->bgAlpha) / 185.0f);
        const bool fadeOn = !m_interactive && fadeStrength > 0.001f;   // 调整模式不渐隐，保证缩放手柄清晰可见

        for (int y = 0; y < H; ++y)
        {
            float fade = 1.0f;
            if (fadeOn)
            {
                float ramp = 1.0f;
                if (y < fadeH) ramp = (float)y / (float)fadeH;
                else if (y > H - 1 - fadeH) ramp = (float)(H - 1 - y) / (float)fadeH;
                fade = 1.0f - (1.0f - ramp) * fadeStrength;
            }
            if (fade < 0.0f) fade = 0.0f;

            BYTE* row = base + y * stride;
            for (int x = 0; x < W; ++x)
            {
                BYTE* px = row + x * 4;
                BYTE a = px[3];
                if (a == 0) continue;
                if (fade < 0.999f)
                {
                    a = (BYTE)(a * fade);
                    px[3] = a;
                }
                px[0] = (BYTE)((px[0] * a + 127) / 255);
                px[1] = (BYTE)((px[1] * a + 127) / 255);
                px[2] = (BYTE)((px[2] * a + 127) / 255);
            }
        }

        // 拷贝到 DIB
        if (m_pBits)
        {
            BYTE* dst = (BYTE*)m_pBits;
            const int dstStride = W * 4;
            for (int y = 0; y < H; ++y)
            {
                const BYTE* srcRow = (stride > 0) ? (base + y * stride)
                                                  : (base + (H - 1 - y) * (-stride));
                memcpy(dst + y * dstStride, srcRow, (size_t)dstStride);
            }
        }
        m_pBmp->UnlockBits(&bd);
    }

    // ---- 呈现 ----
    if (m_pBits)
    {
        HDC screen = ::GetDC(nullptr);
        POINT ptSrc = { 0, 0 };
        POINT ptDst = { rc.left, rc.top };
        SIZE  size = { W, H };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::UpdateLayeredWindow(m_hWnd, screen, &ptDst, &size, m_memDC, &ptSrc, 0, &bf, ULW_ALPHA);
        ::ReleaseDC(nullptr, screen);
    }
}

void COverlayWnd::DrawContent(Graphics& g, int W, int H)
{
    const AppSettings& s = *m_settings;

    // 背景
    if (s.bgAlpha > 0)
    {
        SolidBrush bg(Color((BYTE)s.bgAlpha, GetRValue(s.bgColor), GetGValue(s.bgColor), GetBValue(s.bgColor)));
        g.FillRectangle(&bg, 0, 0, W, H);
    }

    const int padX = (int)(s.fontSize * 0.62f);
    // 右侧给滚动条留出空间，正文不会被压到滚动条下面
    const int barReserve = COverlayBar::BarWidthForDpi(m_dpi) + ::MulDiv(8, m_dpi, 96);
    const int availW = W - padX * 2 - barReserve;
    const int textW = (availW > 40) ? availW : 40;

    if (!m_doc || m_doc->Empty())
    {
        UpdateBarMetrics(0, 0, 0.0f);
        FontFamily fam(L"Microsoft YaHei UI");
        Font font(&fam, 26.0f, FontStyleRegular, UnitPixel);
        SolidBrush br(Color(170, 255, 255, 255));
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetAlignment(StringAlignmentCenter);
        RectF box((REAL)0, (REAL)(H / 2 - 40), (REAL)W, (REAL)80);
        g.DrawString(L"未加载稿件", -1, &font, box, &fmt, &br);
        return;
    }

    m_doc->Layout(&g, s.fontSize, s.lineSpacing, textW);
    const int lineH = m_doc->LineHeight();
    const int total = m_doc->TotalHeight();

    // 滚动范围：最后一行正好落到窗口底部（滚动条能拖到底）
    float maxScroll = (float)total - (float)H + (float)lineH;
    if (maxScroll < 0) maxScroll = 0;

    if (m_followAnchor)
    {
        const int line = m_doc->LineIndexOfChar(m_anchorChar);

        // 行内进度：锚点是「下一个要读的字」，用它在一行里的比例让目标位置
        // 连续推进。否则一句长句要等整行读完才动一次，看起来一顿一顿的。
        float frac = 0.0f;
        const auto& ls = m_doc->Lines();
        if (line >= 0 && line < (int)ls.size())
        {
            const std::wstring& txt = ls[line].text;
            const int len = (int)txt.size();
            int idx = m_anchorChar - ls[line].origStart;
            if (idx < 0) idx = 0;
            if (idx > len) idx = len;
            if (len > 0) frac = (float)idx / (float)len;
            if (frac > 0.92f) frac = 0.92f;   // 读到行尾也别把这一行顶出去太多
        }

        float t = (line + frac) * (float)lineH - H * 0.40f;
        if (t < 0) t = 0;
        if (t > maxScroll) t = maxScroll;
        m_targetScroll = t;

        // 智能跟随由 OnTimer 里的速度模型接管；旧模式才用固定比例的缓动
        if (!m_smartFollow && !m_animating)
            m_animating = (fabsf(m_targetScroll - m_scrollY) > 0.8f);
    }

    if (m_scrollY > maxScroll) { m_scrollY = maxScroll; m_targetScroll = maxScroll; }
    if (m_scrollY < 0) { m_scrollY = 0; }

    UpdateBarMetrics(H, total, maxScroll);

    const int curLine = m_doc->LineIndexOfChar(m_anchorChar);
    const auto& lines = m_doc->Lines();

    const wchar_t* famName = L"Microsoft YaHei UI";
    FontFamily fam(famName);
    Font font(&fam, (REAL)s.fontSize, FontStyleRegular, UnitPixel);

    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);

    SolidBrush textBrush(Color(255, GetRValue(s.textColor), GetGValue(s.textColor), GetBValue(s.textColor)));
    SolidBrush hiBrush(Color(255, GetRValue(s.hiColor), GetGValue(s.hiColor), GetBValue(s.hiColor)));
    SolidBrush shadowBrush(Color(170, 0, 0, 0));
    SolidBrush hiBg(Color(36, GetRValue(s.hiColor), GetGValue(s.hiColor), GetBValue(s.hiColor)));
    SolidBrush hiCell(Color(96, GetRValue(s.hiColor), GetGValue(s.hiColor), GetBValue(s.hiColor)));
    SolidBrush accent(Color(200, GetRValue(s.hiColor), GetGValue(s.hiColor), GetBValue(s.hiColor)));

    // -----------------------------------------------------------------
    //  逐字高亮：用 MeasureCharacterRanges 精确取「已读前缀」与「当前字」
    //  的像素范围（与 DrawString 同一排版管线，逐字边界完全对齐）。
    //  必须在镜像变换应用之前测量，得到的是世界坐标，绘制时再由 transform
    //  统一翻转，避免坐标被变换两次。
    // -----------------------------------------------------------------
    int  hlIdx = 0;
    REAL hlPrefixRight = 0.0f;   // 已读前缀右边界（世界 x）
    REAL hlCurLeft = 0.0f;       // 当前字左边界
    REAL hlCurRight = 0.0f;      // 当前字右边界
    REAL hlTop = 0.0f;           // 当前行文字的世界 y

    const bool hlOn = (s.highlightCur && m_anchorValid &&
                       curLine >= 0 && curLine < (int)lines.size());
    if (hlOn)
    {
        const std::wstring& ct = lines[curLine].text;
        hlIdx = m_anchorChar - lines[curLine].origStart;
        if (hlIdx < 0) hlIdx = 0;
        if (hlIdx > (int)ct.size()) hlIdx = (int)ct.size();

        hlTop = curLine * (REAL)lineH - m_scrollY + (lineH - s.fontSize) * 0.42f;

        CharacterRange crs[2];
        INT nCr = 0;
        if (hlIdx > 0)             crs[nCr++] = CharacterRange(0, hlIdx);
        if (hlIdx < (int)ct.size()) crs[nCr++] = CharacterRange(hlIdx, 1);

        if (nCr > 0)
        {
            StringFormat mf(StringFormat::GenericTypographic());
            mf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
            mf.SetMeasurableCharacterRanges(nCr, crs);

            Region rg[2] = { Region(RectF(0, 0, 0, 0)), Region(RectF(0, 0, 0, 0)) };
            RectF layout((REAL)padX, hlTop, (REAL)W, (REAL)lineH * 2.0f);
            if (g.MeasureCharacterRanges(ct.c_str(), (INT)ct.size(), &font,
                                         layout, &mf, nCr, rg) == Ok)
            {
                RectF b;
                if (hlIdx > 0)
                {
                    rg[0].GetBounds(&b, &g);
                    hlPrefixRight = b.GetRight();
                }
                if (hlIdx < (int)ct.size())
                {
                    rg[hlIdx > 0 ? 1 : 0].GetBounds(&b, &g);
                    hlCurLeft = b.GetLeft();
                    hlCurRight = b.GetRight();
                }
            }
        }
    }

    GraphicsState state = g.Save();
    if (s.mirror)
    {
        g.TranslateTransform((REAL)W, 0);
        g.ScaleTransform(-1.0f, 1.0f);
    }

    int firstLine = (int)(m_scrollY / lineH) - 1;
    if (firstLine < 0) firstLine = 0;
    int lastLine = firstLine + H / lineH + 2;
    if (lastLine > (int)lines.size() - 1) lastLine = (int)lines.size() - 1;

    for (int i = firstLine; i <= lastLine; ++i)
    {
        const float lineTop = i * (float)lineH - m_scrollY;
        const float textTop = lineTop + (lineH - s.fontSize) * 0.42f;

        const std::wstring& t = lines[i].text;
        if (t.empty()) continue;

        const bool isCur = hlOn && (i == curLine);

        if (isCur)
        {
            // 已读部分淡底（到当前字左缘）+ 当前字实底 + 左侧竖条
            if (hlPrefixRight > (REAL)padX + 0.5f)
                g.FillRectangle(&hiBg, 0.0f, lineTop + 2.0f, hlPrefixRight, (REAL)lineH - 4.0f);

            if (hlCurRight > hlCurLeft)
                g.FillRectangle(&hiCell, hlCurLeft, lineTop + 2.0f,
                                hlCurRight - hlCurLeft, (REAL)lineH - 4.0f);

            g.FillRectangle(&accent, 0.0f, lineTop + 2.0f, 4.0f, (REAL)lineH - 4.0f);
        }

        if (s.shadow)
        {
            g.DrawString(t.c_str(), (INT)t.size(), &font, PointF((REAL)padX + 2.0f, textTop + 2.0f), &fmt, &shadowBrush);
            g.DrawString(t.c_str(), (INT)t.size(), &font, PointF((REAL)padX - 1.0f, textTop + 1.0f), &fmt, &shadowBrush);
        }

        // 未读部分用常规色整行画一遍
        g.DrawString(t.c_str(), (INT)t.size(), &font, PointF((REAL)padX, textTop), &fmt, &textBrush);

        // 已读部分（含当前字）裁剪后原样重绘一次，改高亮色；
        // 同一个字符串、同一个起点，保证逐字边界与字形完全对齐
        if (isCur)
        {
            const REAL clipW = (hlCurRight > 0.5f) ? (hlCurRight - 0.5f) : hlPrefixRight;
            if (clipW > (REAL)padX)
            {
                g.SetClip(RectF(0.0f, lineTop - 2.0f, clipW, (REAL)lineH + 4.0f));
                g.DrawString(t.c_str(), (INT)t.size(), &font, PointF((REAL)padX, textTop), &fmt, &hiBrush);
                g.ResetClip();
            }
        }
    }

    // 阅读进度条
    if (s.bgAlpha > 20)
    {
        float prog = (float)m_anchorChar / (float)(m_doc->CharCount() > 0 ? m_doc->CharCount() : 1);
        if (prog < 0) prog = 0;
        if (prog > 1) prog = 1;
        SolidBrush bar(Color(60, 255, 255, 255));
        SolidBrush barFg(Color(190, GetRValue(s.hiColor), GetGValue(s.hiColor), GetBValue(s.hiColor)));
        g.FillRectangle(&bar, 0.0f, (REAL)(H - 3), (REAL)W, 3.0f);
        g.FillRectangle(&barFg, 0.0f, (REAL)(H - 3), (REAL)(W * prog), 3.0f);
    }

    g.Restore(state);

    // 调整模式：虚线边框 + 八向缩放手柄
    if (m_interactive)
    {
        Pen pen(Color(220, 90, 200, 255), 2.0f);
        pen.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&pen, 1.0f, 1.0f, (REAL)(W - 2), (REAL)(H - 2));

        SolidBrush hb(Color(245, 90, 200, 255));
        const REAL t = 4.0f;      // 手柄线宽
        const REAL L = 22.0f;     // 手柄长度
        const REAL Wf = (REAL)W, Hf = (REAL)H;

        // 四角 L 形手柄
        g.FillRectangle(&hb, 0.0f, 0.0f, L, t);
        g.FillRectangle(&hb, 0.0f, 0.0f, t, L);
        g.FillRectangle(&hb, Wf - L, 0.0f, L, t);
        g.FillRectangle(&hb, Wf - t, 0.0f, t, L);
        g.FillRectangle(&hb, 0.0f, Hf - t, L, t);
        g.FillRectangle(&hb, 0.0f, Hf - L, t, L);
        g.FillRectangle(&hb, Wf - L, Hf - t, L, t);
        g.FillRectangle(&hb, Wf - t, Hf - L, t, L);

        // 四边中点手柄
        g.FillRectangle(&hb, (Wf - L) * 0.5f, 0.0f, L, t);
        g.FillRectangle(&hb, (Wf - L) * 0.5f, Hf - t, L, t);
        g.FillRectangle(&hb, 0.0f, (Hf - L) * 0.5f, t, L);
        g.FillRectangle(&hb, Wf - t, (Hf - L) * 0.5f, t, L);

        // 尺寸提示（右上角，避开正文）
        FontFamily fam(L"Microsoft YaHei UI");
        Font hint(&fam, 13.0f, FontStyleRegular, UnitPixel);
        SolidBrush hbText(Color(210, 90, 200, 255));
        StringFormat sf(StringFormat::GenericTypographic());
        sf.SetAlignment(StringAlignmentFar);
        wchar_t buf[64];
        swprintf_s(buf, L"拖动边缘缩放 · %d × %d", W, H);
        RectF box(0.0f, 6.0f, Wf - 10.0f, 22.0f);
        g.DrawString(buf, -1, &hint, box, &sf, &hbText);
    }
}
