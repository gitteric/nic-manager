#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include "scan.h"

#include <algorithm>
#include <mutex>
#include <thread>

#include "ipaddr.h"
#include "resource.h"
#include "strutil.h"

namespace scan {
namespace {

constexpr DWORD kEchoTimeoutMs = 1000;
constexpr DWORD kConnectTimeoutMs = 1000;
constexpr int kNameParallel = 16;

std::wstring FormatMac(const BYTE* b, ULONG len) {
    if (len != 6) return {};
    bool zero = std::all_of(b, b + 6, [](BYTE x) { return x == 0; });
    if (zero) return {};
    wchar_t s[18];
    swprintf_s(s, L"%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
    return s;
}

// res/oui.bin, see tools/make-oui.py: "OUI1", u32 entries, u32 names,
// entries of (3-byte OUI, u16 name index), then the NUL-terminated names.
struct OuiTable {
    const BYTE* entries = nullptr;
    uint32_t count = 0;
    std::vector<const char*> names;

    OuiTable() {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_OUI), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
        HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
        const BYTE* d = mem ? static_cast<const BYTE*>(LockResource(mem)) : nullptr;
        DWORD size = res ? SizeofResource(nullptr, res) : 0;
        if (!d || size < 12 || memcmp(d, "OUI1", 4) != 0) return;
        uint32_t n, m;
        memcpy(&n, d + 4, 4);
        memcpy(&m, d + 8, 4);
        if (12ull + 5ull * n > size) return;
        const char* p = reinterpret_cast<const char*>(d + 12 + 5 * n);
        const char* end = reinterpret_cast<const char*>(d + size);
        for (uint32_t i = 0; i < m && p < end; ++i) {
            names.push_back(p);
            p += strnlen(p, end - p) + 1;
        }
        if (names.size() == m) {
            entries = d + 12;
            count = n;
        }
    }

    const char* Find(uint32_t oui) const {
        uint32_t lo = 0, hi = count;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            const BYTE* e = entries + 5 * mid;
            uint32_t key = (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
            if (key == oui) {
                uint16_t idx = (uint16_t)(e[3] | e[4] << 8);
                return idx < names.size() ? names[idx] : nullptr;
            }
            if (key < oui) lo = mid + 1;
            else hi = mid;
        }
        return nullptr;
    }
};

const OuiTable& Oui() {
    static const OuiTable table;
    return table;
}

in_addr ToInAddr(uint32_t ip) {
    in_addr a{};
    a.S_un.S_addr = htonl(ip);
    return a;
}

bool ProbeArp(uint32_t ip, uint32_t source, std::wstring& mac) {
    ULONG buf[2] = {};
    ULONG len = 6;
    if (SendARP(htonl(ip), htonl(source), buf, &len) != NO_ERROR) return false;
    mac = FormatMac(reinterpret_cast<const BYTE*>(buf), len);
    return !mac.empty();
}

bool ProbePing(HANDLE icmp, uint32_t ip) {
    char payload[32] = "NicManager";
    alignas(8) BYTE reply[sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8];
    if (IcmpSendEcho(icmp, htonl(ip), payload, sizeof(payload), nullptr, reply, sizeof(reply), kEchoTimeoutMs) == 0)
        return false;
    return reinterpret_cast<const ICMP_ECHO_REPLY*>(reply)->Status == IP_SUCCESS;
}

bool ProbeTcp(uint32_t ip, int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    sa.sin_addr = ToInAddr(ip);
    bool open = false;
    if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        open = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set w, e;
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(s, &w);
        FD_SET(s, &e);
        timeval tv{(long)(kConnectTimeoutMs / 1000), (long)(kConnectTimeoutMs % 1000) * 1000};
        if (select(0, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w)) {
            int err = 0, len = sizeof(err);
            open = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0 && err == 0;
        }
    }
    closesocket(s);
    return open;
}

// MAC addresses Windows learned while probing (ping and TCP scans).
void FillMacsFromArpTable(std::vector<Host>& hosts) {
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR || !table) return;
    for (auto& h : hosts) {
        if (!h.mac.empty()) continue;
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IPNET_ROW2& r = table->Table[i];
            if (r.Address.Ipv4.sin_addr.S_un.S_addr != htonl(h.ip) || r.State == NlnsUnreachable) continue;
            h.mac = FormatMac(r.PhysicalAddress, r.PhysicalAddressLength);
            if (!h.mac.empty()) break;
        }
    }
    FreeMibTable(table);
}

// Reverse DNS; Windows also asks NetBIOS and LLMNR for local names.
std::wstring ReverseName(uint32_t ip) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr = ToInAddr(ip);
    wchar_t host[NI_MAXHOST] = {};
    if (GetNameInfoW(reinterpret_cast<sockaddr*>(&sa), sizeof(sa), host, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) != 0)
        return {};
    return host;
}

// Runs fn(i) for i in [0, n) on up to `parallel` threads until stop() says so.
template <typename Fn, typename Stop>
void ForEachParallel(size_t n, int parallel, Fn fn, Stop stop) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> workers;
    for (int w = 0; w < std::min<int>(parallel, (int)n); ++w)
        workers.emplace_back([&] {
            for (;;) {
                if (stop()) return;
                size_t i = next++;
                if (i >= n) return;
                fn(i);
            }
        });
    for (auto& t : workers) t.join();
}

}  // namespace

const wchar_t* KindName(Kind kind) {
    switch (kind) {
    case Kind::Arp: return L"ARP";
    case Kind::Ping: return L"ping";
    default: return L"TCP";
    }
}

std::vector<uint32_t> Targets(uint32_t network, int prefix) {
    std::vector<uint32_t> t;
    uint64_t count = prefix >= 32 ? 1 : (1ull << (32 - prefix));
    uint64_t first = 0, last = count - 1;
    if (prefix < 31) ++first, --last;  // skip network and broadcast address
    for (uint64_t i = first; i <= last; ++i) t.push_back(network + (uint32_t)i);
    return t;
}

std::wstring Vendor(const std::wstring& mac) {
    unsigned b[3];
    if (mac.size() < 8 || swscanf_s(mac.c_str(), L"%2x:%2x:%2x", &b[0], &b[1], &b[2]) != 3) return {};
    if (b[0] & 0x02) return L"(random MAC)";  // locally administered, e.g. phone privacy addresses
    const char* name = Oui().Find(b[0] << 16 | b[1] << 8 | b[2]);
    return name ? Utf8ToWide(name) : std::wstring();
}

netcmd::JobResult Scan(Kind kind, uint32_t network, int prefix, uint32_t source, int port,
                       const netcmd::Deadline& dl, const std::atomic<bool>& cancel,
                       const std::function<void(int, int)>& progress, ScanResult& out) {
    netcmd::JobResult job;
    out = ScanResult{};
    const std::vector<uint32_t> targets = Targets(network, prefix);
    out.total = (int)targets.size();
    job.log = L"Scan of " + ip::FormatIpv4(network) + L"/" + std::to_wstring(prefix) + L": " + KindName(kind) +
              (kind == Kind::Tcp ? L" port " + std::to_wstring(port) : L"") + L", from " + ip::FormatIpv4(source);

    std::atomic<int> done{0};
    std::mutex mu;
    auto stop = [&] {
        if (cancel) return true;
        if (dl.Expired()) {
            std::lock_guard<std::mutex> lock(mu);
            out.timedOut = true;
            return true;
        }
        return false;
    };
    // One ICMP handle per thread; created lazily, closed with the thread.
    struct IcmpHandle {
        HANDLE h = IcmpCreateFile();
        ~IcmpHandle() {
            if (h != INVALID_HANDLE_VALUE) IcmpCloseHandle(h);
        }
    };
    ForEachParallel(
        targets.size(), kParallel,
        [&](size_t i) {
            const uint32_t ip = targets[i];
            Host h;
            h.ip = ip;
            bool found = false;
            if (kind == Kind::Arp) {
                found = ProbeArp(ip, source, h.mac);
            } else if (kind == Kind::Ping) {
                thread_local IcmpHandle icmp;
                found = icmp.h != INVALID_HANDLE_VALUE && ProbePing(icmp.h, ip);
            } else {
                found = ProbeTcp(ip, port);
            }
            if (cancel) return;  // this probe may have been cut short: not counted
            if (found) {
                std::lock_guard<std::mutex> lock(mu);
                out.hosts.push_back(std::move(h));
            }
            int n = ++done;
            if (progress) progress(n, out.total);
        },
        stop);
    out.done = done;
    out.cancelled = cancel && out.done < out.total;

    std::sort(out.hosts.begin(), out.hosts.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
    if (kind != Kind::Arp) FillMacsFromArpTable(out.hosts);
    for (auto& h : out.hosts) h.vendor = Vendor(h.mac);
    // Names last: a lookup can take a few seconds, the probes are done by then.
    ForEachParallel(
        out.hosts.size(), kNameParallel, [&](size_t i) { out.hosts[i].name = ReverseName(out.hosts[i].ip); },
        [&] { return cancel.load(); });
    return job;
}

}  // namespace scan
