// Network adapter enumeration (read-only, no elevation needed) and a
// background monitor that posts fresh snapshots to the UI thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

#include "model.h"

namespace netinfo {

// Enumerates physical/visible network adapters including disabled ones.
// Runs in a few milliseconds; still, call it off the UI thread.
std::vector<Adapter> EnumerateAdapters();

// Fixed sample data for --demo (UI work without touching real adapters).
std::vector<Adapter> DemoAdapters();

// Watches for IP interface/address/route changes and polls every 2 s (to
// catch enable/disable and media state). Posts `msg` with
// lParam = new std::vector<Adapter>* (receiver takes ownership) whenever the
// snapshot differs from the previous one, or on Trigger().
class AdapterMonitor {
public:
    ~AdapterMonitor() { Stop(); }
    void Start(HWND hwnd, UINT msg, bool demo = false);
    void Trigger();  // force a refresh + post
    void Stop();  // waits at most ~2 s for the thread

private:
    void Run();

    HWND m_hwnd = nullptr;
    UINT m_msg = 0;
    HANDLE m_wake = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_force{true};
    std::atomic<bool> m_done{false};  // Run() returned
    bool m_demo = false;
    std::thread m_thread;
    HANDLE m_notifyIf = nullptr, m_notifyAddr = nullptr, m_notifyRoute = nullptr;
};

}  // namespace netinfo
