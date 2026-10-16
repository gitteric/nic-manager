#include "netcmd.h"

#include <shellapi.h>

#include <algorithm>
#include <vector>

#include "ipaddr.h"
#include "strutil.h"

namespace netcmd {
namespace {

constexpr DWORD kMaxCommandMs = 20000;  // per single netsh/ipconfig call

std::wstring SystemExe(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\" + name;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

// Runs one command, appends it to the job log, and records failure.
bool Step(JobResult& job, const std::wstring& exe, const std::wstring& args, const Deadline& dl,
          bool ignoreFailure = false) {
    DWORD budget = std::min(dl.Remaining(), kMaxCommandMs);
    std::wstring line = L"> " + exe.substr(exe.find_last_of(L'\\') + 1) + L" " + args + L"\r\n";
    if (budget == 0) {
        job.ok = false;
        job.log += line + L"skipped: overall timeout reached\r\n";
        return false;
    }
    ProcessResult r = RunProcess(exe, args, budget);
    std::wstring out = Trim(r.output);
    bool ok = r.started && !r.timedOut && r.exitCode == 0;
    if (!ignoreFailure || r.timedOut) {
        job.log += line;
        if (!out.empty()) job.log += out + L"\r\n";
        if (!r.started) job.log += L"could not start process\r\n";
        if (r.timedOut) job.log += L"timed out after " + FormatMs(budget) + L"\r\n";
        if (!ok) job.ok = false;
    }
    return ok;
}

bool Netsh(JobResult& job, const std::wstring& args, const Deadline& dl, bool ignoreFailure = false) {
    return Step(job, SystemExe(L"netsh.exe"), args, dl, ignoreFailure);
}

}  // namespace

ProcessResult RunProcess(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs) {
    ProcessResult result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmdline = Quote(exe) + L" " + args;
    result.started = CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                    nullptr, nullptr, &si, &pi) != FALSE;
    CloseHandle(writePipe);
    if (!result.started) {
        CloseHandle(readPipe);
        return result;
    }

    std::string raw;
    auto drain = [&] {
        DWORD avail = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char buf[4096];
            DWORD n = 0;
            if (!ReadFile(readPipe, buf, std::min<DWORD>(avail, sizeof(buf)), &n, nullptr) || n == 0) break;
            raw.append(buf, n);
        }
    };
    ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        drain();
        DWORD w = WaitForSingleObject(pi.hProcess, 25);
        if (w == WAIT_OBJECT_0) break;
        if (GetTickCount64() >= end) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            result.timedOut = true;
            break;
        }
    }
    drain();
    GetExitCodeProcess(pi.hProcess, &result.exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);
    // Console tools write in the OEM code page.
    result.output = CodePageToWide(raw, CP_OEMCP);
    return result;
}

JobResult SetAdapterEnabled(const std::wstring& name, bool enable, const Deadline& dl) {
    JobResult job;
    Netsh(job, L"interface set interface name=" + Quote(name) + (enable ? L" admin=enabled" : L" admin=disabled"),
          dl);
    return job;
}

JobResult ApplyIpv4(const std::wstring& name, const Ipv4Config& prev, const Ipv4Config& next, const Deadline& dl) {
    JobResult job;
    const std::wstring n = Quote(name);

    if (!next.dhcp && next.addresses.empty()) {
        job.ok = false;
        job.log = L"A static configuration needs at least one IP address.";
        return job;
    }

    // Gateways beyond the first are persistent default routes; remove the
    // old ones so deleted gateways really disappear.
    if (!prev.dhcp)
        for (const auto& gw : prev.gateways)
            Netsh(job, L"interface ipv4 delete route prefix=0.0.0.0/0 interface=" + n + L" nexthop=" + gw, dl, true);

    if (next.dhcp) {
        Netsh(job, L"interface ipv4 set address name=" + n + L" source=dhcp", dl);
    } else {
        const IpEntry& first = next.addresses.front();
        std::wstring args = L"interface ipv4 set address name=" + n + L" source=static address=" + first.ip +
                            L" mask=" + ip::PrefixToMask(first.prefix);
        args += next.gateways.empty() ? L" gateway=none" : L" gateway=" + next.gateways.front() + L" gwmetric=0";
        if (!Netsh(job, args, dl)) return job;
        for (size_t i = 1; i < next.addresses.size(); ++i)
            Netsh(job, L"interface ipv4 add address name=" + n + L" address=" + next.addresses[i].ip + L" mask=" +
                           ip::PrefixToMask(next.addresses[i].prefix), dl);
        for (size_t i = 1; i < next.gateways.size(); ++i)
            Netsh(job, L"interface ipv4 add route prefix=0.0.0.0/0 interface=" + n + L" nexthop=" +
                           next.gateways[i] + L" store=persistent", dl);
    }

    if (next.dhcp && next.autoDns) {
        Netsh(job, L"interface ipv4 set dnsservers name=" + n + L" source=dhcp", dl);
    } else if (next.dns.empty()) {
        Netsh(job, L"interface ipv4 set dnsservers name=" + n + L" source=static address=none", dl);
    } else {
        Netsh(job, L"interface ipv4 set dnsservers name=" + n + L" source=static address=" + next.dns[0] +
                       L" register=primary validate=no", dl);
        for (size_t i = 1; i < next.dns.size() && i < 2; ++i)
            Netsh(job, L"interface ipv4 add dnsservers name=" + n + L" address=" + next.dns[i] + L" index=" +
                           std::to_wstring(i + 1) + L" validate=no", dl);
    }
    return job;
}

JobResult FlushDns(const Deadline& dl) {
    JobResult job;
    Step(job, SystemExe(L"ipconfig.exe"), L"/flushdns", dl);
    return job;
}

JobResult RenewDhcp(const std::wstring& name, const Deadline& dl) {
    JobResult job;
    Step(job, SystemExe(L"ipconfig.exe"), name.empty() ? L"/renew" : L"/renew " + Quote(name), dl);
    return job;
}

void OpenNetworkConnections() {
    ShellExecuteW(nullptr, L"open", SystemExe(L"control.exe").c_str(), L"ncpa.cpl", nullptr, SW_SHOWNORMAL);
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

}  // namespace netcmd
