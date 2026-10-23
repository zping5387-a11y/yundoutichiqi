// framework.h - 公共头文件（不使用预编译头，便于命令行 / 手工编译）
#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include <SDKDDKVer.h>

#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxole.h>

#include <atlbase.h>

#include <gdiplus.h>
#include <shellapi.h>

#include <sapi.h>
#include <sphelper.h>

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>

#include "Resource.h"

#pragma warning(disable: 4996)   // sphelper.h 内部使用 GetVersionEx

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")

// 使用 Common Controls 6.0（现代控件外观）
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 语音识别事件通知消息
constexpr UINT WM_APP_SPEECH_EVENT = WM_APP + 41;

// 托盘图标回调消息
constexpr UINT WM_APP_TRAY = WM_APP + 42;

// 离线语音模型下载进度通知（后台线程 → 主面板）
constexpr UINT WM_APP_MODEL_EVENT = WM_APP + 43;

// 全局热键 ID
enum : int
{
    HK_TOGGLE_SHOW = 1,
    HK_TOGGLE_SPEECH,
    HK_TOGGLE_CMD,       // 语音命令开关（与跟读相互独立）
    HK_TOGGLE_LOCK,
    HK_SCROLL_UP,
    HK_SCROLL_DOWN,
    HK_FONT_UP,
    HK_FONT_DOWN,
    HK_TOGGLE_MIRROR,
    HK_TOGGLE_AUTOSCROLL,
    HK_GOTO_TOP,
    HK_TOGGLE_HIGHLIGHT,
    HK_SIZE_W_INC,
    HK_SIZE_W_DEC,
    HK_SIZE_H_INC,
    HK_SIZE_H_DEC,
    HK_TOGGLE_PANEL,     // 显示 / 隐藏主控制面板
    HK_QUIT,
};

// 托盘右键菜单命令
enum : UINT
{
    IDM_TRAY_SHOW_PANEL = 1910,
    IDM_TRAY_SHOW_OVERLAY,
    IDM_TRAY_SPEECH,
    IDM_TRAY_CMD,
    IDM_TRAY_LOCK,
    IDM_TRAY_QUIT,
};
