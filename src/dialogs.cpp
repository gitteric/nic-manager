#include "dialogs.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

#include "actions.h"
#include "ipaddr.h"
#include "resource.h"
#include "settings.h"
#include "strutil.h"
#include "theme.h"

namespace dialogs {
namespace {

HBRUSH BgBrush() {
    static HBRUSH b = CreateSolidBrush(theme::Bg);
    return b;
}
HBRUSH FieldBrush() {
    static HBRUSH b = CreateSolidBrush(theme::Field);
    return b;
}

BOOL CALLBACK ThemeChild(HWND child, LPARAM) {
    wchar_t cls[32] = {};
    GetClassNameW(child, cls, 32);
    if (_wcsicmp(cls, L"Button") == 0 || _wcsicmp(cls, L"ListBox") == 0)
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
    else if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0)
        SetWindowTheme(child, L"DarkMode_CFD", nullptr);
    return TRUE;
}

// Shared WM_CTLCOLOR* handling. Returns a brush or 0 if not handled.
INT_PTR DarkColors(UINT msg, WPARAM wp) {
    HDC dc = (HDC)wp;
    switch (msg) {
    case WM_CTLCOLORDLG: return (INT_PTR)BgBrush();
    case WM_CTLCOLORSTATIC:
        SetTextColor(dc, theme::TextDim);
        SetBkColor(dc, theme::Bg);
        return (INT_PTR)BgBrush();
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, theme::Text);
        SetBkColor(dc, theme::Field);
        return (INT_PTR)FieldBrush();
    case WM_CTLCOLORBTN: return (INT_PTR)BgBrush();
    }
    return 0;
}

void InitDark(HWND dlg) {
    ApplyDarkFrame(dlg);
    EnumChildWindows(dlg, ThemeChild, 0);
}

std::wstring ItemText(HWND dlg, int id) { return Trim(GetWindowTextStr(GetDlgItem(dlg, id))); }

void Warn(HWND dlg, int focusId, const std::wstring& msg) {
    MessageBoxW(dlg, msg.c_str(), L"NIC Manager", MB_OK | MB_ICONWARNING);
    HWND c = GetDlgItem(dlg, focusId);
    SetFocus(c);
    SendMessageW(c, EM_SETSEL, 0, -1);
}

// ---- prompt -------------------------------------------------------------

struct PromptData {
    std::wstring title, label, text;
    Validator validate;
};

INT_PTR CALLBACK PromptProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<PromptData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        d = reinterpret_cast<PromptData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, d->title.c_str());
        SetDlgItemTextW(dlg, IDC_PROMPT_LABEL, d->label.c_str());
        SetDlgItemTextW(dlg, IDC_PROMPT_EDIT, d->text.c_str());
        InitDark(dlg);
        SetFocus(GetDlgItem(dlg, IDC_PROMPT_EDIT));
        SendDlgItemMessageW(dlg, IDC_PROMPT_EDIT, EM_SETSEL, 0, -1);
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            std::wstring text = ItemText(dlg, IDC_PROMPT_EDIT), err;
            if (text.empty()) return TRUE;
            if (d->validate && !d->validate(text, err)) {
                Warn(dlg, IDC_PROMPT_EDIT, err);
                return TRUE;
            }
            d->text = text;
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
        }
        return TRUE;
    }
    return DarkColors(msg, wp);
}

// ---- start task ---------------------------------------------------------

struct StartTaskData {
    bool* autostart;
    bool* shortcut;
};

INT_PTR CALLBACK StartTaskProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<StartTaskData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        d = reinterpret_cast<StartTaskData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        CheckDlgButton(dlg, IDC_AUTOSTART, *d->autostart ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_SHORTCUT, *d->shortcut ? BST_CHECKED : BST_UNCHECKED);
        InitDark(dlg);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            *d->autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
            *d->shortcut = IsDlgButtonChecked(dlg, IDC_SHORTCUT) == BST_CHECKED;
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
        }
        return TRUE;
    }
    return DarkColors(msg, wp);
}

// ---- host entry ---------------------------------------------------------

struct HostData {
    std::wstring* name;
    std::wstring* address;
};

INT_PTR CALLBACK HostProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<HostData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        d = reinterpret_cast<HostData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetDlgItemTextW(dlg, IDC_HOST_NAME, d->name->c_str());
        SetDlgItemTextW(dlg, IDC_HOST, d->address->c_str());
        InitDark(dlg);
        SetFocus(GetDlgItem(dlg, IDC_HOST_NAME));
        SendDlgItemMessageW(dlg, IDC_HOST_NAME, EM_SETSEL, 0, -1);
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            std::wstring address = ItemText(dlg, IDC_HOST), name = ItemText(dlg, IDC_HOST_NAME);
            if (!ip::IsValidHost(address)) {
                Warn(dlg, IDC_HOST, L"Enter a valid IPv4 address or host name.");
                return TRUE;
            }
            *d->name = name.empty() ? address : name;
            *d->address = address;
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
        }
        return TRUE;
    }
    return DarkColors(msg, wp);
}

// ---- host action --------------------------------------------------------

struct ActionData {
    HostAction* action;
    std::wstring hostAddress;
    bool isNew;
    std::wstring* vncPath;
};

enum ActionType { kRdp, kSsh, kVnc, kBrowser };

int SelectedType(HWND dlg) { return (int)SendDlgItemMessageW(dlg, IDC_TYPE, CB_GETCURSEL, 0, 0); }

void Place(HWND dlg, int id, int x, int y, int w, int h, bool show) {
    HWND c = GetDlgItem(dlg, id);
    RECT r{x, y, x + w, y + h};
    MapDialogRect(dlg, &r);
    MoveWindow(c, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
    ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

// Shows the fields of the selected type and stacks them without gaps
// (positions in dialog units).
void UpdateActionFields(HWND dlg, bool resetPort) {
    const int type = SelectedType(dlg);
    const bool browser = type == kBrowser, port = type == kSsh || type == kVnc, user = type == kRdp || type == kSsh;
    const bool pass = !browser, mode = type == kRdp, vnc = type == kVnc;

    // Other types connect to the host entry's address; the browser has its
    // own URL, preset to that address.
    SetDlgItemTextW(dlg, IDC_LBL_HOST, browser ? L"URL (or host:port)" : L"Host (from the host entry)");
    SendDlgItemMessageW(dlg, IDC_HOST, EM_SETREADONLY, !browser, 0);
    auto* d = reinterpret_cast<ActionData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (!browser && d) SetDlgItemTextW(dlg, IDC_HOST, d->hostAddress.c_str());
    if (resetPort && port) SetDlgItemInt(dlg, IDC_PORT, type == kSsh ? 22 : 5900, FALSE);

    int y = 35;
    auto row = [&](bool visible) {
        int top = y;
        if (visible) y += 28;
        return top;
    };
    int r = row(true);
    Place(dlg, IDC_LBL_HOST, 7, r, 172, 9, true);
    Place(dlg, IDC_HOST, 7, r + 10, 172, 13, true);
    r = row(port || user);
    int ux = port ? 53 : 7;
    Place(dlg, IDC_LBL_PORT, 7, r, 40, 9, port);
    Place(dlg, IDC_PORT, 7, r + 10, 40, 13, port);
    Place(dlg, IDC_LBL_USER, ux, r, 179 - ux, 9, user);
    Place(dlg, IDC_USER, ux, r + 10, 179 - ux, 13, user);
    r = row(pass);
    Place(dlg, IDC_LBL_PASS, 7, r, 172, 9, pass);
    Place(dlg, IDC_PASS, 7, r + 10, 172, 13, pass);
    r = row(mode || vnc);
    Place(dlg, IDC_LBL_MODE, 7, r, 172, 9, mode);
    Place(dlg, IDC_MODE, 7, r + 10, 172, 40, mode);
    Place(dlg, IDC_LBL_VNC, 7, r, 172, 9, vnc);
    Place(dlg, IDC_VNCPATH, 7, r + 10, 150, 13, vnc);
    Place(dlg, IDC_BROWSE, 160, r + 10, 19, 13, vnc);
    y += 2;
    Place(dlg, IDOK, 75, y, 50, 14, true);
    Place(dlg, IDCANCEL, 129, y, 50, 14, true);

    // Fit the dialog to the stacked fields.
    RECT client{0, 0, 186, y + 21};
    MapDialogRect(dlg, &client);
    RECT wr, cr;
    GetWindowRect(dlg, &wr);
    GetClientRect(dlg, &cr);
    int frameH = (wr.bottom - wr.top) - cr.bottom;
    SetWindowPos(dlg, nullptr, 0, 0, wr.right - wr.left, client.bottom + frameH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(dlg, nullptr, TRUE);
}

INT_PTR CALLBACK ActionProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<ActionData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        d = reinterpret_cast<ActionData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        for (const wchar_t* t : {L"Remote Desktop (RDP)", L"SSH (Windows OpenSSH)", L"TightVNC", L"Browser"})
            SendDlgItemMessageW(dlg, IDC_TYPE, CB_ADDSTRING, 0, (LPARAM)t);
        for (const wchar_t* t : {L"Full screen", L"Window 1024 x 768"})
            SendDlgItemMessageW(dlg, IDC_MODE, CB_ADDSTRING, 0, (LPARAM)t);
        const ActionSettings& s = d->action->settings;
        SendDlgItemMessageW(dlg, IDC_TYPE, CB_SETCURSEL, s.index(), 0);
        SendDlgItemMessageW(dlg, IDC_MODE, CB_SETCURSEL, 0, 0);
        SetDlgItemTextW(dlg, IDC_HOST, d->hostAddress.c_str());
        if (auto* r = std::get_if<RdpSettings>(&s)) {
            SetDlgItemTextW(dlg, IDC_USER, r->user.c_str());
            SetDlgItemTextW(dlg, IDC_PASS, r->password.c_str());
            SendDlgItemMessageW(dlg, IDC_MODE, CB_SETCURSEL, r->fullscreen ? 0 : 1, 0);
        } else if (auto* p = std::get_if<SshSettings>(&s)) {
            SetDlgItemInt(dlg, IDC_PORT, p->port, FALSE);
            SetDlgItemTextW(dlg, IDC_USER, p->user.c_str());
            SetDlgItemTextW(dlg, IDC_PASS, p->password.c_str());
        } else if (auto* v = std::get_if<VncSettings>(&s)) {
            SetDlgItemInt(dlg, IDC_PORT, v->port, FALSE);
            SetDlgItemTextW(dlg, IDC_PASS, v->password.c_str());
        } else if (auto* b = std::get_if<BrowserSettings>(&s)) {
            if (!b->url.empty()) SetDlgItemTextW(dlg, IDC_HOST, b->url.c_str());
        }
        SetDlgItemTextW(dlg, IDC_VNCPATH, (d->vncPath->empty() ? actions::FindTightVnc() : *d->vncPath).c_str());
        SetWindowTextW(dlg, d->isNew ? L"Add host action" : L"Edit host action");
        UpdateActionFields(dlg, false);
        InitDark(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
            if (SelectedType(dlg) == kBrowser) SetDlgItemTextW(dlg, IDC_HOST, d->hostAddress.c_str());
            UpdateActionFields(dlg, true);
        } else if (LOWORD(wp) == IDC_BROWSE) {
            wchar_t buf[MAX_PATH] = {};
            std::wstring cur = ItemText(dlg, IDC_VNCPATH);
            wcsncpy(buf, cur.c_str(), MAX_PATH - 1);
            OPENFILENAMEW ofn{sizeof(ofn)};
            ofn.hwndOwner = dlg;
            ofn.lpstrFilter = L"TightVNC viewer (tvnviewer.exe)\0tvnviewer.exe\0Programs (*.exe)\0*.exe\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Select the TightVNC viewer";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(dlg, IDC_VNCPATH, buf);
        } else if (LOWORD(wp) == IDOK) {
            const int type = SelectedType(dlg);
            std::wstring url = ItemText(dlg, IDC_HOST);
            if (type == kBrowser && !actions::IsValidBrowserTarget(url)) {
                Warn(dlg, IDC_HOST, L"Enter an http(s):// URL or host[:port], e.g. 10.0.0.5:8080/admin.");
                return TRUE;
            }
            BOOL okPort = TRUE;
            int port = (int)GetDlgItemInt(dlg, IDC_PORT, &okPort, FALSE);
            if ((type == kSsh || type == kVnc) && (!okPort || port < 1 || port > 65535)) {
                Warn(dlg, IDC_PORT, L"Port must be between 1 and 65535.");
                return TRUE;
            }
            std::wstring user = ItemText(dlg, IDC_USER);
            if (type == kSsh && user.find_first_of(L" \t\"%&|<>^") != std::wstring::npos) {
                Warn(dlg, IDC_USER, L"The SSH user name must not contain spaces or any of \" % & | < > ^");
                return TRUE;
            }
            std::wstring pass = GetWindowTextStr(GetDlgItem(dlg, IDC_PASS));
            if (type == kVnc) {
                std::wstring path = ItemText(dlg, IDC_VNCPATH);
                DWORD attr = GetFileAttributesW(path.c_str());
                if (!path.empty() && (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))) {
                    Warn(dlg, IDC_VNCPATH, L"The TightVNC viewer was not found at this path.");
                    return TRUE;
                }
                *d->vncPath = path;
            }
            if (type == kRdp) {
                bool full = SendDlgItemMessageW(dlg, IDC_MODE, CB_GETCURSEL, 0, 0) == 0;
                d->action->settings = RdpSettings{user, pass, full};
            } else if (type == kSsh) {
                d->action->settings = SshSettings{port, user, pass};
            } else if (type == kVnc) {
                d->action->settings = VncSettings{port, pass};
            } else {
                d->action->settings = BrowserSettings{url};
            }
            EndDialog(dlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
        }
        return TRUE;
    }
    return DarkColors(msg, wp);
}

// ---- presets ------------------------------------------------------------

struct PresetsData {
    std::vector<Preset>* presets;
    bool changed = false;
};

std::wstring PresetSummary(const Preset& p) {
    if (p.cfg.dhcp) return p.name + L"  (DHCP)";
    return p.name + L"  (" + ip::Format(p.cfg.addresses.front()) + L")";
}

void FillPresetList(HWND dlg, const std::vector<Preset>& presets) {
    HWND list = GetDlgItem(dlg, IDC_PRESET_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& p : presets) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)PresetSummary(p).c_str());
}

std::vector<int> SelectedIndices(HWND dlg) {
    HWND list = GetDlgItem(dlg, IDC_PRESET_LIST);
    int n = (int)SendMessageW(list, LB_GETSELCOUNT, 0, 0);
    std::vector<int> idx(std::max(n, 0));
    if (n > 0) SendMessageW(list, LB_GETSELITEMS, n, (LPARAM)idx.data());
    return idx;
}

bool FileDialog(HWND owner, bool save, std::wstring& path) {
    wchar_t buf[MAX_PATH] = L"ipv4-presets.json";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    if (!save) buf[0] = 0;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buf;
    return true;
}

void UpdatePresetButtons(HWND dlg, const std::vector<Preset>& presets) {
    EnableWindow(GetDlgItem(dlg, IDC_DELETE), !SelectedIndices(dlg).empty());
    EnableWindow(GetDlgItem(dlg, IDC_EXPORT), !presets.empty());
}

INT_PTR CALLBACK PresetsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<PresetsData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        d = reinterpret_cast<PresetsData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        FillPresetList(dlg, *d->presets);
        UpdatePresetButtons(dlg, *d->presets);
        InitDark(dlg);
        return TRUE;
    case WM_COMMAND: {
        auto& presets = *d->presets;
        switch (LOWORD(wp)) {
        case IDC_PRESET_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) UpdatePresetButtons(dlg, presets);
            break;
        case IDC_DELETE: {
            auto sel = SelectedIndices(dlg);
            if (sel.empty()) break;
            std::wstring q = sel.size() == 1 ? L"Delete preset \"" + presets[sel[0]].name + L"\"?"
                                             : L"Delete " + std::to_wstring(sel.size()) + L" presets?";
            if (MessageBoxW(dlg, q.c_str(), L"NIC Manager", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            std::sort(sel.rbegin(), sel.rend());
            for (int i : sel) presets.erase(presets.begin() + i);
            d->changed = true;
            FillPresetList(dlg, presets);
            UpdatePresetButtons(dlg, presets);
            break;
        }
        case IDC_EXPORT: {
            auto sel = SelectedIndices(dlg);
            std::vector<Preset> out;
            if (sel.empty()) out = presets;  // nothing selected: export all
            for (int i : sel) out.push_back(presets[i]);
            std::wstring path;
            if (!FileDialog(dlg, true, path)) break;
            if (!settings::WriteFileUtf8(path, settings::PresetsToJson(out)))
                MessageBoxW(dlg, (L"Could not write " + path).c_str(), L"NIC Manager", MB_OK | MB_ICONERROR);
            break;
        }
        case IDC_IMPORT: {
            std::wstring path, err;
            std::string data;
            if (!FileDialog(dlg, false, path)) break;
            std::vector<Preset> in;
            if (!settings::ReadFileUtf8(path, data)) err = L"Could not read " + path;
            else settings::PresetsFromJson(data, in, &err);
            if (!err.empty()) {
                MessageBoxW(dlg, err.c_str(), L"Import failed", MB_OK | MB_ICONERROR);
                break;
            }
            for (auto& p : in) {  // same name replaces the existing preset
                auto it = std::find_if(presets.begin(), presets.end(),
                                       [&](const Preset& e) { return _wcsicmp(e.name.c_str(), p.name.c_str()) == 0; });
                if (it != presets.end()) *it = p;
                else presets.push_back(p);
            }
            d->changed = d->changed || !in.empty();
            FillPresetList(dlg, presets);
            UpdatePresetButtons(dlg, presets);
            std::wstring done = L"Imported " + std::to_wstring(in.size()) + L" preset(s).";
            MessageBoxW(dlg, done.c_str(), L"NIC Manager", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case IDCANCEL:
        case IDOK: EndDialog(dlg, IDOK); break;
        }
        return TRUE;
    }
    }
    return DarkColors(msg, wp);
}

}  // namespace

void ApplyDarkFrame(HWND hwnd) {
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+), 19 on older builds.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

std::optional<std::wstring> PromptText(HWND owner, const std::wstring& title, const std::wstring& label,
                                       const std::wstring& initial, Validator validate) {
    PromptData d{title, label, initial, std::move(validate)};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PROMPT), owner, PromptProc,
                        (LPARAM)&d) != IDOK)
        return std::nullopt;
    return d.text;
}

bool AskStartTask(HWND owner, bool& autostart, bool& shortcut) {
    StartTaskData d{&autostart, &shortcut};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_STARTTASK), owner, StartTaskProc,
                           (LPARAM)&d) == IDOK;
}

bool EditHost(HWND owner, std::wstring& name, std::wstring& address) {
    std::wstring n = name, a = address;
    HostData d{&n, &a};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HOST), owner, HostProc, (LPARAM)&d) != IDOK)
        return false;
    name = n;
    address = a;
    return true;
}

bool EditAction(HWND owner, HostAction& action, const std::wstring& hostAddress, bool isNew, std::wstring& vncPath) {
    HostAction copy = action;
    ActionData d{&copy, hostAddress, isNew, &vncPath};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ACTION), owner, ActionProc,
                        (LPARAM)&d) != IDOK)
        return false;
    action = copy;
    return true;
}

bool ManagePresets(HWND owner, std::vector<Preset>& presets) {
    PresetsData d{&presets};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PRESETS), owner, PresetsProc, (LPARAM)&d);
    return d.changed;
}

}  // namespace dialogs
