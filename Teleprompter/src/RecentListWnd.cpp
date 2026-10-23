// RecentListWnd.cpp - 稿件页「最近文稿」自绘列表
#include <cstdio>
#include <cstdarg>
#include "RecentListWnd.h"

namespace
{
    const COLORREF kPanelBg    = RGB(247, 248, 250);   // 与主面板背景一致（圆角外用）
    const COLORREF kCardBg     = RGB(255, 255, 255);
    const COLORREF kBorder     = RGB(224, 228, 234);
    const COLORREF kRowHover   = RGB(246, 248, 251);
    const COLORREF kRowSel     = RGB(242, 246, 253);
    const COLORREF kRowCurrent = RGB(239, 246, 255);
    const COLORREF kAccent     = RGB(58, 132, 246);
    const COLORREF kTextName   = RGB(32, 36, 42);
    const COLORREF kTextCur    = RGB(28, 94, 200);
    const COLORREF kTextDim    = RGB(150, 155, 163);
    const COLORREF kTextPath   = RGB(140, 146, 156);
    const COLORREF kTextPrev   = RGB(104, 110, 120);
    const COLORREF kSeparator  = RGB(241, 243, 246);
    const COLORREF kDelHover   = RGB(228, 76, 76);
    const COLORREF kTagBg      = RGB(238, 241, 246);
    const COLORREF kTagText    = RGB(108, 116, 128);
    const COLORREF kMissBg     = RGB(253, 238, 238);
    const COLORREF kMissText   = RGB(198, 88, 88);
}

// 自动化排查钩子：设置 TP_LIST_LOG=<文件> 后，把每次点击的命中信息记下来
// （点到了哪一行、哪个按钮区、当时的行高与按钮矩形），方便对自绘列表做 e2e 排障。
static void ListDebugLog(const wchar_t* fmt, ...)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        wchar_t env[MAX_PATH] = { 0 };
        enabled = (::GetEnvironmentVariableW(L"TP_LIST_LOG", env, MAX_PATH) > 0 && env[0]) ? 1 : 0;
    }
    if (enabled != 1) return;

    static std::wstring path;
    if (path.empty())
    {
        wchar_t env[MAX_PATH] = { 0 };
        ::GetEnvironmentVariableW(L"TP_LIST_LOG", env, MAX_PATH);
        path = env;
    }

    wchar_t line[512] = { 0 };
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);

    FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"%s\n", line);
    fclose(f);
}

BEGIN_MESSAGE_MAP(CRecentListWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
    ON_WM_KEYDOWN()
    ON_WM_GETDLGCODE()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

BOOL CRecentListWnd::Create(CWnd* parent, UINT id, const CRect& rc)
{
    const CString cls = ::AfxRegisterWndClass(CS_DBLCLKS,
                                              ::LoadCursorW(nullptr, IDC_ARROW),
                                              nullptr, nullptr);
    return CWnd::CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          rc, parent, id);
}

void CRecentListWnd::SetItems(std::vector<Item> items)
{
    m_items = std::move(items);
    if (m_sel >= (int)m_items.size()) m_sel = -1;
    if (m_hover >= (int)m_items.size()) m_hover = -1;
    ClampScroll();
    if (GetSafeHwnd()) Invalidate(FALSE);
}

void CRecentListWnd::SetFonts(CFont* normal, CFont* bold, CFont* smallFont)
{
    m_f = normal;
    m_fb = bold;
    m_fs = smallFont;
    if (GetSafeHwnd()) Invalidate(FALSE);
}

void CRecentListWnd::SetScale(double scale)
{
    if (scale > 0.1) m_scale = scale;
    ClampScroll();
    if (GetSafeHwnd()) Invalidate(FALSE);
}

const CRecentListWnd::Item* CRecentListWnd::At(int idx) const
{
    if (idx < 0 || idx >= (int)m_items.size()) return nullptr;
    return &m_items[idx];
}

int CRecentListWnd::RowHeight()
{
    CRect rc;
    GetClientRect(&rc);
    const int n = m_items.empty() ? 1 : (int)m_items.size();
    const int pref = Px(100);
    const int avail = rc.Height() - 2;
    if (n * pref <= avail) return pref;
    // 条目多、面板被拉矮时先压缩行高（仍保证三行文本 + 按钮行大致放得下），
    // 全部塞得下就不出滚动条
    const int compressed = avail / n;
    if (compressed >= Px(72)) return compressed;
    // 再放不下：固定最小行高，超出部分靠右侧滚动条滚动
    return Px(72);
}

CRect CRecentListWnd::RowRect(int idx)
{
    CRect rc;
    GetClientRect(&rc);
    const int h = RowHeight();
    const int top = rc.top + 1 + idx * h - m_scrollTop;
    return CRect(rc.left + 1, top, rc.right - 1,
                 min(top + h, (int)rc.bottom - 1));
}

CRect CRecentListWnd::DeleteRect(int idx)
{
    const CRect row = RowRect(idx);
    const int side = Px(22);
    const int cy = (row.top + row.bottom) / 2;
    return CRect(row.right - Px(10) - side, cy - side / 2, row.right - Px(10), cy + side / 2);
}

// 行内小按钮：kZoneFolder=「打开文件夹」，kZoneGoto=「去提词」，单独占路径下方一行（靠左）
// 注意：行高被压缩时 OnPaint 会隐藏预览行并收紧布局，按钮区必须按同一规则计算，
// 这里直接读取 OnPaint 使用的布局参数，保证命中区与绘制一致。
CRect CRecentListWnd::ActionRect(int idx, int zone)
{
    const CRect row = RowRect(idx);
    const int h = Px(18);
    const int top = row.bottom - Px(5) - h;
    const int wGoto = Px(52), wFolder = Px(76), gap = Px(8);
    const int left = row.left + Px(12);
    if (zone == kZoneFolder)
        return CRect(left, top, left + wFolder, top + h);
    return CRect(left + wFolder + gap, top, left + wFolder + gap + wGoto, top + h);
}

int CRecentListWnd::HitTest(CPoint pt, int& zone)
{
    zone = kZoneNone;
    CRect rc;
    GetClientRect(&rc);
    if (!rc.PtInRect(pt)) return -1;
    const int idx = (pt.y - rc.top - 1 + m_scrollTop) / RowHeight();
    if (idx < 0 || idx >= (int)m_items.size()) return -1;
    if (DeleteRect(idx).PtInRect(pt))        zone = kZoneDelete;
    else if (ActionRect(idx, kZoneFolder).PtInRect(pt)) zone = kZoneFolder;
    else if (ActionRect(idx, kZoneGoto).PtInRect(pt))   zone = kZoneGoto;

    CRect row = RowRect(idx);
    CRect fr = ActionRect(idx, kZoneFolder), gr = ActionRect(idx, kZoneGoto);
    CRect delr = DeleteRect(idx);
    ListDebugLog(L"hit pt=(%d,%d) idx=%d zone=%d rh=%d scrollTop=%d row=%d,%d,%d,%d del=%d,%d,%d,%d folder=%d,%d,%d,%d goto=%d,%d,%d,%d",
                 pt.x, pt.y, idx, (int)zone, RowHeight(), m_scrollTop,
                 row.left, row.top, row.right, row.bottom,
                 delr.left, delr.top, delr.right, delr.bottom,
                 fr.left, fr.top, fr.right, fr.bottom,
                 gr.left, gr.top, gr.right, gr.bottom);
    return idx;
}

// ---------------------------------------------------------------------------
// 滚动条：内容超过可视高度时启用（滚轮 / 拖拽滑块 / PageUp·PageDown / 方向键）
// ---------------------------------------------------------------------------
int CRecentListWnd::MaxScroll()
{
    if (m_items.empty()) return 0;
    CRect rc;
    GetClientRect(&rc);
    const int content = 2 + (int)m_items.size() * RowHeight();
    return max(0, content - rc.Height());
}

void CRecentListWnd::ClampScroll()
{
    const int m = MaxScroll();
    if (m_scrollTop > m) m_scrollTop = m;
    if (m_scrollTop < 0) m_scrollTop = 0;
}

void CRecentListWnd::SetScrollTop(int v)
{
    const int old = m_scrollTop;
    m_scrollTop = v;
    ClampScroll();
    if (m_scrollTop != old && GetSafeHwnd()) Invalidate(FALSE);
}

void CRecentListWnd::EnsureVisibleRow(int idx)
{
    if (idx < 0 || idx >= (int)m_items.size()) return;
    const int h = RowHeight();
    const int top = 1 + idx * h;
    const int bottom = top + h;
    CRect rc;
    GetClientRect(&rc);
    if (top - 1 < m_scrollTop)
        SetScrollTop(top - 1);
    else if (bottom + 1 > m_scrollTop + rc.Height())
        SetScrollTop(bottom + 1 - rc.Height());
}

CRect CRecentListWnd::ScrollThumbRect()
{
    const int maxScroll = MaxScroll();
    if (maxScroll <= 0) return CRect(0, 0, 0, 0);
    CRect rc;
    GetClientRect(&rc);
    const int barW = Px(6);
    const int trackTop = rc.top + Px(8);
    const int trackH = rc.Height() - Px(16);
    const int content = 2 + (int)m_items.size() * RowHeight();
    const int thumbH = max(Px(24), (int)(trackH * (double)rc.Height() / content));
    const int y = trackTop + (int)((trackH - thumbH) * (double)m_scrollTop / maxScroll);
    return CRect(rc.right - Px(4) - barW, y, rc.right - Px(4), y + thumbH);
}

void CRecentListWnd::OpenAt(int idx)
{
    if (idx < 0 || idx >= (int)m_items.size()) return;
    if (m_onOpen) m_onOpen(idx);
}

void CRecentListWnd::DeleteAt(int idx)
{
    if (idx < 0 || idx >= (int)m_items.size()) return;
    if (m_onDelete) m_onDelete(idx);
}

BOOL CRecentListWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
    // 不再单独擦背景：OnPaint 用内存双缓冲一次性画完整画面（含四角面板底色），
    // 中间不需要露出任何底色，重绘全程不闪
    return TRUE;
}

void CRecentListWnd::OnPaint()
{
    CPaintDC wndDC(this);
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    // 双缓冲：全部内容先画进内存位图，最后一次贴到屏幕。
    // 滚动 / 悬停 / 选中变化都是全窗口重绘，直接画屏幕会先擦背景再画内容而闪烁。
    CDC mem;
    mem.CreateCompatibleDC(&wndDC);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&wndDC, rc.Width(), rc.Height());
    mem.SelectObject(&bmp);   // 位图必须一直选在内存 DC 里，直到贴屏完成

    CDC& dc = mem;
    dc.SetBkMode(TRANSPARENT);

    // 圆角外的四角先铺面板底色
    dc.FillSolidRect(rc, kPanelBg);

    // 白色圆角卡片（内容裁剪在圆角内，行底色才不会盖住圆角）
    CRgn rgn;
    rgn.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, Px(10), Px(10));
    dc.SelectClipRgn(&rgn);
    dc.FillSolidRect(rc, kCardBg);

    if (m_items.empty())
    {
        dc.SelectObject(m_f ? m_f : dc.GetCurrentFont());
        dc.SetTextColor(kTextDim);
        CRect box = rc;
        box.DeflateRect(Px(24), 0);
        dc.DrawText(L"还没有文稿\r\n点上方「添加稿件」，或把 txt / docx / pptx 拖到本窗口",
                    -1, &box, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    }
    else
    {
        // 只画可视范围内的行（滚动时上下两端的行由圆角裁剪掉）
        const int h = RowHeight();
        const int n = (int)m_items.size();
        int first = 0, rows = n;
        if (MaxScroll() > 0)
        {
            first = max(0, (m_scrollTop - 1) / h);
            rows = min(n - first, rc.Height() / h + 2);
        }

        for (int i = first; i < first + rows; ++i)
        {
            const Item& it = m_items[i];
            CRect row = RowRect(i);
            const bool hover = (m_hover == i);
            const bool sel = (m_sel == i);

            if (it.current)      dc.FillSolidRect(row, kRowCurrent);
            else if (sel)        dc.FillSolidRect(row, kRowSel);
            else if (hover)      dc.FillSolidRect(row, kRowHover);

            if (it.current)
            {
                CRect bar(row.left, row.top + Px(9), row.left + Px(3), row.bottom - Px(9));
                dc.FillSolidRect(bar, kAccent);
            }

            // 右侧删除按钮
            const CRect del = DeleteRect(i);
            const bool delHover = hover && m_hoverZone == kZoneDelete;
            if (delHover)
            {
                const int d = Px(22);
                CBrush brDel(kDelHover);
                CBrush* oldBr = dc.SelectObject(&brDel);
                CPen* oldPen = (CPen*)dc.SelectStockObject(NULL_PEN);
                dc.RoundRect(del, CPoint(d, d));
                dc.SelectObject(oldPen);
                dc.SelectObject(oldBr);

                dc.SelectObject(m_f ? m_f : dc.GetCurrentFont());
                dc.SetTextColor(RGB(255, 255, 255));
                CRect t = del;
                dc.DrawText(L"×", -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                dc.SelectObject(m_f ? m_f : dc.GetCurrentFont());
                dc.SetTextColor(hover ? RGB(110, 118, 130) : kTextDim);
                CRect t = del;
                dc.DrawText(L"×", -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // 行内自适应布局：完整行高（≥Px(84)）时四段全画；被压缩时隐藏预览行并收紧路径行，
            // 保证按钮行（贴底）永远不会与路径行重叠
            const bool roomy = row.Height() >= Px(84);

            // 按钮行（路径下方单独一行，靠左）：[打开文件夹] [去提词]
            struct ActBtn { int zone; const wchar_t* text; int w; COLORREF bg; COLORREF fg; COLORREF bgHover; };
            const ActBtn btns[2] = {
                { kZoneFolder, L"打开文件夹", 76, kTagBg,    kTagText,  RGB(226, 231, 238) },
                { kZoneGoto,   L"去提词",     52, RGB(232, 242, 255), kTextCur, RGB(214, 233, 252) },
            };
            for (const ActBtn& ab : btns)
            {
                const CRect ar = ActionRect(i, ab.zone);
                const bool aHover = hover && m_hoverZone == ab.zone;
                CBrush br(aHover ? ab.bgHover : ab.bg);
                CPen penB(PS_SOLID, 1, aHover ? RGB(196, 204, 214) : kBorder);
                CBrush* oldBr = dc.SelectObject(&br);
                CPen* oldPen = dc.SelectObject(&penB);
                dc.RoundRect(ar, CPoint(Px(8), Px(8)));
                dc.SelectObject(oldPen);
                dc.SelectObject(oldBr);

                dc.SelectObject(m_fs ? m_fs : dc.GetCurrentFont());
                dc.SetTextColor(ab.fg);
                CRect t = ar;
                dc.DrawText(ab.text, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // 名称右侧的状态标签：缺失优先于 INI
            int textRight = del.left - Px(8);
            std::wstring tag;
            COLORREF tagBg = kTagBg, tagFg = kTagText;
            if (it.missing)      { tag = L"缺失"; tagBg = kMissBg; tagFg = kMissText; }
            else if (it.preset)  { tag = L"INI"; }

            if (!tag.empty())
            {
                const int tagW = Px(it.missing ? 34 : 30);
                CRect tagRc(textRight - tagW, row.top + Px(7),
                            textRight, row.top + Px(7) + Px(16));
                CBrush brTag(tagBg);
                CBrush* oldBr = dc.SelectObject(&brTag);
                CPen* oldPen = (CPen*)dc.SelectStockObject(NULL_PEN);
                dc.RoundRect(tagRc, CPoint(Px(8), Px(8)));
                dc.SelectObject(oldPen);
                dc.SelectObject(oldBr);

                dc.SelectObject(m_fs ? m_fs : dc.GetCurrentFont());
                dc.SetTextColor(tagFg);
                CRect t = tagRc;
                dc.DrawText(tag.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                textRight = tagRc.left - Px(6);
            }

            // 第一行：文件名
            CRect nameRc(row.left + Px(12), row.top + Px(5), textRight, row.top + Px(25));
            dc.SelectObject(m_fb ? m_fb : dc.GetCurrentFont());
            dc.SetTextColor(it.missing ? kTextDim : (it.current ? kTextCur : kTextName));
            dc.DrawText(it.name.c_str(), -1, &nameRc,
                        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            // 第二行：内容预览（文件开头 160 字符；空则此行留白；行高压缩时隐藏）
            int pathTop, pathBottom;
            if (roomy)
            {
                pathTop    = 43;
                pathBottom = 59;
                if (!it.preview.empty())
                {
                    CRect prevRc(row.left + Px(12), row.top + Px(24), textRight, row.top + Px(44));
                    dc.SelectObject(m_fs ? m_fs : dc.GetCurrentFont());
                    dc.SetTextColor(it.missing ? kTextDim : kTextPrev);
                    dc.DrawText(it.preview.c_str(), -1, &prevRc,
                                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                }
            }
            else
            {
                pathTop    = 27;
                pathBottom = 43;
            }

            // 第三行：完整路径（中间省略，保证盘符和文件名都在）
            CRect pathRc(row.left + Px(12), row.top + Px(pathTop), textRight, row.top + Px(pathBottom));
            dc.SelectObject(m_fs ? m_fs : dc.GetCurrentFont());
            dc.SetTextColor(kTextPath);
            dc.DrawText(it.path.c_str(), -1, &pathRc,
                        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);

            if (i + 1 < first + rows)
            {
                CRect sep(row.left + Px(12), row.bottom - 1, row.right - Px(10), row.bottom);
                dc.FillSolidRect(sep, kSeparator);
            }
        }

        // 自绘滚动条（浅灰轨道 + 圆角滑块，拖拽时加深）
        if (MaxScroll() > 0)
        {
            const CRect thumb = ScrollThumbRect();
            if (!thumb.IsRectEmpty())
            {
                CBrush brTrack(kSeparator);
                CBrush* oldBr = dc.SelectObject(&brTrack);
                CPen* oldPen = (CPen*)dc.SelectStockObject(NULL_PEN);
                dc.RoundRect(CRect(thumb.left, rc.top + Px(8), thumb.right, rc.bottom - Px(8)),
                             CPoint(Px(6), Px(6)));
                CBrush brThumb((m_dragScroll || m_hoverScroll) ? RGB(170, 177, 188)
                                                               : RGB(198, 204, 214));
                dc.SelectObject(&brThumb);
                dc.RoundRect(thumb, CPoint(Px(6), Px(6)));
                dc.SelectObject(oldPen);
                dc.SelectObject(oldBr);
            }
        }
    }

    dc.SelectClipRgn(nullptr);

    // 圆角边框（键盘焦点时描蓝色）
    CPen pen(PS_SOLID, 1, m_focused ? RGB(140, 178, 240) : kBorder);
    CPen* oldPen = dc.SelectObject(&pen);
    CBrush* oldBr = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
    dc.RoundRect(rc.left, rc.top, rc.right - 1, rc.bottom - 1, Px(10), Px(10));
    dc.SelectObject(oldBr);
    dc.SelectObject(oldPen);

    // 一次性贴屏（此时位图仍在内存 DC 里），随后 DC 析构自动清理
    wndDC.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
}

void CRecentListWnd::OnMouseMove(UINT nFlags, CPoint pt)
{
    if (m_dragScroll)
    {
        CRect rc;
        GetClientRect(&rc);
        const CRect thumb = ScrollThumbRect();
        const int scrollTrack = rc.Height() - Px(16) - thumb.Height();
        const int maxScroll = MaxScroll();
        if (maxScroll > 0 && scrollTrack > 0)
            SetScrollTop((pt.y - rc.top - Px(8) - m_scrollGrab) * maxScroll / scrollTrack);
        return;
    }

    m_hoverScroll = ScrollThumbRect().PtInRect(pt) != FALSE;

    int zone = kZoneNone;
    const int idx = HitTest(pt, zone);
    if (idx != m_hover || zone != m_hoverZone)
    {
        m_hover = idx;
        m_hoverZone = zone;
        Invalidate(FALSE);
    }
    ::SetCursor(::LoadCursorW(nullptr, (idx >= 0 || m_hoverScroll) ? IDC_HAND : IDC_ARROW));

    TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
    ::TrackMouseEvent(&tme);

    CWnd::OnMouseMove(nFlags, pt);
}

void CRecentListWnd::OnMouseLeave()
{
    if (m_hover != -1 || m_hoverZone != kZoneNone || m_hoverScroll)
    {
        m_hover = -1;
        m_hoverZone = kZoneNone;
        m_hoverScroll = false;
        Invalidate(FALSE);
    }
    ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
}

void CRecentListWnd::OnLButtonDown(UINT nFlags, CPoint pt)
{
    SetFocus();

    // 滚动条滑块：开始拖拽
    if (MaxScroll() > 0)
    {
        const CRect thumb = ScrollThumbRect();
        if (!thumb.IsRectEmpty() && thumb.PtInRect(pt))
        {
            m_dragScroll = true;
            m_scrollGrab = pt.y - thumb.top;
            SetCapture();
            return;
        }
    }

    int zone = kZoneNone;
    const int idx = HitTest(pt, zone);
    if (idx >= 0)
    {
        m_sel = idx;
        Invalidate(FALSE);
        switch (zone)
        {
        case kZoneDelete: DeleteAt(idx); break;
        case kZoneFolder:
            if (m_onFolder) m_onFolder(idx);
            break;
        case kZoneGoto:
            if (m_onGoto) m_onGoto(idx);
            break;
        default: break;
        }
    }
    CWnd::OnLButtonDown(nFlags, CPoint(pt));
}

void CRecentListWnd::OnLButtonUp(UINT nFlags, CPoint pt)
{
    if (m_dragScroll)
    {
        m_dragScroll = false;
        if (GetCapture() == this) ReleaseCapture();
        Invalidate(FALSE);
    }
    CWnd::OnLButtonUp(nFlags, CPoint(pt));
}

BOOL CRecentListWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (MaxScroll() > 0)
        SetScrollTop(m_scrollTop - (zDelta / 120) * 3 * RowHeight());
    return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CRecentListWnd::OnSize(UINT nType, int cx, int cy)
{
    ClampScroll();
    Invalidate(FALSE);
    CWnd::OnSize(nType, cx, cy);
}

void CRecentListWnd::OnLButtonDblClk(UINT nFlags, CPoint pt)
{
    int zone = kZoneNone;
    const int idx = HitTest(pt, zone);
    if (idx >= 0 && zone == kZoneNone) OpenAt(idx);   // 按钮上的双击不触发载入
    CWnd::OnLButtonDblClk(nFlags, CPoint(pt));
}

void CRecentListWnd::OnKeyDown(UINT ch, UINT rep, UINT flags)
{
    if (m_items.empty())
    {
        CWnd::OnKeyDown(ch, rep, flags);
        return;
    }

    const int last = (int)m_items.size() - 1;
    switch (ch)
    {
    case VK_UP:
        m_sel = (m_sel <= 0) ? 0 : m_sel - 1;
        EnsureVisibleRow(m_sel);
        Invalidate(FALSE);
        break;
    case VK_DOWN:
        m_sel = (m_sel < 0) ? 0 : min(m_sel + 1, last);
        EnsureVisibleRow(m_sel);
        Invalidate(FALSE);
        break;
    case VK_HOME:
        m_sel = 0;
        EnsureVisibleRow(m_sel);
        Invalidate(FALSE);
        break;
    case VK_END:
        m_sel = last;
        EnsureVisibleRow(m_sel);
        Invalidate(FALSE);
        break;
    case VK_PRIOR:          // PageUp：向上翻一页
    case VK_NEXT:           // PageDown：向下翻一页
    {
        CRect rc;
        GetClientRect(&rc);
        const int step = max(1, rc.Height() / RowHeight() - 1);
        const int base = (m_sel < 0) ? 0 : m_sel;
        m_sel = (ch == VK_PRIOR) ? max(0, base - step) : min(last, base + step);
        EnsureVisibleRow(m_sel);
        Invalidate(FALSE);
        break;
    }
    case VK_RETURN:
        if (m_sel >= 0) OpenAt(m_sel);
        break;
    case VK_DELETE:
        if (m_sel >= 0) DeleteAt(m_sel);
        break;
    default:
        CWnd::OnKeyDown(ch, rep, flags);
        break;
    }
}

UINT CRecentListWnd::OnGetDlgCode()
{
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CRecentListWnd::OnSetFocus(CWnd* pOld)
{
    m_focused = true;
    Invalidate(FALSE);
    CWnd::OnSetFocus(pOld);
}

void CRecentListWnd::OnKillFocus(CWnd* pNew)
{
    m_focused = false;
    Invalidate(FALSE);
    CWnd::OnKillFocus(pNew);
}
