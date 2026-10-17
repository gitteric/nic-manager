#include "nmap.h"

#include <algorithm>
#include <mutex>
#include <thread>

#include "ipaddr.h"
#include "strutil.h"

namespace nmap {
namespace {

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring DecodeEntities(const std::wstring& s) {
    static const struct { const wchar_t* from; wchar_t to; } kMap[] = {
        {L"&amp;", L'&'}, {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&quot;", L'"'}, {L"&apos;", L'\''}};
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        bool hit = false;
        if (s[i] == L'&')
            for (const auto& m : kMap)
                if (s.compare(i, wcslen(m.from), m.from) == 0) {
                    out += m.to;
                    i += wcslen(m.from) - 1;
                    hit = true;
                    break;
                }
        if (!hit) out += s[i];
    }
    return out;
}

// Value of attribute `name` in one XML start tag.
std::wstring Attr(const std::wstring& tag, const wchar_t* name) {
    std::wstring key = std::wstring(L" ") + name + L"=\"";
    size_t p = tag.find(key);
    if (p == std::wstring::npos) return {};
    p += key.size();
    size_t e = tag.find(L'"', p);
    return e == std::wstring::npos ? std::wstring() : DecodeEntities(tag.substr(p, e - p));
}

// All start tags `<name ...>` within [from, to).
std::vector<std::wstring> Tags(const std::wstring& xml, const wchar_t* name, size_t from, size_t to) {
    std::vector<std::wstring> tags;
    std::wstring open = std::wstring(L"<") + name + L" ";
    for (size_t p = xml.find(open, from); p != std::wstring::npos && p < to; p = xml.find(open, p + 1)) {
        size_t e = xml.find(L'>', p);
        if (e == std::wstring::npos || e > to) break;
        tags.push_back(xml.substr(p, e - p + 1));
    }
    return tags;
}

void ParseHosts(const std::wstring& xml, Kind kind, int port, std::vector<Host>& out) {
    // "<host>" in ping scans, "<host starttime=...>" in port scans; not "<hostnames>".
    auto next = [&](size_t from) {
        for (size_t p = xml.find(L"<host", from); p != std::wstring::npos; p = xml.find(L"<host", p + 1))
            if (p + 5 < xml.size() && (xml[p + 5] == L'>' || xml[p + 5] == L' ')) return p;
        return std::wstring::npos;
    };
    for (size_t p = next(0); p != std::wstring::npos; p = next(p + 1)) {
        size_t end = xml.find(L"</host>", p);
        if (end == std::wstring::npos) break;
        auto status = Tags(xml, L"status", p, end);
        if (status.empty() || Attr(status[0], L"state") != L"up") continue;
        Host h;
        bool hasIp = false;
        for (const auto& a : Tags(xml, L"address", p, end)) {
            std::wstring type = Attr(a, L"addrtype");
            if (type == L"ipv4") hasIp = ip::ParseIpv4(Attr(a, L"addr"), &h.ip);
            else if (type == L"mac") {
                h.mac = Attr(a, L"addr");
                h.vendor = Attr(a, L"vendor");
            }
        }
        if (!hasIp) continue;
        auto names = Tags(xml, L"hostname", p, end);
        if (!names.empty()) h.name = Attr(names[0], L"name");
        if (kind == Kind::Tcp) {
            bool open = false;
            for (const auto& pt : Tags(xml, L"port", p, end)) {
                if (Attr(pt, L"portid") != std::to_wstring(port)) continue;
                size_t at = xml.find(pt, p);
                auto st = Tags(xml, L"state", at, end);
                open = !st.empty() && Attr(st[0], L"state") == L"open";
            }
            if (!open) continue;
        }
        out.push_back(std::move(h));
        p = end;
    }
    std::sort(out.begin(), out.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
    out.erase(std::unique(out.begin(), out.end(), [](const Host& a, const Host& b) { return a.ip == b.ip; }),
              out.end());
}

// stderr lines (everything that is not part of the XML document).
std::vector<std::wstring> Diagnostics(const std::wstring& output) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= output.size()) {
        size_t e = output.find_first_of(L"\r\n", pos);
        std::wstring ln = Trim(output.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos));
        if (!ln.empty() && ln[0] != L'<') lines.push_back(ln);
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
    return lines;
}

// Raw packet scans (ARP, ICMP) need Npcap; without it nmap quietly falls back
// to TCP connect probes, so these warnings count as failures.
bool IsRawFailure(const std::wstring& line) {
    std::wstring l = Lower(line);
    return l.find(L"npcap") != std::wstring::npos || l.find(L"pcap") != std::wstring::npos ||
           l.find(L"failed to open device") != std::wstring::npos || l.find(L"privileged") != std::wstring::npos ||
           l.find(L"requires root") != std::wstring::npos || l.find(L"raw socket") != std::wstring::npos;
}

bool IsError(const std::wstring& line) {
    std::wstring l = Lower(line);
    return l.rfind(L"error", 0) == 0 || l.find(L"quitting") != std::wstring::npos ||
           l.find(L"failed") != std::wstring::npos || l.find(L"unable") != std::wstring::npos;
}

}  // namespace

std::wstring Find() {
    wchar_t buf[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"nmap.exe", nullptr, MAX_PATH, buf, nullptr) > 0 && FileExists(buf)) return buf;
    for (const wchar_t* var : {L"ProgramFiles(x86)", L"ProgramW6432", L"ProgramFiles"}) {
        wchar_t dir[MAX_PATH] = {};
        if (GetEnvironmentVariableW(var, dir, MAX_PATH) == 0) continue;
        std::wstring p = std::wstring(dir) + L"\\Nmap\\nmap.exe";
        if (FileExists(p)) return p;
    }
    return {};
}

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

namespace {

struct Probe {
    bool fatal = false;  // nmap itself failed; stop the whole scan
    std::wstring error, log;
    std::vector<Host> found;
};

Probe ProbeOne(const std::wstring& exe, Kind kind, uint32_t addr, int port, DWORD budget,
               const std::atomic<bool>& cancel) {
    Probe pr;
    std::wstring args;
    switch (kind) {
    case Kind::Arp: args = L"-sn -PR"; break;
    case Kind::Ping: args = L"-sn -PE --disable-arp-ping"; break;
    case Kind::Tcp: args = L"-sT -Pn -p " + std::to_wstring(port); break;
    }
    args += L" --max-retries 0 -T4 -oX - " + ip::FormatIpv4(addr);
    netcmd::ProcessResult r = netcmd::RunProcess(exe, args, budget, &cancel);
    auto diag = Diagnostics(r.output);
    pr.log = L"> nmap " + args + L"\r\n";
    for (const auto& d : diag) pr.log += d + L"\r\n";
    if (!r.started) {
        pr.fatal = true;
        pr.error = L"Could not start " + exe + L".";
        return pr;
    }
    if (r.timedOut) {  // cancelled, or this address took too long: not an nmap error
        pr.log += cancel ? L"stopped\r\n" : L"timed out\r\n";
        return pr;
    }
    if (kind != Kind::Tcp) {
        auto raw = std::find_if(diag.begin(), diag.end(), IsRawFailure);
        if (raw != diag.end()) {
            pr.fatal = true;
            pr.error = std::wstring(kind == Kind::Arp ? L"ARP" : L"Ping") +
                       L" scans need Npcap (npcap.com) and administrator rights; a TCP port check works without them.";
            return pr;
        }
    }
    // nmap reports fatal errors in <finished errormsg="..."> as well.
    auto finished = Tags(r.output, L"finished", 0, r.output.size());
    std::wstring errmsg = finished.empty() ? std::wstring() : Attr(finished[0], L"errormsg");
    bool completed = !finished.empty() && Attr(finished[0], L"exit") != L"error";
    if (r.exitCode != 0 || !completed) {
        pr.fatal = true;
        auto err = std::find_if(diag.begin(), diag.end(), IsError);
        if (!errmsg.empty()) pr.error = L"nmap: " + Trim(errmsg);
        else if (err != diag.end()) pr.error = L"nmap: " + *err;
        else if (!diag.empty()) pr.error = L"nmap: " + diag.front();
        else pr.error = L"nmap failed with exit code " + std::to_wstring(r.exitCode) + L".";
        return pr;
    }
    ParseHosts(r.output, kind, port, pr.found);
    return pr;
}

}  // namespace

netcmd::JobResult Scan(const std::wstring& exe, Kind kind, uint32_t network, int prefix, int port,
                       const netcmd::Deadline& dl, const std::atomic<bool>& cancel,
                       const std::function<void(int, int)>& progress, ScanResult& out) {
    netcmd::JobResult job;
    out = ScanResult{};
    if (exe.empty() || !FileExists(exe)) {
        job.ok = false;
        job.error = exe.empty() ? L"nmap.exe was not found. Select it in the tray menu."
                                : L"nmap.exe was not found at " + exe + L".";
        return job;
    }
    const std::vector<uint32_t> targets = Targets(network, prefix);
    out.total = (int)targets.size();

    constexpr DWORD kProbeMs = 15000;  // one address, including name lookup
    std::atomic<size_t> next{0};
    std::atomic<int> done{0};
    std::atomic<bool> stop{false};  // fatal error, cancel or deadline
    std::mutex mu;
    std::vector<std::thread> workers;
    for (int w = 0; w < std::min<int>(kParallel, out.total); ++w)
        workers.emplace_back([&] {
            for (;;) {
                if (cancel) stop = true;
                if (stop) return;
                size_t i = next++;
                if (i >= targets.size()) return;
                DWORD budget = std::min(dl.Remaining(), kProbeMs);
                if (budget == 0) {
                    std::lock_guard<std::mutex> lock(mu);
                    out.timedOut = true;
                    stop = true;
                    return;
                }
                Probe pr = ProbeOne(exe, kind, targets[i], port, budget, cancel);
                {
                    std::lock_guard<std::mutex> lock(mu);
                    if (pr.fatal) {
                        if (job.ok) {
                            job.ok = false;
                            job.error = pr.error;
                            job.log += pr.log;
                        }
                        stop = true;
                        return;
                    }
                    if (cancel) return;  // this probe was cut short: not counted
                    out.hosts.insert(out.hosts.end(), pr.found.begin(), pr.found.end());
                    if (job.log.empty()) job.log = pr.log;  // the first command, as an example
                }
                int n = ++done;
                if (progress) progress(n, out.total);
            }
        });
    for (auto& t : workers) t.join();

    out.done = done;
    out.cancelled = cancel && out.done < out.total;
    std::sort(out.hosts.begin(), out.hosts.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
    return job;
}

}  // namespace nmap
