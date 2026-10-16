// Host actions: RDP (mstsc), SSH (Windows OpenSSH), TightVNC viewer and the
// default browser. Every launcher starts the client and returns at once.
#pragma once

#include <windows.h>

#include <string>

#include "model.h"

namespace actions {

// Each returns an empty string on success, otherwise a one-line reason.
// `host` is the address of the host entry the action belongs to.
std::wstring ConnectRdp(const std::wstring& host, const RdpSettings& s);
std::wstring ConnectSsh(const std::wstring& host, const SshSettings& s);
std::wstring ConnectTightVnc(const std::wstring& host, const VncSettings& s, const std::wstring& viewerPath);
std::wstring OpenBrowser(const BrowserSettings& s);

// Dispatches to the above.
std::wstring Run(const HostEntry& host, const HostAction& action, const std::wstring& tightVncPath);

const wchar_t* TypeName(const HostAction& action);  // "RDP", "SSH", "VNC", "Browser"
std::wstring Describe(const HostAction& action);    // "RDP admin", "SSH root :2222", "Browser http://..."
std::wstring BrowserUrl(const BrowserSettings& s);  // the URL, with http:// added when no scheme is given
bool IsValidBrowserTarget(const std::wstring& s);   // http(s) URL or host[:port][/path]

// Default install location of tvnviewer.exe, if present.
std::wstring FindTightVnc();

// SSH_ASKPASS helper: when ssh.exe runs this executable to ask for the
// password, print it and return true (the process should then exit).
bool HandleAskPass();

}  // namespace actions
