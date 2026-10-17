// Persistent settings in HKCU\Software\NicManager.
//
//   PingCycleMs  REG_DWORD  ping interval per host (1..10000)
//   WindowX/Y    REG_DWORD  last window position (signed)
//   AlwaysOnTop  REG_DWORD  0/1
//   MinimizeToTray REG_DWORD 0/1 (1: no taskbar button, minimize hides to tray)
//   TightVncPath REG_SZ   full path of tvnviewer.exe (edited in the VNC action dialog)
//   ExpandedAdapter REG_SZ  GUID of the adapter shown expanded (restored at start)
//   NmapEnabled  REG_DWORD  0/1 (subnet scans in the IP address context menu)
//   NmapPath     REG_SZ     full path of nmap.exe
//   Hosts        REG_SZ     JSON array of hosts + actions (passwords DPAPI-encrypted)
//   Presets      REG_SZ     JSON document of IPv4 presets (same format as export files)
#pragma once

#include <windows.h>

#include <climits>
#include <string>
#include <vector>

#include "model.h"

struct AppSettings {
    DWORD pingCycleMs = 5000;
    int windowX = INT_MIN;
    int windowY = INT_MIN;
    bool alwaysOnTop = false;
    bool minimizeToTray = true;
    std::wstring tightVncPath;
    std::wstring expandedAdapter;
    bool nmapEnabled = false;
    std::wstring nmapPath;
    std::vector<HostEntry> hosts;
    std::vector<Preset> presets;
};

namespace settings {

constexpr DWORD kMinPingCycleMs = 1;
constexpr DWORD kMaxPingCycleMs = 10000;

AppSettings Load();
void SaveGeneral(const AppSettings& s);  // cycle, window position, flags, nmap
void SaveHosts(const std::vector<HostEntry>& hosts);
void SavePresets(const std::vector<Preset>& presets);
void SaveTightVncPath(const std::wstring& path);
void SaveExpandedAdapter(const std::wstring& guid);  // empty: none

// DPAPI for the current Windows user ("dpapi:<base64>"); empty on failure.
std::string ProtectString(const std::wstring& plain);
std::wstring UnprotectString(const std::string& stored);

// Preset file format (UTF-8 JSON), shared by registry storage and export/import.
std::string PresetsToJson(const std::vector<Preset>& presets);
bool PresetsFromJson(const std::string& utf8, std::vector<Preset>& out, std::wstring* err);

bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8(const std::wstring& path, const std::string& data);

}  // namespace settings
