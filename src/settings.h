// Persistent settings in HKCU\Software\NicManager.
//
//   PingCycleMs  REG_DWORD  ping interval per host (100..30000)
//   WindowX/Y    REG_DWORD  last window position (signed)
//   AlwaysOnTop  REG_DWORD  0/1
//   MinimizeToTray REG_DWORD 0/1 (1: no taskbar button, minimize hides to tray)
//   TightVncPath REG_SZ   full path of tvnviewer.exe (edited in the VNC action dialog)
//   CerhostPath  REG_SZ   full path of cerhost.exe (edited in the cerhost action dialog)
//   ExpandedAdapter REG_SZ  GUID of the adapter shown expanded (restored at start)
//   HiddenAdapters REG_SZ   GUIDs of adapters hidden from the list, ';'-separated
//   ShowHidden   REG_DWORD  0/1 (1: hidden adapters are listed anyway, dimmed)
//   Design       REG_DWORD  index into theme::kDesigns (0 Standard, 1 Color-blind friendly)
//   Hosts        REG_SZ     JSON array of hosts + actions (passwords DPAPI-encrypted)
//   HostGroups   REG_SZ     JSON array of host groups (name, ping, expanded); hosts refer to them by index
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
    std::wstring cerhostPath;
    std::wstring expandedAdapter;
    std::vector<std::wstring> hiddenAdapters;  // GUIDs
    bool showHidden = false;
    int design = 0;
    std::vector<HostEntry> hosts;
    std::vector<HostGroup> groups;
    std::vector<Preset> presets;
};

namespace settings {

constexpr DWORD kMinPingCycleMs = 100;
constexpr DWORD kMaxPingCycleMs = 30000;

AppSettings Load();
void SaveGeneral(const AppSettings& s);  // cycle, window position, flags, show hidden, design
void SaveHosts(const std::vector<HostEntry>& hosts, const std::vector<HostGroup>& groups);
void SavePresets(const std::vector<Preset>& presets);

// Host export file (UTF-8 JSON): {"format":"nic-manager-hosts","version":1,"groups":[...],"hosts":[...]}.
// Passwords stay DPAPI-encrypted, so they only come back for the same Windows user.
// withGroups=false (a single host): no groups and no group reference.
std::string HostsExportJson(const std::vector<HostEntry>& hosts, const std::vector<HostGroup>& groups,
                            bool withGroups);
// hosts[i].group: index into groups, -1 top level, -2 not given (an existing host keeps its group).
bool HostsImportJson(const std::string& utf8, std::vector<HostEntry>& hosts, std::vector<HostGroup>& groups,
                     std::wstring* err);
// Full backup (UTF-8 JSON): {"format":"nic-manager-backup","version":1,"settings":{...},
// "groups":[...],"hosts":[...],"presets":[...]}. Hosts and presets use the formats of their
// own export files, so Import > Hosts and Import > Presets also read a backup.
// "settings": ping cycle, always on top, minimize to tray, design and the program paths.
std::string BackupExportJson(const AppSettings& s);
// Fills hosts, groups, presets, options and paths of `out`; options missing from the
// file keep the values `out` had. Paths are empty when the file has none.
bool BackupImportJson(const std::string& utf8, AppSettings& out, std::wstring* err);
// Same name (case-insensitive) replaces the existing preset; returns the number merged.
int MergePresets(std::vector<Preset>& presets, const std::vector<Preset>& in);

void SaveTightVncPath(const std::wstring& path);
void SaveCerhostPath(const std::wstring& path);
void SaveExpandedAdapter(const std::wstring& guid);  // empty: none
void SaveHiddenAdapters(const std::vector<std::wstring>& guids);

// DPAPI for the current Windows user ("dpapi:<base64>"); empty on failure.
std::string ProtectString(const std::wstring& plain);
std::wstring UnprotectString(const std::string& stored);

// Preset file format (UTF-8 JSON), shared by registry storage and export/import.
std::string PresetsToJson(const std::vector<Preset>& presets);
bool PresetsFromJson(const std::string& utf8, std::vector<Preset>& out, std::wstring* err);

bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8(const std::wstring& path, const std::string& data);

}  // namespace settings
