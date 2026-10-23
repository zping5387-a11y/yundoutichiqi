// TeleprompterApp.h
#pragma once

#include "framework.h"

class CTeleprompterApp : public CWinApp
{
public:
    BOOL InitInstance() override;
    int  ExitInstance() override;

private:
    ULONG_PTR m_gdiplusToken = 0;
    HANDLE    m_hMutex = nullptr;      // 单实例互斥体，进程存活期间一直持有
};

extern CTeleprompterApp theApp;
