#include "netinfo.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <map>
#include <memory>

#include "strutil.h"

namespace netinfo {
namespace {

// {4D36E972-E325-11CE-BFC1-08002BE10318} – network adapter device class.
const GUID kNetClassGuid = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
constexpr wchar_t kNetClassKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\";
constexpr wchar_t kTcpipIfKey[] = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\";
constexpr DWORD NCF_HIDDEN_FLAG = 0x8;

std::wstring RegString(HKEY root, const std::wstring& path, const wchar_t* value) {
    DWORD size = 0;
    if (RegGetValueW(root, path.c_str(), value, RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0)
        return {};
    std::wstring s(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, path.c_str(), value, RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ, nullptr, s.data(), &size) !=
        ERROR_SUCCESS)
        return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

std::wstring SockToString(const SOCKADDR* sa) {
    if (!sa || sa->sa_family != AF_INET) return {};
    wchar_t buf[INET_ADDRSTRLEN] = {};
    InetNtopW(AF_INET, &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, buf, INET_ADDRSTRLEN);
    return buf;
}

std::wstring Upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towupper(c); });
    return s;
}

// Fills IPv4 details for enabled adapters from GetAdaptersAddresses.
std::map<std::wstring, Adapter> QueryIpHelper() {
    std::map<std::wstring, Adapter> result;
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()),
                                  &size);
    }
    if (rc != NO_ERROR) return result;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;
        Adapter ad;
        ad.guid = Upper(Utf8ToWide(a->AdapterName));
        ad.name = a->FriendlyName ? a->FriendlyName : L"";
        ad.device = a->Description ? a->Description : L"";
        ad.enabled = true;
        ad.connected = a->OperStatus == IfOperStatusUp;
        ad.ipv4Enabled = (a->Flags & IP_ADAPTER_IPV4_ENABLED) != 0;
        ad.cfg.dhcp = (a->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
        if (ad.cfg.dhcp) ad.dhcpServer = SockToString(a->Dhcpv4Server.lpSockaddr);

        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            std::wstring s = SockToString(u->Address.lpSockaddr);
            if (!s.empty()) ad.cfg.addresses.push_back({s, (int)u->OnLinkPrefixLength});
        }
        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            std::wstring s = SockToString(g->Address.lpSockaddr);
            if (!s.empty()) ad.cfg.gateways.push_back(s);
        }
        for (auto* d = a->FirstDnsServerAddress; d; d = d->Next) {
            std::wstring s = SockToString(d->Address.lpSockaddr);
            if (!s.empty()) ad.cfg.dns.push_back(s);
        }
        // Statically configured DNS servers live in the "NameServer" value;
        // empty means the servers come from DHCP.
        std::wstring ns = RegString(HKEY_LOCAL_MACHINE, std::wstring(kTcpipIfKey) + ad.guid, L"NameServer");
        ad.cfg.autoDns = ad.cfg.dhcp && Trim(ns).empty();
        result[ad.guid] = std::move(ad);
    }
    return result;
}

}  // namespace

std::vector<Adapter> EnumerateAdapters() {
    std::map<std::wstring, Adapter> ipInfo = QueryIpHelper();
    std::vector<Adapter> list;

    // SetupAPI is the only cheap way to also see disabled adapters, which
    // GetAdaptersAddresses omits.
    HDEVINFO devs = SetupDiGetClassDevsW(&kNetClassGuid, nullptr, nullptr, DIGCF_PRESENT);
    bool setupOk = devs != INVALID_HANDLE_VALUE;
    if (setupOk) {
        SP_DEVINFO_DATA dev{sizeof(dev)};
        for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &dev); ++i) {
            HKEY drv = SetupDiOpenDevRegKey(devs, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
            if (drv == INVALID_HANDLE_VALUE) continue;
            wchar_t guid[64] = {};
            DWORD size = sizeof(guid), characteristics = 0, csize = sizeof(characteristics);
            LONG r = RegGetValueW(drv, nullptr, L"NetCfgInstanceId", RRF_RT_REG_SZ, nullptr, guid, &size);
            RegGetValueW(drv, nullptr, L"Characteristics", RRF_RT_REG_DWORD, nullptr, &characteristics, &csize);
            RegCloseKey(drv);
            if (r != ERROR_SUCCESS || (characteristics & NCF_HIDDEN_FLAG)) continue;

            std::wstring key = Upper(guid);
            ULONG status = 0, problem = 0;
            bool disabled = CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0) == CR_SUCCESS &&
                            (status & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED;

            auto it = ipInfo.find(key);
            if (!disabled) {
                if (it != ipInfo.end()) list.push_back(it->second);
                continue;
            }
            Adapter ad;
            ad.guid = key;
            ad.enabled = false;
            ad.name = RegString(HKEY_LOCAL_MACHINE, std::wstring(kNetClassKey) + guid + L"\\Connection", L"Name");
            if (ad.name.empty()) continue;  // not a network connection
            wchar_t desc[256] = {};
            if (!SetupDiGetDeviceRegistryPropertyW(devs, &dev, SPDRP_FRIENDLYNAME, nullptr, (BYTE*)desc,
                                                   sizeof(desc) - sizeof(wchar_t), nullptr))
                SetupDiGetDeviceRegistryPropertyW(devs, &dev, SPDRP_DEVICEDESC, nullptr, (BYTE*)desc,
                                                  sizeof(desc) - sizeof(wchar_t), nullptr);
            ad.device = desc;
            list.push_back(std::move(ad));
        }
        SetupDiDestroyDeviceInfoList(devs);
    }
    if (!setupOk || list.empty()) {
        for (auto& kv : ipInfo) list.push_back(kv.second);
    }
    std::sort(list.begin(), list.end(), [](const Adapter& a, const Adapter& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return list;
}

std::vector<Adapter> DemoAdapters() {
    std::vector<Adapter> list(4);
    list[0] = {L"{DEMO-0001}", L"Demo LAN", L"Intel(R) Ethernet Connection I219-LM", true, true, true,
               L"192.168.1.1", {true, true, {{L"192.168.1.57", 24}}, {L"192.168.1.1"}, {L"192.168.1.1"}}};
    list[1] = {L"{DEMO-0002}", L"Demo Lab", L"USB 3.0 Gigabit Ethernet Adapter", true, true, true, L"",
               {false, false, {{L"10.10.0.5", 16}, {L"172.16.4.20", 24}}, {L"10.10.0.1"}, {L"10.10.0.1", L"1.1.1.1"}}};
    list[2] = {L"{DEMO-0003}", L"Demo WiFi", L"Intel(R) Wi-Fi 6 AX201 160MHz", true, false, true, L"", {}};
    list[3] = {L"{DEMO-0004}", L"Demo Bluetooth", L"Bluetooth Device (Personal Area Network)", false, false, false,
               L"", {}};
    return list;
}

// ---- monitor ------------------------------------------------------------

namespace {
void CALLBACK OnIfChange(PVOID ctx, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) { SetEvent((HANDLE)ctx); }
void CALLBACK OnAddrChange(PVOID ctx, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) { SetEvent((HANDLE)ctx); }
void CALLBACK OnRouteChange(PVOID ctx, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE) { SetEvent((HANDLE)ctx); }
}  // namespace

void AdapterMonitor::Start(HWND hwnd, UINT msg, bool demo) {
    m_demo = demo;
    m_hwnd = hwnd;
    m_msg = msg;
    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    NotifyIpInterfaceChange(AF_INET, OnIfChange, m_wake, FALSE, &m_notifyIf);
    NotifyUnicastIpAddressChange(AF_INET, OnAddrChange, m_wake, FALSE, &m_notifyAddr);
    NotifyRouteChange2(AF_INET, OnRouteChange, m_wake, FALSE, &m_notifyRoute);
    m_thread = std::thread([this] { Run(); });
}

void AdapterMonitor::Trigger() {
    m_force = true;
    if (m_wake) SetEvent(m_wake);
}

void AdapterMonitor::Stop() {
    if (m_notifyIf) CancelMibChangeNotify2(m_notifyIf);
    if (m_notifyAddr) CancelMibChangeNotify2(m_notifyAddr);
    if (m_notifyRoute) CancelMibChangeNotify2(m_notifyRoute);
    m_notifyIf = m_notifyAddr = m_notifyRoute = nullptr;
    m_stop = true;
    if (m_wake) SetEvent(m_wake);
    if (m_thread.joinable()) m_thread.join();
    if (m_wake) CloseHandle(m_wake);
    m_wake = nullptr;
}

void AdapterMonitor::Run() {
    std::vector<Adapter> last;
    bool first = true;
    while (!m_stop) {
        auto snapshot = std::make_unique<std::vector<Adapter>>(m_demo ? DemoAdapters() : EnumerateAdapters());
        bool force = m_force.exchange(false);
        if (first || force || *snapshot != last) {
            first = false;
            last = *snapshot;
            if (PostMessageW(m_hwnd, m_msg, 0, reinterpret_cast<LPARAM>(snapshot.get()))) snapshot.release();
        }
        if (WaitForSingleObject(m_wake, 2000) == WAIT_OBJECT_0 && !m_stop) {
            // Changes usually arrive in bursts; let them settle.
            Sleep(150);
            ResetEvent(m_wake);
        }
    }
}

}  // namespace netinfo
