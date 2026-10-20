// Plain data types shared by the whole app. No Win32 here on purpose.
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct IpEntry {
    std::wstring ip;  // dotted IPv4, e.g. 192.168.1.10
    int prefix = 24;  // on-link prefix length (0..32)

    bool operator==(const IpEntry& o) const { return ip == o.ip && prefix == o.prefix; }
    bool operator!=(const IpEntry& o) const { return !(*this == o); }
};

struct Ipv4Config {
    bool dhcp = true;
    bool autoDns = true;  // only meaningful while dhcp == true
    std::vector<IpEntry> addresses;
    std::vector<std::wstring> gateways;
    std::vector<std::wstring> dns;  // at most 2 are shown/edited

    bool operator==(const Ipv4Config& o) const {
        return dhcp == o.dhcp && autoDns == o.autoDns && addresses == o.addresses &&
               gateways == o.gateways && dns == o.dns;
    }
    bool operator!=(const Ipv4Config& o) const { return !(*this == o); }
};

struct Adapter {
    std::wstring guid;     // {XXXXXXXX-...}, stable key
    std::wstring name;     // connection name ("Ethernet 2"), used by netsh
    std::wstring device;   // device description ("Intel(R) Ethernet ...")
    bool enabled = true;
    bool connected = false;
    bool ipv4Enabled = false;
    std::wstring dhcpServer;
    std::wstring linkLocal;  // 169.254.x.x autoconfiguration address, display only
    Ipv4Config cfg;
    bool wireless = false;  // IEEE 802.11 adapter

    bool operator==(const Adapter& o) const {
        return guid == o.guid && name == o.name && device == o.device && enabled == o.enabled &&
               connected == o.connected && ipv4Enabled == o.ipv4Enabled &&
               dhcpServer == o.dhcpServer && linkLocal == o.linkLocal && cfg == o.cfg &&
               wireless == o.wireless;
    }
    bool operator!=(const Adapter& o) const { return !(*this == o); }
};

struct Preset {
    std::wstring name;
    Ipv4Config cfg;
};

// ---- host actions -------------------------------------------------------

// Actions connect to their host entry's address; only the browser has its
// own target, a URL or host[:port] preset to that address.
struct RdpSettings {
    std::wstring user;
    std::wstring password;
    bool fullscreen = true;  // false = 1024x768 window
};

struct SshSettings {
    int port = 22;
    std::wstring user;
    std::wstring password;
};

struct VncSettings {
    int port = 5900;
    std::wstring password;
};

struct BrowserSettings {
    std::wstring url;  // http(s)://... or host[:port][/path]; http:// is assumed
};

using ActionSettings = std::variant<RdpSettings, SshSettings, VncSettings, BrowserSettings>;

struct HostAction {
    ActionSettings settings;
};

struct HostEntry {
    std::wstring name;     // display name, defaults to the address
    std::wstring address;  // IPv4 or host name
    bool ping = true;      // monitored by the pinger; off leaves the marker grey
    int group = -1;        // index into the host groups, -1: top level
    std::vector<HostAction> actions;
};

// Folder in the host list. Its ping switch is ANDed with each member's.
struct HostGroup {
    std::wstring name;
    bool ping = true;
    bool expanded = true;
};
