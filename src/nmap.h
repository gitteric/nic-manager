// Subnet scans with nmap (https://nmap.org): ARP, ICMP echo or a TCP port.
// Every address gets its own short nmap run (one probe, no retries), several
// at a time, so progress is visible and a scan can stop with partial results.
// Scan() blocks and is meant for a worker thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "netcmd.h"

namespace nmap {

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

constexpr int kMinPrefix = 24;           // larger subnets are not scanned
constexpr int kParallel = 16;            // nmap processes at a time
constexpr DWORD kScanTimeoutMs = 180000;

// nmap.exe on PATH or in the default install folders; empty if not found.
std::wstring Find();

const wchar_t* KindName(Kind kind);  // "ARP", "ping", "TCP"

// Addresses probed for network/prefix: all hosts, without network and
// broadcast address below /31.
std::vector<uint32_t> Targets(uint32_t network, int prefix);

// Scans network/prefix (host byte order). `progress(done, total)` is called
// from worker threads. Fails only on nmap errors (e.g. missing Npcap);
// cancel and timeout end the scan with what was found so far.
netcmd::JobResult Scan(const std::wstring& exe, Kind kind, uint32_t network, int prefix, int port,
                       const netcmd::Deadline& dl, const std::atomic<bool>& cancel,
                       const std::function<void(int, int)>& progress, ScanResult& out);

}  // namespace nmap
