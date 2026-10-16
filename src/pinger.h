// One background thread per monitored host, each sending ICMP echo
// requests in its own loop so a slow or unreachable host never delays the
// others or the UI.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ping {

enum class State : uint16_t { Unknown = 0, Up = 1, Down = 2 };

// Results arrive as PostMessage(hwnd, msg, hostId, MAKELPARAM(state, rttMs)).
class Pinger {
public:
    Pinger();
    ~Pinger();
    void Init(HWND hwnd, UINT msg) {
        m_hwnd = hwnd;
        m_msg = msg;
    }
    // Starts/stops workers so exactly the given (id, address) pairs run.
    void Sync(const std::vector<std::pair<uint32_t, std::wstring>>& hosts);
    void SetCycle(DWORD ms);
    void StopAll();

    struct Worker;  // shared between the Pinger and its thread

private:
    HWND m_hwnd = nullptr;
    UINT m_msg = 0;
    std::shared_ptr<std::atomic<DWORD>> m_cycle;
    std::map<uint32_t, std::shared_ptr<Worker>> m_workers;
};

}  // namespace ping
