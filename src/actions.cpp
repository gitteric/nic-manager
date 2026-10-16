#include "actions.h"

namespace actions {
namespace {

void NotImplemented(HWND owner, const std::wstring& what) {
    MessageBoxW(owner, (what + L"\n\nThis action is a stub. Implement it in src/actions.cpp.").c_str(),
                L"NIC Manager", MB_OK | MB_ICONINFORMATION);
}

}  // namespace

// Called on the UI thread. Launch the client and return immediately; do
// not wait for it to exit.
void ConnectRdp(HWND owner, const RdpSettings& s) {
    // TODO: Implementation sketch:
    //   1. Store the credential so mstsc can log in without prompting:
    //        cmdkey /generic:TERMSRV/<host> /user:<user> /pass:<password>
    //      (or CredWriteW with CRED_TYPE_DOMAIN_PASSWORD, target "TERMSRV/<host>")
    //   2. Launch: mstsc.exe /v:<host> /f            (full screen)
    //              mstsc.exe /v:<host> /w:1024 /h:768 (windowed)
    NotImplemented(owner, L"Connect RDP to " + s.host + (s.user.empty() ? L"" : L" as " + s.user) +
                              (s.fullscreen ? L" (full screen)" : L" (1024x768)"));
}

void ConnectPutty(HWND owner, const PuttySettings& s) {
    // TODO: Implementation sketch:
    //   putty.exe -ssh <user>@<host> -P <port> [-pw <password>]
    //   (-pw is visible in the process list; consider pageant/keys instead)
    NotImplemented(owner, L"Connect PuTTY to " + s.host + L":" + std::to_wstring(s.port) +
                              (s.user.empty() ? L"" : L" as " + s.user));
}

void ConnectTightVnc(HWND owner, const VncSettings& s) {
    // TODO: Implementation sketch:
    //   tvnviewer.exe -host=<host> -port=<port> [-password=<password>]
    NotImplemented(owner, L"Connect TightVNC to " + s.host + L":" + std::to_wstring(s.port));
}

void Run(HWND owner, const HostAction& action) {
    if (auto* r = std::get_if<RdpSettings>(&action.settings)) ConnectRdp(owner, *r);
    else if (auto* p = std::get_if<PuttySettings>(&action.settings)) ConnectPutty(owner, *p);
    else if (auto* v = std::get_if<VncSettings>(&action.settings)) ConnectTightVnc(owner, *v);
}

const wchar_t* TypeName(const HostAction& action) {
    switch (action.settings.index()) {
    case 0: return L"RDP";
    case 1: return L"PuTTY";
    default: return L"VNC";
    }
}

std::wstring Describe(const HostAction& action) {
    std::wstring s = TypeName(action);
    s += L" ";
    if (auto* r = std::get_if<RdpSettings>(&action.settings)) {
        if (!r->user.empty()) s += r->user + L"@";
        s += r->host;
    } else if (auto* p = std::get_if<PuttySettings>(&action.settings)) {
        if (!p->user.empty()) s += p->user + L"@";
        s += p->host;
        if (p->port != 22) s += L":" + std::to_wstring(p->port);
    } else if (auto* v = std::get_if<VncSettings>(&action.settings)) {
        s += v->host;
        if (v->port != 5900) s += L":" + std::to_wstring(v->port);
    }
    return s;
}

}  // namespace actions
