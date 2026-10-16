#include "starttask.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "netcmd.h"
#include "strutil.h"

namespace starttask {
namespace {

constexpr wchar_t kTask[] = L"NIC Manager";
constexpr wchar_t kAutostartTask[] = L"NIC Manager Autostart";
constexpr wchar_t kShortcutName[] = L"\\NIC Manager.lnk";
constexpr DWORD kTimeoutMs = 15000;

std::wstring SystemExe(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\" + name;
}

std::wstring SelfPath() {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return self;
}

std::wstring XmlEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': o += L"&amp;"; break;
        case L'<': o += L"&lt;"; break;
        case L'>': o += L"&gt;"; break;
        case L'"': o += L"&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

// DOMAIN\user, as the task principal and logon trigger expect.
// DOMAIN\user, as the task principal and logon trigger expect.
std::wstring CurrentUser() {
    wchar_t user[256] = {}, domain[256] = {};
    DWORD len = 256;
    GetEnvironmentVariableW(L"USERDOMAIN", domain, 256);
    GetUserNameW(user, &len);
    return domain[0] ? std::wstring(domain) + L"\\" + user : std::wstring(user);
}

bool TaskExists(const wchar_t* name) {
    auto r = netcmd::RunProcess(SystemExe(L"schtasks.exe"), L"/Query /TN \"" + std::wstring(name) + L"\"", kTimeoutMs);
    return r.started && !r.timedOut && r.exitCode == 0;
}

// Task definition: highest privileges, interactive (needs the user's
// desktop for the tray icon), no time limit, never started twice.
std::wstring TaskXml(bool logon, const std::wstring& user) {
    const std::wstring exe = SelfPath();
    std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    std::wstring x =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>Starts NIC Manager with administrator rights without a UAC prompt."
        L"</Description></RegistrationInfo>\r\n";
    if (logon)
        x += L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + XmlEscape(user) +
             L"</UserId></LogonTrigger></Triggers>\r\n";
    else
        x += L"  <Triggers/>\r\n";
    x += L"  <Principals><Principal id=\"Author\"><UserId>" + XmlEscape(user) +
         L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
         L"</Principal></Principals>\r\n"
         L"  <Settings>\r\n"
         L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
         L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
         L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
         L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
         L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
         L"    <Priority>5</Priority>\r\n"
         L"  </Settings>\r\n"
         L"  <Actions Context=\"Author\"><Exec><Command>\"" + XmlEscape(exe) + L"\"</Command>" +
         (logon ? L"<Arguments>--minimized</Arguments>" : L"") + L"<WorkingDirectory>" + XmlEscape(dir) +
         L"</WorkingDirectory></Exec></Actions>\r\n"
         L"</Task>\r\n";
    return x;
}

std::wstring CreateTask(const wchar_t* name, const std::wstring& xml) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"NicManager-task.xml";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"Could not write the task definition.";
    const wchar_t bom = 0xFEFF;
    DWORD w = 0;
    WriteFile(f, &bom, sizeof(bom), &w, nullptr);
    WriteFile(f, xml.data(), (DWORD)(xml.size() * sizeof(wchar_t)), &w, nullptr);
    CloseHandle(f);
    auto r = netcmd::RunProcess(SystemExe(L"schtasks.exe"),
                                L"/Create /F /TN \"" + std::wstring(name) + L"\" /XML \"" + path + L"\"", kTimeoutMs);
    DeleteFileW(path.c_str());
    if (r.started && !r.timedOut && r.exitCode == 0) return {};
    std::wstring out = Trim(r.output);
    return L"schtasks could not create \"" + std::wstring(name) + L"\"" + (out.empty() ? L"." : L": " + out);
}

void DeleteTask(const wchar_t* name) {
    netcmd::RunProcess(SystemExe(L"schtasks.exe"), L"/Delete /F /TN \"" + std::wstring(name) + L"\"", kTimeoutMs);
}

std::wstring ShortcutPath() {
    PWSTR desk = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk))) return {};
    std::wstring p = std::wstring(desk) + kShortcutName;
    CoTaskMemFree(desk);
    return p;
}

// Desktop shortcut that runs the on-demand task (schtasks /run), with the
// app's icon. The schtasks console starts minimized, so nothing flashes up.
std::wstring CreateShortcut() {
    std::wstring path = ShortcutPath();
    if (path.empty()) return L"Could not find the desktop folder.";
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(hr)) {
        link->SetPath(SystemExe(L"schtasks.exe").c_str());
        link->SetArguments((L"/Run /TN \"" + std::wstring(kTask) + L"\"").c_str());
        link->SetIconLocation(SelfPath().c_str(), 0);
        link->SetShowCmd(SW_SHOWMINNOACTIVE);
        link->SetDescription(L"NIC Manager (starts without a UAC prompt)");
        IPersistFile* file = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&file));
        if (SUCCEEDED(hr)) {
            hr = file->Save(path.c_str(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return SUCCEEDED(hr) ? std::wstring() : L"Could not create the desktop shortcut.";
}

}  // namespace

bool IsInstalled() { return TaskExists(kTask); }
bool AutostartInstalled() { return TaskExists(kAutostartTask); }

std::wstring Install(bool autostart, bool desktopShortcut) {
    const std::wstring user = CurrentUser();
    std::wstring err = CreateTask(kTask, TaskXml(false, user));
    if (err.empty()) {
        if (autostart) err = CreateTask(kAutostartTask, TaskXml(true, user));
        else DeleteTask(kAutostartTask);
    }
    if (err.empty() && desktopShortcut) err = CreateShortcut();
    return err;
}

std::wstring Remove() {
    DeleteTask(kTask);
    DeleteTask(kAutostartTask);
    std::wstring lnk = ShortcutPath();
    if (!lnk.empty()) DeleteFileW(lnk.c_str());
    if (TaskExists(kTask) || TaskExists(kAutostartTask)) return L"schtasks could not delete the tasks.";
    return {};
}

}  // namespace starttask
