// Host actions (RDP / PuTTY / TightVNC / Browser). The Connect* functions are hulls:
// they receive the stored settings and are the place to add the real
// launch code.
#pragma once

#include <windows.h>

#include <string>

#include "model.h"

namespace actions {

void ConnectRdp(HWND owner, const RdpSettings& s);
void ConnectPutty(HWND owner, const PuttySettings& s);
void ConnectTightVnc(HWND owner, const VncSettings& s);
void OpenBrowser(HWND owner, const BrowserSettings& s);

void Run(HWND owner, const HostAction& action);  // dispatches to the above

const wchar_t* TypeName(const HostAction& action);  // "RDP", "PuTTY", "VNC", "Browser"
std::wstring Describe(const HostAction& action);    // "RDP admin@10.0.0.5"
std::wstring BrowserUrl(const BrowserSettings& s);  // "http://10.0.0.5:8080/"

}  // namespace actions
