// Commands that change system state. All of them run netsh/ipconfig as
// child processes with a hard timeout and are meant to be called from a
// worker thread. Changing adapter settings requires elevation.
#pragma once

#include <windows.h>

#include <string>

#include "model.h"

namespace netcmd {

class Deadline {
public:
    static Deadline In(DWORD ms) { return Deadline(GetTickCount64() + ms); }
    DWORD Remaining() const {
        ULONGLONG now = GetTickCount64();
        return now >= m_end ? 0 : (DWORD)(m_end - now);
    }
    bool Expired() const { return Remaining() == 0; }

private:
    explicit Deadline(ULONGLONG end) : m_end(end) {}
    ULONGLONG m_end;
};

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    std::wstring output;  // stdout + stderr
};

// Runs a console program without a window and captures its output.
ProcessResult RunProcess(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs);

struct JobResult {
    bool ok = true;
    std::wstring log;  // commands run and their output, for error display
};

JobResult SetAdapterEnabled(const std::wstring& name, bool enable, const Deadline& dl);
// Applies `next` as a whole. `prev` is the current state (used to remove
// gateways that were added as extra default routes).
JobResult ApplyIpv4(const std::wstring& name, const Ipv4Config& prev, const Ipv4Config& next, const Deadline& dl);
JobResult FlushDns(const Deadline& dl);
JobResult RenewDhcp(const std::wstring& name /* empty = all adapters */, const Deadline& dl);

void OpenNetworkConnections();  // classic "Network Connections" (ncpa.cpl)
bool IsElevated();

}  // namespace netcmd
