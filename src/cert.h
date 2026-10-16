// The app's own self-signed code signing certificate (res/nicmanager.cer,
// embedded as a resource). Trusting it machine-wide makes Windows show
// "NIC Manager" as verified publisher of the signed exe.
#pragma once

#include <string>

namespace cert {

bool HasEmbedded();  // false when the build had no res/nicmanager.cer
bool IsInstalled();  // present in Local Machine "Trusted Root" and "Trusted Publishers"

// Need administrator rights. Return an empty string on success, else the reason.
std::wstring Install();
std::wstring Remove();

std::wstring Thumbprint();  // SHA-1, hex, for display

}  // namespace cert
