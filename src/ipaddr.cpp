#include "ipaddr.h"

#include <cwctype>

#include "strutil.h"

namespace ip {

bool ParseIpv4(const std::wstring& s, uint32_t* out) {
    uint32_t value = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        if (i >= s.size() || !iswdigit(s[i])) return false;
        int octet = 0, digits = 0;
        while (i < s.size() && iswdigit(s[i])) {
            octet = octet * 10 + (s[i] - L'0');
            if (++digits > 3 || octet > 255) return false;
            ++i;
        }
        value = (value << 8) | (uint32_t)octet;
        ++parts;
        if (parts < 4) {
            if (i >= s.size() || s[i] != L'.') return false;
            ++i;
        }
    }
    if (i != s.size()) return false;
    if (out) *out = value;
    return true;
}

std::wstring FormatIpv4(uint32_t v) {
    return std::to_wstring((v >> 24) & 0xFF) + L"." + std::to_wstring((v >> 16) & 0xFF) + L"." +
           std::to_wstring((v >> 8) & 0xFF) + L"." + std::to_wstring(v & 0xFF);
}

bool IsValidHostname(const std::wstring& s) {
    if (s.empty() || s.size() > 253) return false;
    bool allNumeric = true;
    size_t start = 0;
    while (start <= s.size()) {
        size_t dot = s.find(L'.', start);
        size_t end = dot == std::wstring::npos ? s.size() : dot;
        size_t len = end - start;
        if (len == 0 || len > 63) return false;
        if (s[start] == L'-' || s[end - 1] == L'-') return false;
        for (size_t k = start; k < end; ++k) {
            wchar_t c = s[k];
            bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                      c == L'-';
            if (!ok) return false;
            if (!(c >= L'0' && c <= L'9')) allNumeric = false;
        }
        if (dot == std::wstring::npos) break;
        start = dot + 1;
    }
    // "10.0.0" or "300.1.1.1" are malformed addresses, not host names.
    return !allNumeric;
}

bool IsLinkLocal(const std::wstring& s) {
    uint32_t v;
    return ParseIpv4(s, &v) && (v >> 16) == 0xA9FE;
}

bool IsValidHost(const std::wstring& s) { return ParseIpv4(s) || IsValidHostname(s); }

std::wstring PrefixToMask(int prefix) {
    if (prefix <= 0) return L"0.0.0.0";
    if (prefix > 32) prefix = 32;
    uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1u);
    return FormatIpv4(mask);
}

bool MaskToPrefix(const std::wstring& mask, int* prefix) {
    uint32_t m;
    if (!ParseIpv4(mask, &m)) return false;
    // Must be contiguous ones followed by zeros.
    uint32_t inv = ~m;
    if ((inv & (inv + 1)) != 0) return false;
    int p = 0;
    while (p < 32 && (m & (0x80000000u >> p))) ++p;
    if (prefix) *prefix = p;
    return true;
}

bool ParseAddressInput(const std::wstring& input, IpEntry* out, std::wstring* err) {
    std::wstring s = Trim(input);
    std::wstring addr = s, rest;
    size_t sep = s.find_first_of(L"/ ");
    if (sep != std::wstring::npos) {
        addr = s.substr(0, sep);
        rest = Trim(s.substr(sep + 1));
    }
    uint32_t a;
    if (!ParseIpv4(addr, &a)) {
        if (err) *err = L"Invalid IPv4 address";
        return false;
    }
    int prefix = 24;
    if (!rest.empty()) {
        if (rest.find(L'.') != std::wstring::npos) {
            if (!MaskToPrefix(rest, &prefix)) {
                if (err) *err = L"Invalid subnet mask";
                return false;
            }
        } else {
            bool digits = rest.size() <= 2;
            for (wchar_t c : rest) digits = digits && iswdigit(c);
            prefix = digits ? _wtoi(rest.c_str()) : -1;
            if (prefix < 1 || prefix > 32) {
                if (err) *err = L"Prefix must be 1..32";
                return false;
            }
        }
    }
    if (a == 0 || (a >> 24) == 127 || (a >> 28) >= 14) {
        if (err) *err = L"Address not usable on an adapter";
        return false;
    }
    if (out) {
        out->ip = FormatIpv4(a);
        out->prefix = prefix;
    }
    return true;
}

std::wstring Format(const IpEntry& e) { return e.ip + L"/" + std::to_wstring(e.prefix); }

}  // namespace ip
