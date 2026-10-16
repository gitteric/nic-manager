// IPv4 / host name parsing and validation.
#pragma once

#include <cstdint>
#include <string>

#include "model.h"

namespace ip {

// Strict dotted-quad parser ("10.0.0.1"). Output is host byte order.
bool ParseIpv4(const std::wstring& s, uint32_t* out = nullptr);
std::wstring FormatIpv4(uint32_t hostOrder);

// RFC 1123 host name (letters, digits, hyphens; labels 1..63; total <= 253).
bool IsValidHostname(const std::wstring& s);
// IPv4 address or host name, as accepted for the ping list.
bool IsValidHost(const std::wstring& s);

std::wstring PrefixToMask(int prefix);
bool MaskToPrefix(const std::wstring& mask, int* prefix);

// Accepts "192.168.1.10/24", "192.168.1.10 255.255.255.0" or "192.168.1.10"
// (defaults to /24). On failure, err gets a short reason.
bool ParseAddressInput(const std::wstring& s, IpEntry* out, std::wstring* err);
std::wstring Format(const IpEntry& e);  // "192.168.1.10/24"

}  // namespace ip
