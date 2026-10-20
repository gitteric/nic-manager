#include "actions.h"

#include <shellapi.h>
#include <wincrypt.h>

#include <cwctype>
#include <thread>

#include "ipaddr.h"
#include "settings.h"
#include "strutil.h"

namespace actions {
namespace {

constexpr wchar_t kAskPassVar[] = L"NICMANAGER_SSH_ASKPASS";  // DPAPI blob of the SSH password
constexpr DWORD kRdpFileLifetimeMs = 30000;                   // mstsc has read the file by then

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// System32 as seen by this process. A 32-bit build on 64-bit Windows
// would otherwise land in SysWOW64, which has no OpenSSH.
std::wstring NativeSystemDir() {
    wchar_t dir[MAX_PATH] = {};
    BOOL wow = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &wow) && wow) {
        GetWindowsDirectoryW(dir, MAX_PATH);
        return std::wstring(dir) + L"\\Sysnative";
    }
    GetSystemDirectoryW(dir, MAX_PATH);
    return dir;
}

// System32 as a native (64-bit) child such as cmd.exe sees it: "Sysnative"
// exists only for 32-bit processes.
std::wstring ChildSystemDir() {
    wchar_t dir[MAX_PATH] = {};
    GetWindowsDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\System32";
}

std::wstring LastErrorText(const std::wstring& what) {
    DWORD err = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring text = what + (msg ? L": " + Trim(msg) : L".");
    if (msg) LocalFree(msg);
    return text;
}

bool Spawn(const std::wstring& exe, const std::wstring& args, DWORD flags, const std::wstring& what,
           std::wstring& err) {
    std::wstring cmd = Quote(exe) + L" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        err = LastErrorText(L"Could not start " + what);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// The .rdp "password 51:b:" value: DPAPI blob of the UTF-16 password, hex.
// Only the same Windows user on the same machine can decrypt it.
std::wstring RdpPasswordHex(const std::wstring& pw) {
    DATA_BLOB in{(DWORD)(pw.size() * sizeof(wchar_t)), (BYTE*)pw.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"psw", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring s;
    s.reserve(out.cbData * 2);
    for (DWORD i = 0; i < out.cbData; ++i) {
        s += hex[out.pbData[i] >> 4];
        s += hex[out.pbData[i] & 15];
    }
    LocalFree(out.pbData);
    return s;
}

std::wstring SafeFileName(std::wstring s) {
    for (auto& c : s)
        if (!iswalnum(c) && c != L'.' && c != L'-') c = L'_';
    return s;
}

}  // namespace

// ---- RDP ----------------------------------------------------------------

std::wstring ConnectRdp(const std::wstring& host, const RdpSettings& s) {
    // Only the clipboard is shared; drives, printers, devices, microphone
    // and camera stay local. Sound plays on this computer.
    std::wstring rdp;
    auto line = [&](const std::wstring& l) { rdp += l + L"\r\n"; };
    line(L"full address:s:" + host);
    if (!s.user.empty()) line(L"username:s:" + s.user);
    if (!s.password.empty()) {
        std::wstring hex = RdpPasswordHex(s.password);
        if (hex.empty()) return LastErrorText(L"Could not encrypt the RDP password");
        line(L"password 51:b:" + hex);
        line(L"prompt for credentials:i:0");
    }
    if (s.fullscreen) {
        line(L"screen mode id:i:2");
        line(L"use multimon:i:0");
    } else {
        line(L"screen mode id:i:1");
        line(L"desktopwidth:i:1024");
        line(L"desktopheight:i:768");
        line(L"smart sizing:i:1");
    }
    for (const wchar_t* l : {
             L"session bpp:i:32", L"connection type:i:7", L"networkautodetect:i:1", L"bandwidthautodetect:i:1",
             L"compression:i:1", L"displayconnectionbar:i:1", L"autoreconnection enabled:i:1",
             L"authentication level:i:2", L"enablecredsspsupport:i:1", L"promptcredentialonce:i:1",
             L"redirectclipboard:i:1", L"redirectprinters:i:0", L"redirectcomports:i:0", L"redirectsmartcards:i:0",
             L"redirectdrives:i:0", L"drivestoredirect:s:", L"redirectposdevices:i:0", L"redirectwebauthn:i:0",
             L"devicestoredirect:s:", L"usbdevicestoredirect:s:", L"camerastoredirect:s:", L"audiomode:i:0",
             L"audiocapturemode:i:0", L"redirectlocation:i:0",
         })
        line(l);

    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"NicManager";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + SafeFileName(host) + L".rdp";

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return LastErrorText(L"Could not write the RDP file");
    const wchar_t bom = 0xFEFF;  // mstsc reads UTF-16LE with BOM
    DWORD written = 0;
    BOOL ok = WriteFile(f, &bom, sizeof(bom), &written, nullptr) &&
              WriteFile(f, rdp.data(), (DWORD)(rdp.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
    SecureZeroMemory(rdp.data(), rdp.size() * sizeof(wchar_t));
    if (!ok) {
        DeleteFileW(path.c_str());
        return LastErrorText(L"Could not write the RDP file");
    }

    std::wstring err;
    if (!Spawn(NativeSystemDir() + L"\\mstsc.exe", Quote(path), 0, L"mstsc", err)) {
        DeleteFileW(path.c_str());
        return err;
    }
    std::thread([path] {
        Sleep(kRdpFileLifetimeMs);
        DeleteFileW(path.c_str());
    }).detach();
    return {};
}

// ---- SSH ----------------------------------------------------------------

std::wstring ConnectSsh(const std::wstring& host, const SshSettings& s) {
    const std::wstring ssh = NativeSystemDir() + L"\\OpenSSH\\ssh.exe";
    if (!FileExists(ssh))
        return L"The Windows OpenSSH client is not installed (Settings > Apps > Optional features > OpenSSH Client).";

    // accept-new trusts a host key on first contact but still refuses a
    // changed one; one password attempt so a wrong password fails at once.
    std::wstring target = s.user.empty() ? host : s.user + L"@" + host;
    std::wstring args = L"-p " + std::to_wstring(s.port) +
                        L" -o StrictHostKeyChecking=accept-new -o NumberOfPasswordPrompts=1 " + Quote(target);
    // cmd keeps the window open with the error when ssh fails. cmd runs
    // natively, so it gets ssh's System32 path, not the Sysnative alias.
    const std::wstring sshForCmd = ChildSystemDir() + L"\\OpenSSH\\ssh.exe";
    std::wstring line = L"/s /c \"title SSH " + target + L" & " + Quote(sshForCmd) + L" " + args + L" || pause\"";

    // ssh.exe asks this executable for the password (SSH_ASKPASS); the
    // password travels DPAPI-encrypted in the environment of the child only.
    bool askpass = !s.password.empty();
    if (askpass) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring blob = Utf8ToWide(settings::ProtectString(s.password));
        if (blob.empty()) return LastErrorText(L"Could not encrypt the SSH password");
        SetEnvironmentVariableW(kAskPassVar, blob.c_str());
        SetEnvironmentVariableW(L"SSH_ASKPASS", self);
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", L"force");
        SetEnvironmentVariableW(L"DISPLAY", L"localhost:0");  // older OpenSSH only uses askpass with DISPLAY
    }
    std::wstring err;
    bool ok = Spawn(NativeSystemDir() + L"\\cmd.exe", line, CREATE_NEW_CONSOLE, L"the SSH console", err);
    if (askpass)
        for (const wchar_t* v : {kAskPassVar, L"SSH_ASKPASS", L"SSH_ASKPASS_REQUIRE", L"DISPLAY"})
            SetEnvironmentVariableW(v, nullptr);
    return ok ? std::wstring() : err;
}

bool HandleAskPass() {
    wchar_t probe[2];
    DWORD n = GetEnvironmentVariableW(kAskPassVar, probe, 0);
    if (n == 0) return false;
    std::wstring blob(n, L'\0');
    blob.resize(GetEnvironmentVariableW(kAskPassVar, blob.data(), n));

    // Only answer password prompts, never e.g. a host key confirmation.
    std::wstring prompt = GetCommandLineW();
    for (auto& c : prompt) c = (wchar_t)towlower(c);
    if (prompt.find(L"password") == std::wstring::npos) return true;

    std::string pw = WideToUtf8(settings::UnprotectString(WideToUtf8(blob)));
    if (pw.empty()) return true;
    pw += '\n';
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), pw.data(), (DWORD)pw.size(), &written, nullptr);
    SecureZeroMemory(pw.data(), pw.size());
    return true;
}

// ---- TightVNC -----------------------------------------------------------

std::wstring FindTightVnc() {
    for (const wchar_t* var : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) {
        wchar_t dir[MAX_PATH] = {};
        if (GetEnvironmentVariableW(var, dir, MAX_PATH) == 0) continue;
        std::wstring p = std::wstring(dir) + L"\\TightVNC\\tvnviewer.exe";
        if (FileExists(p)) return p;
    }
    return {};
}

std::wstring ConnectTightVnc(const std::wstring& host, const VncSettings& s, const std::wstring& viewerPath) {
    std::wstring viewer = viewerPath.empty() ? FindTightVnc() : viewerPath;
    if (viewer.empty()) return L"Set the TightVNC viewer path in the VNC action.";
    if (!FileExists(viewer)) return L"TightVNC viewer not found at " + viewer + L".";
    std::wstring args = L"-host=" + host + L" -port=" + std::to_wstring(s.port);
    if (!s.password.empty()) args += L" " + Quote(L"-password=" + s.password);
    std::wstring err;
    return Spawn(viewer, args, 0, L"the TightVNC viewer", err) ? std::wstring() : err;
}

// ---- Browser ------------------------------------------------------------

namespace {
bool HasHttpScheme(const std::wstring& s) {
    std::wstring l = s;
    for (auto& c : l) c = (wchar_t)towlower(c);
    return l.rfind(L"http://", 0) == 0 || l.rfind(L"https://", 0) == 0;
}
}  // namespace

bool IsValidBrowserTarget(const std::wstring& s) {
    if (s.empty() || s.find_first_of(L" \t\"<>") != std::wstring::npos) return false;
    std::wstring rest = s;
    if (HasHttpScheme(s)) rest = s.substr(s.find(L"://") + 3);
    else if (s.find(L"://") != std::wstring::npos) return false;  // only http and https
    // The authority (host[:port]) must be a valid host.
    std::wstring authority = rest.substr(0, rest.find_first_of(L"/?#"));
    size_t colon = authority.rfind(L':');
    if (colon != std::wstring::npos) {
        std::wstring port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || port.find_first_not_of(L"0123456789") != std::wstring::npos)
            return false;
        int p = _wtoi(port.c_str());
        if (p < 1 || p > 65535) return false;
        authority.resize(colon);
    }
    return ip::IsValidHost(authority);
}

std::wstring BrowserUrl(const BrowserSettings& s) { return HasHttpScheme(s.url) ? s.url : L"http://" + s.url; }

std::wstring OpenBrowser(const BrowserSettings& s) {
    std::wstring url = BrowserUrl(s);
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        return L"Could not open " + url + L" in the default browser.";
    return {};
}

// ---- dispatch -----------------------------------------------------------

std::wstring Run(const HostEntry& host, const HostAction& action, const std::wstring& tightVncPath) {
    if (auto* r = std::get_if<RdpSettings>(&action.settings)) return ConnectRdp(host.address, *r);
    if (auto* p = std::get_if<SshSettings>(&action.settings)) return ConnectSsh(host.address, *p);
    if (auto* v = std::get_if<VncSettings>(&action.settings)) return ConnectTightVnc(host.address, *v, tightVncPath);
    if (auto* b = std::get_if<BrowserSettings>(&action.settings)) return OpenBrowser(*b);
    return {};
}

const wchar_t* TypeName(const HostAction& action) {
    switch (action.settings.index()) {
    case 0: return L"RDP";
    case 1: return L"SSH";
    case 2: return L"VNC";
    default: return L"Browser";
    }
}

std::wstring Describe(const HostAction& action) {
    std::wstring s = TypeName(action);
    if (auto* r = std::get_if<RdpSettings>(&action.settings)) {
        if (!r->user.empty()) s += L" " + r->user;
        if (!r->fullscreen) s += L" (window)";
    } else if (auto* p = std::get_if<SshSettings>(&action.settings)) {
        if (!p->user.empty()) s += L" " + p->user;
        if (p->port != 22) s += L" :" + std::to_wstring(p->port);
    } else if (auto* v = std::get_if<VncSettings>(&action.settings)) {
        if (v->port != 5900) s += L" :" + std::to_wstring(v->port);
    } else if (auto* b = std::get_if<BrowserSettings>(&action.settings)) {
        s += L" " + BrowserUrl(*b);
    }
    return s;
}

}  // namespace actions
