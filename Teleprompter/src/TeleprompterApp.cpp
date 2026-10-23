// TeleprompterApp.cpp - 程序入口
#include "TeleprompterApp.h"
#include "ControlDlg.h"

CTeleprompterApp theApp;

BOOL CTeleprompterApp::InitInstance()
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&icc);

    CWinApp::InitInstance();
    AfxEnableControlContainer();

    // 固定按系统 DPI 缩放，保证文字清晰
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusInput, nullptr);

    // SAPI 需要 COM
    if (!AfxOleInit())
    {
        ::MessageBoxW(nullptr, L"COM 初始化失败，语音识别不可用。", L"芸豆提词器", MB_ICONWARNING);
    }

    // ---------------- 单实例 ----------------
    // 同一个登录会话里只允许跑一个提词器：否则两套热键互相抢占、两份浮层叠在一起。
    // 第二个实例不做任何事，只把已有实例的面板叫出来（面板可能是隐藏状态）。
    m_hMutex = ::CreateMutexW(nullptr, TRUE, L"Local\\TeacherPrompter.SingleInstance");
    if (m_hMutex && ::GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND hPrev = ::FindWindowW(nullptr, L"芸豆提词器 · 教学模式"))
        {
            ::PostMessageW(hPrev, ::RegisterWindowMessageW(L"TeacherPrompter.ShowPanel"), 0, 0);
            if (::IsIconic(hPrev)) ::ShowWindow(hPrev, SW_RESTORE);
            ::SetForegroundWindow(hPrev);
        }
        ::CloseHandle(m_hMutex);
        m_hMutex = nullptr;
        return FALSE;          // 静默退出，不再启动第二套热键 / 浮层
    }

    CControlDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    return FALSE;   // 对话框关闭后直接退出
}

int CTeleprompterApp::ExitInstance()
{
    if (m_hMutex)
    {
        ::ReleaseMutex(m_hMutex);
        ::CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
    if (m_gdiplusToken)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    AfxOleTerm(FALSE);
    return CWinApp::ExitInstance();
}
