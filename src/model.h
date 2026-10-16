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
    Ipv4Config cfg;

    bool operator==(const Adapter& o) const {
        return guid == o.guid && name == o.name && device == o.device && enabled == o.enabled &&
               connected == o.connected && ipv4Enabled == o.ipv4Enabled &&
               dhcpServer == o.dhcpServer && cfg == o.cfg;
    }
    bool operator!=(const Adapter& o) const { return !(*this == o); }
};

struct Preset {
    std::wstring name;
    Ipv4Config cfg;
};

// ---- host actions -------------------------------------------------------

struct RdpSettings {
    std::wstring host;
    std::wstring user;
    std::wstring password;
    bool fullscreen = true;  // false = 1024x768 window
};

struct PuttySettings {
    std::wstring host;
    int port = 22;
    std::wstring user;
    std::wstring password;
};

struct VncSettings {
    std::wstring host;
    int port = 5900;
    std::wstring password;
};

using ActionSettings = std::variant<RdpSettings, PuttySettings, VncSettings>;

struct HostAction {
    ActionSettings settings;
};

struct HostEntry {
    std::wstring address;  // IPv4 or host name
    std::vector<HostAction> actions;
};
