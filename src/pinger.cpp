#include "pinger.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <algorithm>
#include <thread>

namespace ping {

constexpr DWORD kEchoTimeoutMs = 1000;
constexpr ULONGLONG kResolveTtlMs = 30000;

struct Pinger::Worker {
    std::wstring address;
    std::atomic<bool> stop{false};
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Worker() { CloseHandle(wake); }
};

namespace {

bool Resolve(const std::wstring& host, IN_ADDR& out) {
    if (InetPtonW(AF_INET, host.c_str(), &out) == 1) return true;
    ADDRINFOW hints{};
    hints.ai_family = AF_INET;
    ADDRINFOW* res = nullptr;
    if (GetAddrInfoW(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    FreeAddrInfoW(res);
    return true;
}

void WorkerLoop(std::shared_ptr<Pinger::Worker> w, std::shared_ptr<std::atomic<DWORD>> cycle, HWND hwnd,
                UINT msg, uint32_t id) {
    HANDLE icmp = IcmpCreateFile();
    IN_ADDR addr{};
    bool resolved = false;
    ULONGLONG resolvedAt = 0;
    char payload[32] = "nic-manager-ping";
    unsigned char reply[sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 64];

    while (!w->stop) {
        State state = State::Down;
        DWORD rtt = 0;
        ULONGLONG now = GetTickCount64();
        if (!resolved || now - resolvedAt > kResolveTtlMs) {
            resolved = Resolve(w->address, addr);
            resolvedAt = now;
        }
        if (resolved && icmp != INVALID_HANDLE_VALUE) {
            DWORD n = IcmpSendEcho(icmp, addr.S_un.S_addr, payload, sizeof(payload), nullptr, reply, sizeof(reply),
                                   kEchoTimeoutMs);
            auto* r = reinterpret_cast<ICMP_ECHO_REPLY*>(reply);
            if (n > 0 && r->Status == IP_SUCCESS) {
                state = State::Up;
                rtt = r->RoundTripTime;
            }
        }
        if (w->stop) break;
        PostMessageW(hwnd, msg, id, MAKELPARAM((WORD)state, (WORD)std::min<DWORD>(rtt, 0xFFFF)));
        WaitForSingleObject(w->wake, cycle->load());
    }
    if (icmp != INVALID_HANDLE_VALUE) IcmpCloseHandle(icmp);
}

}  // namespace

Pinger::Pinger() : m_cycle(std::make_shared<std::atomic<DWORD>>(1000)) {}
Pinger::~Pinger() { StopAll(); }

void Pinger::SetCycle(DWORD ms) {
    m_cycle->store(ms);
    for (auto& kv : m_workers) SetEvent(kv.second->wake);  // apply a shorter cycle right away
}

void Pinger::Sync(const std::vector<std::pair<uint32_t, std::wstring>>& hosts) {
    // Stop workers that are gone or whose address changed.
    for (auto it = m_workers.begin(); it != m_workers.end();) {
        auto want = std::find_if(hosts.begin(), hosts.end(), [&](const auto& h) { return h.first == it->first; });
        if (want == hosts.end() || want->second != it->second->address) {
            it->second->stop = true;
            SetEvent(it->second->wake);
            it = m_workers.erase(it);  // thread keeps its own reference and exits on its own
        } else {
            ++it;
        }
    }
    for (const auto& h : hosts) {
        if (m_workers.count(h.first)) continue;
        auto w = std::make_shared<Worker>();
        w->address = h.second;
        m_workers[h.first] = w;
        std::thread(WorkerLoop, w, m_cycle, m_hwnd, m_msg, h.first).detach();
    }
}

void Pinger::StopAll() {
    for (auto& kv : m_workers) {
        kv.second->stop = true;
        SetEvent(kv.second->wake);
    }
    m_workers.clear();
}

}  // namespace ping
