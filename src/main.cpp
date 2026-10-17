// NIC Manager – entry point.
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>

#include "actions.h"
#include "mainwindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (actions::HandleAskPass()) return 0;  // started by ssh.exe as SSH_ASKPASS

    const bool demo = cmdLine && wcsstr(cmdLine, L"--demo") != nullptr;
    const bool minimized = cmdLine && wcsstr(cmdLine, L"--minimized") != nullptr;  // autostart task

    // Single instance: a second start just brings the existing window up.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\NicManager.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(MainWindow::kClassName, nullptr))
            PostMessageW(existing, MainWindow::WM_APP_SHOW, 0, 0);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int code = 1;
    MainWindow window;
    if (window.Create(inst, demo, minimized)) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        code = 0;
    }
    // The window is gone and settings are saved as they change. End the
    // process here instead of waiting for worker threads (a running job, ping
    // or DNS lookups): nothing may keep NIC Manager alive after Exit. Child
    // processes (nmap, netsh) die with it through their job object.
    if (mutex) CloseHandle(mutex);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return code;
}
