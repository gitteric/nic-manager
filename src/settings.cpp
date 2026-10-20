#include "settings.h"

#include <wincrypt.h>

#include <algorithm>

#include "ipaddr.h"
#include "json.h"
#include "strutil.h"

namespace settings {
namespace {

constexpr wchar_t kKey[] = L"Software\\NicManager";

DWORD ReadDword(const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return def;
    return v;
}

std::wstring ReadString(const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size == 0)
        return {};
    std::wstring s(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_SZ, nullptr, s.data(), &size) != ERROR_SUCCESS)
        return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

HKEY OpenWriteKey() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return nullptr;
    return key;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

void WriteString(const wchar_t* name, const std::wstring& v) {
    if (HKEY key = OpenWriteKey()) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

// ---- password protection (DPAPI, bound to the current Windows user) ----

constexpr char kDpapiPrefix[] = "dpapi:";

std::string Protect(const std::wstring& plain) {
    if (plain.empty()) return {};
    DATA_BLOB in{(DWORD)(plain.size() * sizeof(wchar_t)), (BYTE*)plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"NicManager", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    DWORD len = 0;
    std::string b64;
    if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len)) {
        b64.resize(len);
        CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
        b64.resize(len);
    }
    LocalFree(out.pbData);
    return kDpapiPrefix + b64;
}

std::wstring Unprotect(const std::string& stored) {
    if (stored.rfind(kDpapiPrefix, 0) != 0) return Utf8ToWide(stored);  // tolerate plain text
    std::string b64 = stored.substr(sizeof(kDpapiPrefix) - 1);
    DWORD len = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
        return {};
    std::string bin(len, '\0');
    CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, (BYTE*)bin.data(), &len, nullptr,
                         nullptr);
    DATA_BLOB in{len, (BYTE*)bin.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::wstring plain(reinterpret_cast<wchar_t*>(out.pbData), out.cbData / sizeof(wchar_t));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

// ---- JSON mapping -------------------------------------------------------

json::Value Str(const std::wstring& w) { return json::Value(WideToUtf8(w)); }
std::wstring WStr(const json::Value& v) { return Utf8ToWide(v.AsString()); }

json::Value StrArray(const std::vector<std::wstring>& list) {
    json::Value a = json::Value::Array();
    for (const auto& s : list) a.Push(Str(s));
    return a;
}

void ConfigToJson(const Ipv4Config& c, json::Value& o) {
    o.Set("dhcp", c.dhcp);
    o.Set("autoDns", c.autoDns);
    json::Value addrs = json::Value::Array();
    for (const auto& a : c.addresses) {
        json::Value e = json::Value::Object();
        e.Set("ip", Str(a.ip));
        e.Set("prefix", a.prefix);
        addrs.Push(std::move(e));
    }
    o.Set("addresses", std::move(addrs));
    o.Set("gateways", StrArray(c.gateways));
    o.Set("dns", StrArray(c.dns));
}

bool ConfigFromJson(const json::Value& o, Ipv4Config& c, std::wstring* err) {
    c = {};
    c.dhcp = o["dhcp"].AsBool(true);
    c.autoDns = c.dhcp && o["autoDns"].AsBool(true);
    for (const auto& e : o["addresses"].Items()) {
        IpEntry ip{WStr(e["ip"]), e["prefix"].AsInt(24)};
        if (ip::IsLinkLocal(ip.ip)) continue;  // older presets could contain the APIPA address
        if (!ip::ParseIpv4(ip.ip) || ip.prefix < 1 || ip.prefix > 32) {
            if (err) *err = L"invalid address '" + ip.ip + L"'";
            return false;
        }
        c.addresses.push_back(ip);
    }
    for (const auto& g : o["gateways"].Items()) {
        if (!ip::ParseIpv4(WStr(g))) {
            if (err) *err = L"invalid gateway '" + WStr(g) + L"'";
            return false;
        }
        c.gateways.push_back(WStr(g));
    }
    for (const auto& d : o["dns"].Items()) {
        if (!ip::ParseIpv4(WStr(d))) {
            if (err) *err = L"invalid DNS server '" + WStr(d) + L"'";
            return false;
        }
        if (c.dns.size() < 2) c.dns.push_back(WStr(d));
    }
    if (!c.dhcp && c.addresses.empty()) {
        if (err) *err = L"static configuration without an address";
        return false;
    }
    return true;
}

// groupRef: 0 only when set (registry), 1 always (export with groups), 2 never (single host).
json::Value HostsToJson(const std::vector<HostEntry>& hosts, int groupRef = 0) {
    json::Value arr = json::Value::Array();
    for (const auto& h : hosts) {
        json::Value o = json::Value::Object();
        o.Set("name", Str(h.name));
        o.Set("address", Str(h.address));
        o.Set("ping", h.ping);
        if (groupRef == 1 || (groupRef == 0 && h.group >= 0)) o.Set("group", h.group);
        json::Value acts = json::Value::Array();
        for (const auto& a : h.actions) {
            json::Value j = json::Value::Object();
            if (auto* r = std::get_if<RdpSettings>(&a.settings)) {
                j.Set("type", "rdp");
                j.Set("user", Str(r->user));
                j.Set("password", Protect(r->password));
                j.Set("fullscreen", r->fullscreen);
            } else if (auto* p = std::get_if<SshSettings>(&a.settings)) {
                j.Set("type", "ssh");
                j.Set("port", p->port);
                j.Set("user", Str(p->user));
                j.Set("password", Protect(p->password));
            } else if (auto* v = std::get_if<VncSettings>(&a.settings)) {
                j.Set("type", "vnc");
                j.Set("port", v->port);
                j.Set("password", Protect(v->password));
            } else if (auto* b = std::get_if<BrowserSettings>(&a.settings)) {
                j.Set("type", "browser");
                j.Set("url", Str(b->url));
            }
            acts.Push(std::move(j));
        }
        o.Set("actions", std::move(acts));
        arr.Push(std::move(o));
    }
    return arr;
}

json::Value GroupsToJson(const std::vector<HostGroup>& groups) {
    json::Value arr = json::Value::Array();
    for (const auto& g : groups) {
        json::Value o = json::Value::Object();
        o.Set("name", Str(g.name));
        o.Set("ping", g.ping);
        o.Set("expanded", g.expanded);
        arr.Push(std::move(o));
    }
    return arr;
}

std::vector<HostGroup> GroupsFromJson(const json::Value& arr) {
    std::vector<HostGroup> groups;
    for (const auto& o : arr.Items()) {
        HostGroup g;
        g.name = Trim(WStr(o["name"]));
        g.ping = o["ping"].AsBool(true);
        g.expanded = o["expanded"].AsBool(true);
        groups.push_back(std::move(g));
    }
    return groups;
}

std::vector<HostEntry> HostsFromJson(const json::Value& arr) {
    std::vector<HostEntry> hosts;
    for (const auto& o : arr.Items()) {
        HostEntry h;
        h.address = WStr(o["address"]);
        if (!ip::IsValidHost(h.address)) continue;
        h.name = Trim(WStr(o["name"]));
        if (h.name.empty()) h.name = h.address;
        h.ping = o["ping"].AsBool(true);
        h.group = o["group"].IsNumber() ? o["group"].AsInt(-1) : -2;  // -2: no group given
        for (const auto& j : o["actions"].Items()) {
            std::string type = j["type"].AsString();
            HostAction a;
            if (type == "rdp") {
                RdpSettings r;
                r.user = WStr(j["user"]);
                r.password = Unprotect(j["password"].AsString());
                r.fullscreen = j["fullscreen"].AsBool(true);
                a.settings = r;
            } else if (type == "ssh" || type == "putty") {  // "putty": before the rename
                SshSettings p;
                p.port = j["port"].AsInt(22);
                p.user = WStr(j["user"]);
                p.password = Unprotect(j["password"].AsString());
                a.settings = p;
            } else if (type == "vnc") {
                VncSettings v;
                v.port = j["port"].AsInt(5900);
                v.password = Unprotect(j["password"].AsString());
                a.settings = v;
            } else if (type == "browser") {
                BrowserSettings b;
                b.url = WStr(j["url"]);
                if (b.url.empty()) {  // older format: host, port, https
                    bool https = j["https"].AsBool(false);
                    int port = j["port"].AsInt(https ? 443 : 80);
                    std::wstring host = WStr(j["host"]).empty() ? h.address : WStr(j["host"]);
                    b.url = (https ? L"https://" : L"http://") + host +
                            (port == (https ? 443 : 80) ? L"" : L":" + std::to_wstring(port));
                }
                a.settings = b;
            } else {
                continue;
            }
            h.actions.push_back(std::move(a));
        }
        hosts.push_back(std::move(h));
    }
    return hosts;
}

}  // namespace

std::string ProtectString(const std::wstring& plain) { return Protect(plain); }
std::wstring UnprotectString(const std::string& stored) { return Unprotect(stored); }

void SaveTightVncPath(const std::wstring& path) { WriteString(L"TightVncPath", path); }
void SaveExpandedAdapter(const std::wstring& guid) { WriteString(L"ExpandedAdapter", guid); }

void SaveHiddenAdapters(const std::vector<std::wstring>& guids) {
    std::wstring v;
    for (const auto& g : guids) v += (v.empty() ? L"" : L";") + g;
    WriteString(L"HiddenAdapters", v);
}

std::string PresetsToJson(const std::vector<Preset>& presets) {
    json::Value root = json::Value::Object();
    root.Set("format", "nic-manager-ipv4-presets");
    root.Set("version", 1);
    json::Value arr = json::Value::Array();
    for (const auto& p : presets) {
        json::Value o = json::Value::Object();
        o.Set("name", Str(p.name));
        ConfigToJson(p.cfg, o);
        arr.Push(std::move(o));
    }
    root.Set("presets", std::move(arr));
    return json::Write(root, true);
}

bool PresetsFromJson(const std::string& utf8, std::vector<Preset>& out, std::wstring* err) {
    json::Value root;
    std::string perr;
    if (!json::Parse(utf8, root, &perr)) {
        if (err) *err = L"Not valid JSON: " + Utf8ToWide(perr);
        return false;
    }
    // Accept the full document or a bare array of presets.
    const json::Value& arr = root.IsArray() ? root : root["presets"];
    if (!arr.IsArray()) {
        if (err) *err = L"No \"presets\" array found";
        return false;
    }
    std::vector<Preset> result;
    for (const auto& o : arr.Items()) {
        Preset p;
        p.name = Trim(WStr(o["name"]));
        std::wstring cerr;
        if (p.name.empty()) {
            if (err) *err = L"Preset without a name";
            return false;
        }
        if (!ConfigFromJson(o, p.cfg, &cerr)) {
            if (err) *err = L"Preset '" + p.name + L"': " + cerr;
            return false;
        }
        result.push_back(std::move(p));
    }
    out = std::move(result);
    return true;
}

AppSettings Load() {
    AppSettings s;
    s.pingCycleMs = std::clamp<DWORD>(ReadDword(L"PingCycleMs", 5000), kMinPingCycleMs, kMaxPingCycleMs);
    s.windowX = (int)ReadDword(L"WindowX", (DWORD)INT_MIN);
    s.windowY = (int)ReadDword(L"WindowY", (DWORD)INT_MIN);
    s.alwaysOnTop = ReadDword(L"AlwaysOnTop", 0) != 0;
    s.minimizeToTray = ReadDword(L"MinimizeToTray", 1) != 0;
    s.tightVncPath = ReadString(L"TightVncPath");
    s.expandedAdapter = ReadString(L"ExpandedAdapter");
    const std::wstring hidden = ReadString(L"HiddenAdapters");
    for (size_t pos = 0; pos < hidden.size();) {
        size_t end = hidden.find(L';', pos);
        if (end == std::wstring::npos) end = hidden.size();
        if (end > pos) s.hiddenAdapters.push_back(hidden.substr(pos, end - pos));
        pos = end + 1;
    }
    s.showHidden = ReadDword(L"ShowHidden", 0) != 0;
    s.design = (int)ReadDword(L"Design", 0);

    json::Value hosts, groups;
    if (json::Parse(WideToUtf8(ReadString(L"Hosts")), hosts) && hosts.IsArray()) s.hosts = HostsFromJson(hosts);
    if (json::Parse(WideToUtf8(ReadString(L"HostGroups")), groups) && groups.IsArray())
        s.groups = GroupsFromJson(groups);
    for (auto& h : s.hosts)
        if (h.group < -1 || h.group >= (int)s.groups.size()) h.group = -1;  // dangling reference: top level
    PresetsFromJson(WideToUtf8(ReadString(L"Presets")), s.presets, nullptr);
    return s;
}

void SaveGeneral(const AppSettings& s) {
    if (HKEY key = OpenWriteKey()) {
        WriteDword(key, L"PingCycleMs", s.pingCycleMs);
        WriteDword(key, L"WindowX", (DWORD)s.windowX);
        WriteDword(key, L"WindowY", (DWORD)s.windowY);
        WriteDword(key, L"AlwaysOnTop", s.alwaysOnTop ? 1 : 0);
        WriteDword(key, L"MinimizeToTray", s.minimizeToTray ? 1 : 0);
        WriteDword(key, L"ShowHidden", s.showHidden ? 1 : 0);
        WriteDword(key, L"Design", (DWORD)s.design);
        RegCloseKey(key);
    }
}

void SaveHosts(const std::vector<HostEntry>& hosts, const std::vector<HostGroup>& groups) {
    WriteString(L"Hosts", Utf8ToWide(json::Write(HostsToJson(hosts), false)));
    WriteString(L"HostGroups", Utf8ToWide(json::Write(GroupsToJson(groups), false)));
}

std::string HostsExportJson(const std::vector<HostEntry>& hosts, const std::vector<HostGroup>& groups,
                            bool withGroups) {
    json::Value root = json::Value::Object();
    root.Set("format", "nic-manager-hosts");
    root.Set("version", 1);
    if (withGroups) root.Set("groups", GroupsToJson(groups));
    root.Set("hosts", HostsToJson(hosts, withGroups ? 1 : 2));
    return json::Write(root, true);
}

bool HostsImportJson(const std::string& utf8, std::vector<HostEntry>& hosts, std::vector<HostGroup>& groups,
                     std::wstring* err) {
    json::Value root;
    std::string perr;
    if (!json::Parse(utf8, root, &perr)) {
        if (err) *err = L"Not valid JSON: " + Utf8ToWide(perr);
        return false;
    }
    const json::Value& arr = root.IsArray() ? root : root["hosts"];  // also a bare array of hosts
    if (!arr.IsArray()) {
        if (err) *err = L"No \"hosts\" array found";
        return false;
    }
    groups = root.IsObject() ? GroupsFromJson(root["groups"]) : std::vector<HostGroup>{};
    for (const auto& g : groups)
        if (g.name.empty()) {
            if (err) *err = L"Group without a name";
            return false;
        }
    hosts = HostsFromJson(arr);
    for (auto& h : hosts)
        if (h.group >= (int)groups.size() || h.group < -2) h.group = -1;
    return true;
}

void SavePresets(const std::vector<Preset>& presets) { WriteString(L"Presets", Utf8ToWide(PresetsToJson(presets))); }

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < 16 * 1024 * 1024;
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    }
    CloseHandle(f);
    return ok;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    CloseHandle(f);
    return ok;
}

}  // namespace settings
