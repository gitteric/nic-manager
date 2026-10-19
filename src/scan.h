// Subnet scans without external tools: ARP requests (SendARP), ICMP echo or
// a TCP connect per address, many addresses at a time. Hosts found get a
// MAC address (ARP table), a vendor (embedded IEEE OUI list) and a reverse
// DNS / NetBIOS name. Scan() blocks and is meant for a worker thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "netcmd.h"

namespace scan {

enum class Kind { Arp, Ping, Tcp };

struct Host {
    uint32_t ip = 0;  // host byte order
    std::wstring mac, vendor, name;
};

struct ScanResult {
    std::vector<Host> hosts;  // answered (Tcp: port open), ascending by address
    int done = 0, total = 0;  // addresses probed / in the subnet
    bool cancelled = false, timedOut = false;
};

constexpr int kMinPrefix = 24;  // larger subnets are not scanned
constexpr int kParallel = 64;   // probes at a time
constexpr DWORD kScanTimeoutMs = 180000;

const wchar_t* KindName(Kind kind);  // "ARP", "ping", "TCP"

// Addresses probed for network/prefix: all hosts, without network and
// broadcast address below /31.
std::vector<uint32_t> Targets(uint32_t network, int prefix);

// Vendor for a MAC address ("AA:BB:CC:..."), empty if unknown.
std::wstring Vendor(const std::wstring& mac);

// Scans network/prefix (host byte order). `source` is the adapter's own
// address in that subnet; ARP requests go out from it. `progress(done,
// total)` is called from worker threads. Cancel and timeout end the scan
// with what was found so far.
netcmd::JobResult Scan(Kind kind, uint32_t network, int prefix, uint32_t source, int port,
                       const netcmd::Deadline& dl, const std::atomic<bool>& cancel,
                       const std::function<void(int, int)>& progress, ScanResult& out);

}  // namespace scan
