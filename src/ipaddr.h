// IPv4 / host name parsing and validation.
#pragma once

#include <cstdint>
#include <string>

#include "model.h"

namespace ip {

// Strict dotted-quad parser ("10.0.0.1"). Output is host byte order.
bool ParseIpv4(const std::wstring& s, uint32_t* out = nullptr);
std::wstring FormatIpv4(uint32_t hostOrder);

// 169.254.0.0/16 (APIPA). Windows adds these on its own; never part of a config.
bool IsLinkLocal(const std::wstring& s);

// RFC 1123 host name (letters, digits, hyphens; labels 1..63; total <= 253),
// either a single label ("nas01") or a domain name whose top-level label
// starts with a letter ("srv.example.com").
bool IsValidHostname(const std::wstring& s);
// Plain IPv4 address (no leading zeros) or host name, as accepted for the
// host list.
bool IsValidHost(const std::wstring& s);
// The looser rule of earlier versions, for hosts already saved: loading
// must not drop an entry the user once added.
bool IsStoredHost(const std::wstring& s);

std::wstring PrefixToMask(int prefix);
bool MaskToPrefix(const std::wstring& mask, int* prefix);

// Accepts "192.168.1.10/24", "192.168.1.10 255.255.255.0" or "192.168.1.10"
// (defaults to /24). On failure, err gets a short reason.
bool ParseAddressInput(const std::wstring& s, IpEntry* out, std::wstring* err);
std::wstring Format(const IpEntry& e);  // "192.168.1.10/24"

}  // namespace ip
