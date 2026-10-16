#include "dialogs.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

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

// ---- host action --------------------------------------------------------

struct ActionData {
    HostAction* action;
    std::wstring defaultHost;
    bool isNew;
};

int SelectedType(HWND dlg) { return (int)SendDlgItemMessageW(dlg, IDC_TYPE, CB_GETCURSEL, 0, 0); }

void UpdateActionFields(HWND dlg, bool resetPort) {
    int type = SelectedType(dlg);  // 0 RDP, 1 PuTTY, 2 VNC
    bool port = type != 0, user = type != 2, mode = type == 0;
    for (int id : {IDC_LBL_PORT, IDC_PORT}) ShowWindow(GetDlgItem(dlg, id), port ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_LBL_USER, IDC_USER}) ShowWindow(GetDlgItem(dlg, id), user ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_LBL_MODE, IDC_MODE}) ShowWindow(GetDlgItem(dlg, id), mode ? SW_SHOW : SW_HIDE);
    if (resetPort && port) SetDlgItemInt(dlg, IDC_PORT, type == 1 ? 22 : 5900, FALSE);

    // Without a port field the user field takes the full row.
    RECT label{port ? 53 : 7, 63, 179, 72}, edit{port ? 53 : 7, 73, 179, 86};
    MapDialogRect(dlg, &label);
    MapDialogRect(dlg, &edit);
    MoveWindow(GetDlgItem(dlg, IDC_LBL_USER), label.left, label.top, label.right - label.left,
               label.bottom - label.top, TRUE);
    MoveWindow(GetDlgItem(dlg, IDC_USER), edit.left, edit.top, edit.right - edit.left, edit.bottom - edit.top, TRUE);
}

INT_PTR CALLBACK ActionProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<ActionData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        d = reinterpret_cast<ActionData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        for (const wchar_t* t : {L"Remote Desktop (RDP)", L"PuTTY (SSH)", L"TightVNC"})
            SendDlgItemMessageW(dlg, IDC_TYPE, CB_ADDSTRING, 0, (LPARAM)t);
        for (const wchar_t* t : {L"Full screen", L"Window 1024 x 768"})
            SendDlgItemMessageW(dlg, IDC_MODE, CB_ADDSTRING, 0, (LPARAM)t);
        const ActionSettings& s = d->action->settings;
        SendDlgItemMessageW(dlg, IDC_TYPE, CB_SETCURSEL, s.index(), 0);
        SendDlgItemMessageW(dlg, IDC_MODE, CB_SETCURSEL, 0, 0);
        std::wstring host = d->defaultHost;
        if (auto* r = std::get_if<RdpSettings>(&s)) {
            if (!r->host.empty()) host = r->host;
            SetDlgItemTextW(dlg, IDC_USER, r->user.c_str());
            SetDlgItemTextW(dlg, IDC_PASS, r->password.c_str());
            SendDlgItemMessageW(dlg, IDC_MODE, CB_SETCURSEL, r->fullscreen ? 0 : 1, 0);
        } else if (auto* p = std::get_if<PuttySettings>(&s)) {
            if (!p->host.empty()) host = p->host;
            SetDlgItemInt(dlg, IDC_PORT, p->port, FALSE);
            SetDlgItemTextW(dlg, IDC_USER, p->user.c_str());
            SetDlgItemTextW(dlg, IDC_PASS, p->password.c_str());
        } else if (auto* v = std::get_if<VncSettings>(&s)) {
            if (!v->host.empty()) host = v->host;
            SetDlgItemInt(dlg, IDC_PORT, v->port, FALSE);
            SetDlgItemTextW(dlg, IDC_PASS, v->password.c_str());
        }
        SetDlgItemTextW(dlg, IDC_HOST, host.c_str());
        SetWindowTextW(dlg, d->isNew ? L"Add host action" : L"Edit host action");
        UpdateActionFields(dlg, false);
        InitDark(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
            UpdateActionFields(dlg, true);
        } else if (LOWORD(wp) == IDOK) {
            std::wstring host = ItemText(dlg, IDC_HOST);
            if (!ip::IsValidHost(host)) {
                Warn(dlg, IDC_HOST, L"Enter a valid IPv4 address or host name.");
                return TRUE;
            }
            int type = SelectedType(dlg);
            BOOL okPort = TRUE;
            int port = (int)GetDlgItemInt(dlg, IDC_PORT, &okPort, FALSE);
            if (type != 0 && (!okPort || port < 1 || port > 65535)) {
                Warn(dlg, IDC_PORT, L"Port must be between 1 and 65535.");
                return TRUE;
            }
            std::wstring user = ItemText(dlg, IDC_USER);
            std::wstring pass = GetWindowTextStr(GetDlgItem(dlg, IDC_PASS));
            if (type == 0) {
                bool full = SendDlgItemMessageW(dlg, IDC_MODE, CB_GETCURSEL, 0, 0) == 0;
                d->action->settings = RdpSettings{host, user, pass, full};
            } else if (type == 1) {
                d->action->settings = PuttySettings{host, port, user, pass};
            } else {
                d->action->settings = VncSettings{host, port, pass};
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

bool EditAction(HWND owner, HostAction& action, const std::wstring& defaultHost, bool isNew) {
    HostAction copy = action;
    ActionData d{&copy, defaultHost, isNew};
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
