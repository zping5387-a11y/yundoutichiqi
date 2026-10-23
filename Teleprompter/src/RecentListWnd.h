// RecentListWnd.h - 稿件页「最近文稿」自绘列表（CWnd 派生）
//
// 每行四行内容：第一行文件名（当前稿件带蓝条与高亮），第二行内容预览（前 160 字符），
// 第三行完整路径，第四行两个小按钮 [打开文件夹][去提词]；右侧一个「×」删除按钮。
// 支持鼠标悬停 / 单击选中 / 双击载入 / Delete 键删除 / 上下键切换。
// 行数放不下时行高先压缩；仍放不下则固定最小行高并在右侧显示自绘滚动条
// （滚轮 / 拖拽滑块 / PageUp·PageDown 均可滚动）。
// 控件只负责画和收鼠标，数据与删除策略由 CControlDlg 决定。
#pragma once

#include "framework.h"
#include <functional>

class CRecentListWnd : public CWnd
{
public:
    struct Item
    {
        std::wstring path;      // 完整路径（第三行显示，DT_PATH_ELLIPSIS 截断）
        std::wstring name;      // 文件名（第一行显示）
        std::wstring preview;   // 内容预览（第二行显示，前 160 字符，可为空）
        std::wstring dir;       // 所在目录（缺省时从 path 拆）
        bool current = false;   // 正是当前加载的稿件
        bool missing = false;   // 文件已不存在
        bool preset  = false;   // 来自 INI [file] 的预设（程序不修改 INI）
    };

    BOOL Create(CWnd* parent, UINT id, const CRect& rc);
    void SetItems(std::vector<Item> items);
    void SetFonts(CFont* normal, CFont* bold, CFont* smallFont);
    void SetScale(double scale);
    void SetOnOpen(std::function<void(int)> fn)       { m_onOpen = std::move(fn); }
    void SetOnDelete(std::function<void(int)> fn)     { m_onDelete = std::move(fn); }
    void SetOnOpenFolder(std::function<void(int)> fn) { m_onFolder = std::move(fn); }
    void SetOnGoto(std::function<void(int)> fn)       { m_onGoto = std::move(fn); }

    int Count() const { return (int)m_items.size(); }
    const Item* At(int idx) const;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnMouseMove(UINT nFlags, CPoint pt);
    afx_msg void OnMouseLeave();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint pt);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint pt);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint pt);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnKeyDown(UINT ch, UINT rep, UINT flags);
    afx_msg UINT OnGetDlgCode();
    afx_msg void OnSetFocus(CWnd* pOld);
    afx_msg void OnKillFocus(CWnd* pNew);
    DECLARE_MESSAGE_MAP()

private:
    // 行内命中区域：0=无 1=×删除 2=打开文件夹 3=去提词
    enum { kZoneNone = 0, kZoneDelete = 1, kZoneFolder = 2, kZoneGoto = 3 };

    int   Px(double v) const { return (int)(v * m_scale + 0.5); }
    int   RowHeight();          // 行高优先 100，放不下时压缩；再放不下固定 72 并滚动
    CRect RowRect(int idx);
    CRect DeleteRect(int idx);
    CRect ActionRect(int idx, int zone);   // kZoneFolder / kZoneGoto 两个小按钮
    int   HitTest(CPoint pt, int& zone);
    void  OpenAt(int idx);
    void  DeleteAt(int idx);

    // 滚动条：内容超过可视高度时启用
    int   MaxScroll();
    void  ClampScroll();
    void  SetScrollTop(int v);
    void  EnsureVisibleRow(int idx);
    CRect ScrollThumbRect();

    std::vector<Item> m_items;
    int    m_sel = -1;
    int    m_hover = -1;
    int    m_hoverZone = kZoneNone;
    bool   m_focused = false;
    double m_scale = 1.0;
    CFont* m_f = nullptr;
    CFont* m_fb = nullptr;
    CFont* m_fs = nullptr;

    int  m_scrollTop = 0;        // 滚动偏移（像素）
    bool m_dragScroll = false;   // 正在拖拽滚动条滑块
    int  m_scrollGrab = 0;       // 按下点相对滑块顶部的偏移
    bool m_hoverScroll = false;  // 鼠标悬停在滑块上
    std::function<void(int)> m_onOpen;
    std::function<void(int)> m_onDelete;
    std::function<void(int)> m_onFolder;
    std::function<void(int)> m_onGoto;
};
