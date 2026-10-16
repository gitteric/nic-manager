#include "cert.h"

#include <windows.h>
#include <wincrypt.h>

#include "resource.h"

namespace cert {
namespace {

constexpr const wchar_t* kStores[] = {L"Root", L"TrustedPublisher"};

// The embedded DER certificate, or nullptr.
PCCERT_CONTEXT Embedded() {
    static PCCERT_CONTEXT ctx = [] () -> PCCERT_CONTEXT {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SIGNING_CERT), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
        if (!res) return nullptr;
        HGLOBAL mem = LoadResource(nullptr, res);
        DWORD size = SizeofResource(nullptr, res);
        if (!mem || !size) return nullptr;
        return CertCreateCertificateContext(X509_ASN_ENCODING, static_cast<const BYTE*>(LockResource(mem)), size);
    }();
    return ctx;
}

HCERTSTORE OpenStore(const wchar_t* name, bool write) {
    DWORD flags = CERT_SYSTEM_STORE_LOCAL_MACHINE | (write ? 0 : CERT_STORE_READONLY_FLAG);
    // Writes go to the registry store itself; reads see the whole logical store.
    return CertOpenStore(write ? CERT_STORE_PROV_SYSTEM_REGISTRY_W : CERT_STORE_PROV_SYSTEM_W, 0, 0, flags, name);
}

std::wstring ErrorText(const std::wstring& what) {
    DWORD err = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring text = what + (msg ? L": " + std::wstring(msg) : L".");
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    return text;
}

}  // namespace

bool HasEmbedded() { return Embedded() != nullptr; }

bool IsInstalled() {
    PCCERT_CONTEXT c = Embedded();
    if (!c) return false;
    for (const wchar_t* name : kStores) {
        HCERTSTORE store = OpenStore(name, false);
        if (!store) return false;
        PCCERT_CONTEXT found = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, c, nullptr);
        if (found) CertFreeCertificateContext(found);
        CertCloseStore(store, 0);
        if (!found) return false;
    }
    return true;
}

std::wstring Install() {
    PCCERT_CONTEXT c = Embedded();
    if (!c) return L"This build contains no signing certificate.";
    for (const wchar_t* name : kStores) {
        HCERTSTORE store = OpenStore(name, true);
        if (!store) return ErrorText(L"Could not open the certificate store");
        BOOL ok = CertAddCertificateContextToStore(store, c, CERT_STORE_ADD_USE_EXISTING, nullptr);
        std::wstring err = ok ? std::wstring() : ErrorText(L"Could not add the certificate");
        CertCloseStore(store, 0);
        if (!ok) return err;
    }
    return {};
}

std::wstring Remove() {
    PCCERT_CONTEXT c = Embedded();
    if (!c) return L"This build contains no signing certificate.";
    for (const wchar_t* name : kStores) {
        HCERTSTORE store = OpenStore(name, true);
        if (!store) return ErrorText(L"Could not open the certificate store");
        // CertDeleteCertificateFromStore frees the context it is given.
        while (PCCERT_CONTEXT found =
                   CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, c, nullptr)) {
            if (!CertDeleteCertificateFromStore(found)) {
                std::wstring err = ErrorText(L"Could not remove the certificate");
                CertCloseStore(store, 0);
                return err;
            }
        }
        CertCloseStore(store, 0);
    }
    return {};
}

std::wstring Thumbprint() {
    PCCERT_CONTEXT c = Embedded();
    if (!c) return {};
    BYTE hash[20];
    DWORD size = sizeof(hash);
    if (!CertGetCertificateContextProperty(c, CERT_SHA1_HASH_PROP_ID, hash, &size)) return {};
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring s;
    for (DWORD i = 0; i < size; ++i) {
        s += hex[hash[i] >> 4];
        s += hex[hash[i] & 15];
    }
    return s;
}

}  // namespace cert
