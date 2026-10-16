// NIC Manager – entry point.
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>

#include "mainwindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const bool demo = cmdLine && wcsstr(cmdLine, L"--demo") != nullptr;

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
    {
        MainWindow window;
        if (window.Create(inst, demo)) {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            code = 0;
        }
    }
    WSACleanup();
    if (mutex) CloseHandle(mutex);
    return code;
}
