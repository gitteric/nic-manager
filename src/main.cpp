// NIC Manager – entry point.
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>

#include <vector>

#include "actions.h"
#include "mainwindow.h"

namespace {

// Other NicManager.exe processes in this session, by exe file name.
std::vector<DWORD> OtherInstances() {
    std::vector<DWORD> pids;
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const wchar_t* selfName = wcsrchr(self, L'\\') ? wcsrchr(self, L'\\') + 1 : self;
    DWORD mySession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        DWORD session = 0;
        if (pe.th32ProcessID != GetCurrentProcessId() && _wcsicmp(pe.szExeFile, selfName) == 0 &&
            ProcessIdToSessionId(pe.th32ProcessID, &session) && session == mySession)
            pids.push_back(pe.th32ProcessID);
    }
    CloseHandle(snap);
    return pids;
}

// The single-instance mutex is taken. Normally the owner has a window, which
// is told to show itself. If it has none (still starting, or stuck without
// UI) or does not respond, offer to end it so this start can take over.
// Returns true when this process now owns the mutex and should run.
bool TakeOverFromStaleInstance(HANDLE mutex) {
    for (int i = 0; i < 10; ++i) {  // give a starting instance up to ~5 s to create its window
        if (HWND existing = FindWindowW(MainWindow::kClassName, nullptr)) {
            DWORD_PTR r = 0;
            if (SendMessageTimeoutW(existing, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 3000, &r)) {
                PostMessageW(existing, MainWindow::WM_APP_SHOW, 0, 0);
                return false;
            }
            break;  // window exists but is hung
        }
        if (!mutex || WaitForSingleObject(mutex, 500) != WAIT_TIMEOUT) return mutex != nullptr;  // owner exited
    }
    std::vector<DWORD> pids = OtherInstances();
    if (pids.empty()) return mutex && WaitForSingleObject(mutex, 0) != WAIT_TIMEOUT;
    if (MessageBoxW(nullptr,
                    L"Another NIC Manager process is running but has no responding window.\n\n"
                    L"End it and start NIC Manager again?",
                    L"NIC Manager", MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND) != IDYES)
        return false;
    for (DWORD pid : pids)
        if (HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
            TerminateProcess(p, 1);
            WaitForSingleObject(p, 3000);
            CloseHandle(p);
        }
    // An ended owner leaves the mutex abandoned; waiting on it hands it to us.
    if (mutex && WaitForSingleObject(mutex, 3000) != WAIT_TIMEOUT) return true;
    MessageBoxW(nullptr, L"The other NIC Manager process could not be ended. End it in Task Manager.", L"NIC Manager",
                MB_OK | MB_ICONERROR | MB_TOPMOST);
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (actions::HandleAskPass()) return 0;  // started by ssh.exe as SSH_ASKPASS

    const bool demo = cmdLine && wcsstr(cmdLine, L"--demo") != nullptr;
    const bool minimized = cmdLine && wcsstr(cmdLine, L"--minimized") != nullptr;  // autostart task

    // Single instance: a second start just brings the existing window up.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\NicManager.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS && !TakeOverFromStaleInstance(mutex)) return 0;

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
    // processes (netsh, ipconfig, schtasks) die with it through their job object.
    if (mutex) CloseHandle(mutex);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return code;
}
