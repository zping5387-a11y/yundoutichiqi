// AppSettings.h - 配置项与 INI 持久化
#pragma once

#include "framework.h"

struct AppSettings
{
    // 文字
    int      fontSize      = 46;          // 像素
    int      lineSpacing   = 165;         // 行距百分比
    COLORREF textColor     = RGB(255, 255, 255);
    COLORREF hiColor       = RGB(255, 214, 102);
    bool     highlightCur  = true;
    bool     shadow        = true;
    bool     mirror        = false;

    // 面板
    int      bgAlpha       = 70;          // 0..255
    COLORREF bgColor       = RGB(8, 10, 14);   // 浮层背景色
    int      autoSpeed     = 3;           // 1..10
    bool     autoScroll    = false;
    bool     visible       = true;
    bool     locked        = true;        // true=点击穿透（锁定）
    bool     clickThrough  = true;        // 锁定时鼠标穿透；关掉后浮层可接收鼠标（滚轮直接滚）
    bool     trayHintShown = false;       // 首次隐藏到托盘时提示过一次（之后不再打扰）

    // 语音
    int      langId        = 0x0804;      // 0 = 系统默认；0x0804 中文；0x0409 英文
    std::wstring inputDeviceId;           // 麦克风设备 ID（空 = 自动）
    int      asrEngine     = 0;           // 0 = 离线识别（准，推荐）；1 = 系统自带识别
    bool     smartFollow   = true;        // 智能跟随：读才滚、停读暂停（关掉=按行跳动）

    // 浮层几何（屏幕坐标，空表示使用默认布局）
    RECT     overlayRect   = { 0, 0, 0, 0 };

    // 关于页：作者邮箱（INI [about] email 可改，改完重启生效）
    std::wstring authorEmail = L"yundou@agent.qq.com";

    // 主面板左上角（屏幕坐标）与窗口尺寸：重启时恢复上次位置和大小
    // panelPosValid / panelSizeValid = false 表示没记录过（用默认布局）
    POINT    panelPos       = { 0, 0 };
    bool     panelPosValid  = false;
    SIZE     panelSize      = { 0, 0 };
    bool     panelSizeValid = false;

    // 上次打开的文稿（运行期状态，不写回主 INI）
    std::wstring lastScriptPath;

    // 最近打开过的文稿（最新的在最前，最多 kMaxRecent 条）
    // 浮层顶部「切换稿件」按钮弹出的列表就来自这里：
    //   = 程序自己打开过的（sidecar 记录，最近在前） + INI [file] 里手写的预设清单
    static const size_t kMaxRecent = 8;
    std::vector<std::wstring> recentScripts;

    void Load();
    void Save() const;

    // 只写「最近文稿」sidecar（%APPDATA%\TeacherPrompter\recent.ini，可用 TP_DATA_DIR 覆盖）。
    // 主 INI 的 [file] 段是用户手写的稿件定义，程序只读、任何情况下都不写回。
    void SaveRecentStore() const;

    // 把 path 提到最近列表最前面（去重、截断到 kMaxRecent）。只改内存，落盘由 SaveRecentStore() 负责
    void AddRecentScript(const std::wstring& path);

    // 这条路径是否来自 INI [file] 的预设清单（预设只读，删除时只能在本次运行隐藏）
    bool IsPresetRecent(const std::wstring& path) const;

    // 从「程序记录」里删掉一条并立刻写 sidecar（preset 不受影响，INI 也不动）
    void RemoveRecentScript(const std::wstring& path);

    // 本次运行隐藏一条 INI 预设（不落盘、不改 INI）
    void HidePresetRecent(const std::wstring& path);

    static std::wstring SettingsPath();
    static std::wstring RecentStorePath();

private:
    void RebuildRecent();
    std::vector<std::wstring> m_presetRecent;   // INI [file] recentN（只读预设，程序不写回）
    std::vector<std::wstring> m_progRecent;     // 程序真正打开过的（写 sidecar）
};
