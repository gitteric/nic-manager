#include "mainwindow.h"

#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwctype>

#include "actions.h"
#include "cert.h"
#include "resource.h"
#include "starttask.h"
#include "dialogs.h"
#include "ipaddr.h"
#include "scan.h"
#include "strutil.h"
#include "theme.h"

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_ADAPTERS = WM_APP + 2;
constexpr UINT WM_APP_PING = WM_APP + 3;
constexpr UINT WM_APP_JOBDONE = WM_APP + 4;
constexpr UINT WM_APP_SCANPROGRESS = WM_APP + 6;  // wParam: done, lParam: total
constexpr UINT WM_APP_EDITKEY = WM_APP + 5;  // wParam: 1 commit, 0 cancel; lParam: edit generation

constexpr UINT_PTR kTimerSpin = 1;
constexpr UINT_PTR kTimerStatus = 2;
constexpr UINT_PTR kTimerToast = 3;
constexpr UINT_PTR kTimerTray = 4;  // retries the tray icon while Explorer is not ready yet
constexpr UINT kToastMs = 5000;
constexpr UINT kTrayId = 1;
constexpr int kEditId = 100;

// Segoe MDL2 Assets glyphs (present on every Windows 10/11 install).
constexpr wchar_t kGlyphEdit = 0xE70F;
constexpr wchar_t kGlyphDelete = 0xE74D;
constexpr wchar_t kGlyphAdd = 0xE710;
constexpr wchar_t kGlyphClose = 0xE8BB;
constexpr wchar_t kGlyphChevronDown = 0xE70D;
constexpr wchar_t kGlyphChevronRight = 0xE76C;
constexpr wchar_t kGlyphNetwork = 0xE839;
constexpr wchar_t kGlyphGlobe = 0xE774;
constexpr wchar_t kGlyphRefresh = 0xE72C;
constexpr wchar_t kGlyphPlay = 0xE768;
constexpr wchar_t kGlyphWifi = 0xE701;
constexpr wchar_t kGlyphCopy = 0xE8C8;
constexpr wchar_t kGlyphMinimize = 0xE921;
constexpr wchar_t kGlyphError = 0xEA39;
constexpr wchar_t kGlyphScan = 0xE721;  // magnifier
constexpr wchar_t kGlyphView = 0xE890;  // eye: hidden adapters are shown
constexpr wchar_t kGlyphHide = 0xED1A;  // crossed-out eye: hidden adapters are left out

enum : UINT {
    IDM_SHOW = 100, IDM_PRESETS, IDM_TOPMOST, IDM_TO_TRAY, IDM_CERT, IDM_TASK_SETUP, IDM_TASK_REMOVE, IDM_EXIT, IDM_CYCLE_CUSTOM, IDM_SAVE_PRESET, IDM_REFRESH,
    IDM_SCAN_ARP, IDM_SCAN_PING, IDM_SCAN_TCP, IDM_HIDE_ADAPTER, IDM_HOST_PING,
    IDM_CYCLE_BASE = 200,
    IDM_DESIGN_BASE = 300,
    IDM_APPLY_PRESET_BASE = 1000,
};
constexpr DWORD kCycleChoices[] = {500, 1000, 5000, 10000, 30000};

void Fill(HDC dc, const RECT& rc, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void DrawStr(HDC dc, HFONT font, const std::wstring& s, RECT rc, COLORREF color,
             UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &rc, flags | DT_NOPREFIX);
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

void DrawGlyph(HDC dc, HFONT font, wchar_t glyph, RECT rc, COLORREF color) {
    wchar_t s[2] = {glyph, 0};
    SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, 1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

COLORREF Lerp(COLORREF a, COLORREF b, double t) {
    auto mix = [t](int x, int y) { return (BYTE)(x + (y - x) * t); };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

RECT Offset(RECT rc, int dy) {
    OffsetRect(&rc, 0, dy);
    return rc;
}

UINT DpiForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) return fn(hwnd);
    HDC dc = GetDC(nullptr);
    UINT dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return dpi;
}

// Draws the app icon pixel by pixel: accent rounded square with a small
// white network topology. No .ico file needed.
// The app icon (res/app.ico, rendered by tools/make-icon.py) at a given size.
HICON LoadAppIcon(HINSTANCE inst, int size) {
    HICON icon = nullptr;
    if (FAILED(LoadIconWithScaleDown(inst, MAKEINTRESOURCEW(IDI_APP), size, size, &icon)))
        icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, 0));
    return icon;
}

}  // namespace

// =========================================================================
// creation
// =========================================================================

bool MainWindow::Create(HINSTANCE inst, bool demo, bool minimized) {
    m_inst = inst;
    m_demo = demo;
    m_settings = settings::Load();
    theme::Apply(m_settings.design);
    m_expandedAdapter = m_settings.expandedAdapter;  // dropped on the first adapter list if it is gone
    m_elevated = netcmd::IsElevated();
    for (size_t i = 0; i < m_settings.hosts.size(); ++i) m_hostState.push_back({m_nextHostId++});

    m_iconSmall = LoadAppIcon(inst, GetSystemMetrics(SM_CXSMICON));
    m_iconLarge = LoadAppIcon(inst, GetSystemMetrics(SM_CXICON));

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = m_iconLarge;
    wc.hIconSm = m_iconSmall;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    DWORD ex = (m_settings.minimizeToTray ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW) |
               (m_settings.alwaysOnTop ? WS_EX_TOPMOST : 0);
    // WS_MINIMIZEBOX + WS_SYSMENU let the taskbar button minimize/restore the popup.
    m_hwnd = CreateWindowExW(ex, kClassName, L"NIC Manager", WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             200, 200, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    ShowWindowNear(!minimized);
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::OnCreate() {
    m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // We run elevated and Explorer does not: without this, UIPI drops its
    // TaskbarCreated broadcast and the tray icon is gone after an Explorer
    // restart. WM_APP_SHOW comes from a second start of the exe.
    ChangeWindowMessageFilterEx(m_hwnd, m_taskbarCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_APP_SHOW, MSGFLT_ALLOW, nullptr);
    UpdateDpi(DpiForWindow(m_hwnd));

    // Rounded corners on Windows 11 (attribute 33 = DWMWA_WINDOW_CORNER_PREFERENCE, 2 = round).
    int corner = 2;
    DwmSetWindowAttribute(m_hwnd, 33, &corner, sizeof(corner));

    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr, m_inst,
                                nullptr);
    TOOLINFOW ti{sizeof(ti)};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = m_hwnd;
    ti.uId = reinterpret_cast<UINT_PTR>(m_hwnd);
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, S(320));

    AddTrayIcon();
    m_pinger.Init(m_hwnd, WM_APP_PING);
    m_pinger.SetCycle(m_settings.pingCycleMs);
    SyncPinger();
    m_monitor.Start(m_hwnd, WM_APP_ADAPTERS, m_demo);
    if (m_elevated && !m_demo) std::thread(starttask::Upgrade).detach();  // a few schtasks calls, off the UI thread
    if (m_demo) SetStatus(L"Demo mode", theme::Accent, 8000);
    else if (!m_elevated) SetStatus(L"Read-only: not elevated", theme::TextDim, 8000);
    Relayout();
}

void MainWindow::UpdateDpi(UINT dpi) {
    m_dpi = dpi ? dpi : 96;
    M.width = S(300);
    M.title = S(26);
    M.bottom = S(32);
    M.header = S(24);
    M.adapter = S(38);
    M.host = S(26);
    M.toggle = S(24);
    M.caption = S(18);
    M.value = S(20);
    M.action = S(22);
    M.hint = S(22);
    M.pad = S(8);
    M.indent = S(18);
    M.btn = S(18);
    M.bar = S(6);
    M.scrollW = S(4);
    M.editH = S(20);
    M.switchW = S(26);
    M.switchH = S(14);
    M.chevron = S(14);
    CreateFonts();
    if (m_edit) SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
}

void MainWindow::CreateFonts() {
    DestroyFonts();
    auto make = [&](int px, int weight, const wchar_t* face) {
        return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    };
    m_font = make(12, FW_NORMAL, L"Segoe UI");
    m_fontBold = make(12, FW_SEMIBOLD, L"Segoe UI");
    m_fontSmall = make(11, FW_NORMAL, L"Segoe UI");
    m_fontIcon = make(12, FW_NORMAL, L"Segoe MDL2 Assets");
}

void MainWindow::DestroyFonts() {
    for (HFONT* f : {&m_font, &m_fontBold, &m_fontSmall, &m_fontIcon}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

void MainWindow::AddTrayIcon() {
    KillTimer(m_hwnd, kTimerTray);
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = m_iconSmall;
    wcscpy_s(nid.szTip, L"NIC Manager");
    if (Shell_NotifyIconW(NIM_ADD, &nid) || Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        m_trayRetries = 0;
        return;
    }
    // At logon the autostart task can run before the taskbar accepts icons.
    // Without a retry the app would live on hidden, with no way to reach it.
    if (m_trayRetries++ < 60) SetTimer(m_hwnd, kTimerTray, 2000, nullptr);
}

// =========================================================================
// message handling
// =========================================================================

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_taskbarCreated && m_taskbarCreated) {  // Explorer restarted
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
    case WM_CREATE: OnCreate(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(); return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) return 0;
        PlaceButtons();
        PositionEdit();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(m_hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            if (pt.y < M.title && HitTest(pt).fixed < 0) return HTCAPTION;  // drag by the title strip
        }
        return hit;
    }
    case WM_ENTERSIZEMOVE:
        GetWindowRect(m_hwnd, &m_dragStartRect);
        GetCursorPos(&m_dragStartCursor);
        return 0;
    case WM_MOVING: {
        // Keep the window inside the work area of the monitor under the
        // cursor and snap it to that area's edges. The position comes from
        // the drag start plus the total cursor offset: Windows only adds the
        // latest small mouse step to the (snapped) rect it got back, so slow
        // drags would be pulled back to the edge on every step.
        RECT* r = reinterpret_cast<RECT*>(lp);
        POINT cur;
        GetCursorPos(&cur);
        const int curW = r->right - r->left, curH = r->bottom - r->top;  // size may change while dragging
        r->left = m_dragStartRect.left + cur.x - m_dragStartCursor.x;
        r->top = m_dragStartRect.top + cur.y - m_dragStartCursor.y;
        r->right = r->left + curW;
        r->bottom = r->top + curH;
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& work = mi.rcWork;
        const int w = r->right - r->left, h = r->bottom - r->top, snap = S(16);
        int x = r->left, y = r->top;
        if (std::abs(x - work.left) < snap) x = work.left;
        if (std::abs(x + w - work.right) < snap) x = work.right - w;
        if (std::abs(y - work.top) < snap) y = work.top;
        if (std::abs(y + h - work.bottom) < snap) y = work.bottom - h;
        x = std::max<int>(work.left, std::min<int>(x, work.right - w));
        y = std::max<int>(work.top, std::min<int>(y, work.bottom - h));
        OffsetRect(r, x - r->left, y - r->top);
        return TRUE;
    }
    case WM_EXITSIZEMOVE: {
        RECT wr;
        GetWindowRect(m_hwnd, &wr);
        m_settings.windowX = wr.left;
        m_settings.windowY = wr.top;
        settings::SaveGeneral(m_settings);
        return 0;
    }
    case WM_DPICHANGED: {
        UpdateDpi(HIWORD(wp));
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, M.width, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout();
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: Relayout(); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            const Hit& h = m_hover;
            bool clickable = h.fixed >= 0 || h.button >= 0 || h.toggle ||
                             (h.row >= 0 && h.row < (int)m_rows.size() &&
                              (m_rows[h.row].rowCmd != Cmd::None || m_rows[h.row].dblCmd != Cmd::None));
            SetCursor(LoadCursorW(nullptr, m_busy ? IDC_APPSTARTING : clickable ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: OnMouseMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_NCMOUSEMOVE:
        if (wp == HTCAPTION) OnTitleMouseMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        break;
    case WM_NCMOUSELEAVE:
        m_trackingNc = false;
        SetTip(L"");
        return 0;
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        m_hover = {};
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: OnLButtonDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, false); return 0;
    case WM_LBUTTONDBLCLK: OnLButtonDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, true); return 0;
    case WM_LBUTTONUP: OnLButtonUp({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_RBUTTONUP: OnRButtonUp({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_MOUSEWHEEL:
        ScrollTo(m_scroll - GET_WHEEL_DELTA_WPARAM(wp) * S(48) / WHEEL_DELTA);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) ShowWindow(m_hwnd, SW_HIDE);
        else if (wp == VK_F5) m_monitor.Trigger();
        return 0;
    case WM_CTLCOLOREDIT: {
        static HBRUSH field = CreateSolidBrush(theme::Field);
        SetTextColor(reinterpret_cast<HDC>(wp), theme::Text);
        SetBkColor(reinterpret_cast<HDC>(wp), theme::Field);
        return reinterpret_cast<LRESULT>(field);
    }
    case WM_TIMER:
        if (wp == kTimerSpin) {
            m_spin = (m_spin + 1) % 8;
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            rc.top = rc.bottom - M.bottom;
            InvalidateRect(m_hwnd, &rc, FALSE);
        } else if (wp == kTimerToast) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (PtInRect(&m_toastRc, pt)) SetTimer(m_hwnd, kTimerToast, 1000, nullptr);  // held while hovered
            else HideToast();
        } else if (wp == kTimerTray) {
            AddTrayIcon();
        } else if (wp == kTimerStatus) {
            KillTimer(m_hwnd, kTimerStatus);
            m_status.clear();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP) ToggleVisible();
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_APP_SHOW: ShowRestored(); return 0;
    case WM_APP_ADAPTERS: OnAdapters(reinterpret_cast<std::vector<Adapter>*>(lp)); return 0;
    case WM_APP_PING: OnPing((uint32_t)wp, (ping::State)LOWORD(lp), HIWORD(lp)); return 0;
    case WM_APP_JOBDONE: OnJobDone(reinterpret_cast<netcmd::JobResult*>(lp)); return 0;
    case WM_APP_SCANPROGRESS: OnScanProgress((int)wp, (int)lp); return 0;
    case WM_APP_EDITKEY:
        if ((int)lp == m_editGen && m_edit) {
            if (wp) CommitEdit();
            else EndEdit();
        }
        return 0;
    case WM_CLOSE: DestroyWindow(m_hwnd); return 0;  // X button, Alt+F4, taskbar "Close window"
    case WM_DESTROY: {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (m_scanCancel) *m_scanCancel = true;  // ends the running probes
        m_pinger.StopAll();
        m_monitor.Stop();
        if (m_job.joinable()) m_job.detach();  // main() ends the process right after the message loop
        DestroyFonts();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// =========================================================================
// layout
// =========================================================================

MainWindow::Row& MainWindow::Push(RowKind kind, int h) {
    m_rows.emplace_back();
    Row& r = m_rows.back();
    r.kind = kind;
    r.y = m_layoutY;
    r.h = h;
    m_layoutY += h;
    return r;
}

std::wstring MainWindow::AdapterSummary(const Adapter& a) const {
    if (!a.enabled) return L"Disabled";
    if (!a.ipv4Enabled) return L"IPv4 not enabled";
    if (a.cfg.addresses.empty()) {
        if (!a.connected) return L"Not connected";
        return a.linkLocal.empty() ? L"No IPv4 address" : a.linkLocal + L" (link-local)";
    }
    std::wstring s = a.cfg.addresses.front().ip;
    if (a.cfg.addresses.size() > 1) s += L" (...)";
    return s;
}

std::wstring MainWindow::AdapterTip(const Adapter& a) const {
    std::wstring s = a.name + L"\r\n" + a.device;
    if (!a.enabled) return s + L"\r\nDisabled";
    s += a.connected ? L"\r\nConnected" : L"\r\nNot connected";
    for (const auto& e : a.cfg.addresses) s += L"\r\n" + ip::Format(e);
    if (!a.linkLocal.empty()) s += L"\r\n" + a.linkLocal + L" (link-local)";
    return s;
}

bool MainWindow::IsHidden(const std::wstring& guid) const {
    const auto& list = m_settings.hiddenAdapters;
    return std::find(list.begin(), list.end(), guid) != list.end();
}

COLORREF MainWindow::AdapterColor(const Adapter& a) const {
    if (!a.enabled) return theme::Grey;
    return a.connected ? theme::Green : theme::Red;
}

void MainWindow::BuildLayout() {
    m_rows.clear();
    m_layoutY = 0;

    {
        Row& h = Push(RowKind::Header, M.header);
        h.text = L"ADAPTERS";
    }
    int listed = 0;
    for (int i = 0; i < (int)m_adapters.size(); ++i) {
        const Adapter& a = m_adapters[i];
        const bool hidden = IsHidden(a.guid);
        if (hidden && !m_settings.showHidden) continue;
        ++listed;
        bool expanded = a.guid == m_expandedAdapter;
        {
            Row& r = Push(RowKind::Adapter, M.adapter);
            r.adapter = i;
            r.text = a.name;
            r.sub = AdapterSummary(a);
            r.tip = AdapterTip(a);
            r.state = AdapterColor(a);
            r.dim = !a.enabled || hidden;
            if (hidden) {
                r.sub = L"Hidden \u00B7 " + r.sub;
                r.tip += L"\r\nHidden (right-click to show it again)";
            }
            r.expanded = expanded;
            r.chevron = true;
            r.rowCmd = Cmd::ExpandAdapter;
            if (a.wireless && a.enabled)
                r.buttons.push_back({kGlyphWifi, Cmd::OpenWifi, L"Choose a Wi-Fi network"});
        }
        if (expanded) AddAdapterDetails(i);
    }
    if (listed == 0) {
        Row& r = Push(RowKind::Hint, M.hint);
        r.text = !m_adaptersLoaded     ? L"Loading..."
                 : m_adapters.empty() ? L"No network adapters found"
                                      : L"All adapters are hidden";
    }

    {
        Row& h = Push(RowKind::Header, M.header);
        h.text = L"HOSTS";
        h.buttons.push_back({kGlyphAdd, Cmd::AddHost, L"Add host"});
    }
    for (int i = 0; i < (int)m_settings.hosts.size(); ++i) {
        const HostEntry& host = m_settings.hosts[i];
        const HostState& st = m_hostState[i];
        bool expanded = m_expandedHost == i && !host.actions.empty();
        {
            Row& r = Push(RowKind::Host, M.host);
            r.host = i;
            r.text = host.name.empty() ? host.address : host.name;
            if (r.text != host.address) r.sub = host.address;
            r.state = !host.ping                    ? theme::Grey
                      : st.state == ping::State::Up   ? theme::Green
                      : st.state == ping::State::Down ? theme::Red
                                                      : theme::Grey;
            r.tip = !host.ping                      ? std::wstring(L"Ping off")
                    : st.state == ping::State::Up   ? L"Reachable, " + std::to_wstring(st.rtt) + L" ms"
                    : st.state == ping::State::Down ? std::wstring(L"Not reachable")
                                                    : std::wstring(L"Waiting for first reply");
            if (!r.sub.empty()) r.tip = host.address + L": " + r.tip;
            r.expanded = expanded;
            r.chevron = !host.actions.empty();
            r.rowCmd = host.actions.empty() ? Cmd::None : Cmd::ExpandHost;
            r.dblCmd = Cmd::EditHost;
            r.hoverButtons = true;
            r.buttons = {{kGlyphAdd, Cmd::AddAction, L"Add action"},
                         {kGlyphEdit, Cmd::EditHost, L"Edit host"},
                         {kGlyphDelete, Cmd::DelHost, L"Remove host"}};
        }
        if (expanded) {
            for (int k = 0; k < (int)host.actions.size(); ++k) {
                Row& r = Push(RowKind::Action, M.action);
                r.host = i;
                r.index = k;
                r.text = actions::Describe(host.actions[k]);
                r.tip = L"Click to connect";
                r.rowCmd = Cmd::RunAction;
                r.buttons = {{kGlyphEdit, Cmd::EditAction, L"Edit action"},
                             {kGlyphDelete, Cmd::DelAction, L"Remove action"}};
            }
        }
    }
    if (m_editTarget.kind == EditKind::NewHost) {
        Row& r = Push(RowKind::Value, M.host);
        r.editKind = EditKind::NewHost;
        r.editSlot = true;
    } else if (m_settings.hosts.empty()) {
        Row& r = Push(RowKind::Hint, M.hint);
        r.text = L"Click + to add a host to ping";
    }
    m_contentH = m_layoutY + M.pad;
}

void MainWindow::AddAdapterDetails(int ai) {
    const Adapter& a = m_adapters[ai];
    const Ipv4Config& c = a.cfg;
    const bool isStatic = !c.dhcp;

    auto toggle = [&](const wchar_t* label, bool on, bool enabled, Cmd cmd) {
        Row& r = Push(RowKind::Toggle, M.toggle);
        r.adapter = ai;
        r.text = label;
        r.on = on;
        r.toggleEnabled = enabled;
        r.toggleCmd = cmd;
    };
    auto caption = [&](const wchar_t* label, bool canAdd, Cmd addCmd, const wchar_t* tip) {
        Row& r = Push(RowKind::Caption, M.caption);
        r.adapter = ai;
        r.text = label;
        if (canAdd) r.buttons.push_back({kGlyphAdd, addCmd, tip});
    };
    auto value = [&](const std::wstring& text, int index, EditKind kind, bool canEdit, Cmd edit, bool canDel,
                     Cmd del) {
        Row& r = Push(RowKind::Value, M.value);
        r.adapter = ai;
        r.index = index;
        r.text = text;
        r.editKind = kind;
        r.editSlot = kind != EditKind::None && m_editTarget.kind == kind && m_editTarget.guid == a.guid &&
                     m_editTarget.index == index;
        if (canEdit) {
            r.dblCmd = edit;
            r.tip = L"Double-click to edit";
            r.buttons.push_back({kGlyphEdit, edit, L"Edit"});
        }
        if (canDel) r.buttons.push_back({kGlyphDelete, del, L"Remove"});
    };
    auto newSlot = [&](EditKind kind) {
        if (m_editTarget.kind == kind && m_editTarget.guid == a.guid) value(L"", -1, kind, false, Cmd::None, false, Cmd::None);
    };
    auto hint = [&](const wchar_t* text) {
        Row& r = Push(RowKind::Hint, M.hint);
        r.adapter = ai;
        r.text = text;
    };

    toggle(L"Enabled", a.enabled, true, Cmd::ToggleEnabled);
    caption(L"Device", false, Cmd::None, nullptr);
    value(a.device, -1, EditKind::None, false, Cmd::None, false, Cmd::None);
    if (!a.enabled) return;
    if (!a.ipv4Enabled) {
        hint(L"IPv4 is not enabled on this adapter");
        return;
    }

    toggle(L"DHCP", c.dhcp, true, Cmd::ToggleDhcp);
    if (c.dhcp) {
        caption(L"DHCP server", false, Cmd::None, nullptr);
        value(a.dhcpServer.empty() ? L"-" : a.dhcpServer, -1, EditKind::None, false, Cmd::None, false, Cmd::None);
    }

    caption(L"IP address / subnet", isStatic, Cmd::AddAddr, L"Add IP address");
    for (int k = 0; k < (int)c.addresses.size(); ++k) {
        // Editable under DHCP too: committing the edit switches the adapter to static.
        value(ip::Format(c.addresses[k]), k, EditKind::Address, true, Cmd::EditAddr, isStatic && k > 0,
              Cmd::DelAddr);
        if (!isStatic) {
            m_rows.back().tip = L"Double-click to edit (switches DHCP off and keeps the other values as static)";
            m_rows.back().buttons.back().tip = L"Edit (switches DHCP off)";
        }
        {
            auto& b = m_rows.back().buttons;
            if (m_scanning && m_scanGuid == a.guid && m_scanIndex == k)
                b.insert(b.begin(), {kGlyphClose, Cmd::StopScan, L"Stop the scan (keeps the hosts found so far)"});
            else
                b.insert(b.begin(), {kGlyphScan, Cmd::ScanSubnet, L"Scan this subnet (ARP, ping or TCP port)"});
        }
    }
    newSlot(EditKind::NewAddress);
    if (!a.linkLocal.empty())  // assigned by Windows itself: shown, not editable
    {
        value(a.linkLocal + L"  (link-local)", -1, EditKind::None, false, Cmd::None, false, Cmd::None);
        m_rows.back().dim = true;
        m_rows.back().tip = L"Assigned by Windows when no other address is reachable";
    }
    if (c.addresses.empty() && a.linkLocal.empty() && m_editTarget.kind != EditKind::NewAddress)
        value(L"-", -1, EditKind::None, false, Cmd::None, false, Cmd::None);

    toggle(L"Auto DNS", c.dhcp && c.autoDns, c.dhcp, Cmd::ToggleAutoDns);
    const bool staticDns = !(c.dhcp && c.autoDns);
    caption(L"DNS servers", staticDns && c.dns.size() < 2, Cmd::AddDns, L"Add DNS server");
    for (int k = 0; k < (int)c.dns.size() && k < 2; ++k)
        value(c.dns[k], k, EditKind::Dns, staticDns, Cmd::EditDns, staticDns, Cmd::DelDns);
    newSlot(EditKind::NewDns);
    if (c.dns.empty() && m_editTarget.kind != EditKind::NewDns)
        value(L"-", -1, EditKind::None, false, Cmd::None, false, Cmd::None);

    caption(L"Gateways", isStatic, Cmd::AddGw, L"Add gateway");
    for (int k = 0; k < (int)c.gateways.size(); ++k)
        value(c.gateways[k], k, EditKind::Gateway, isStatic, Cmd::EditGw, isStatic, Cmd::DelGw);
    newSlot(EditKind::NewGateway);
    if (c.gateways.empty() && m_editTarget.kind != EditKind::NewGateway)
        value(L"-", -1, EditKind::None, false, Cmd::None, false, Cmd::None);

    Push(RowKind::Hint, S(4)).adapter = ai;  // bottom padding of the expanded block
}

int MainWindow::ViewHeight() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    return std::max(0, (int)rc.bottom - M.title - M.bottom);
}

void MainWindow::PlaceButtons() {
    RECT client;
    GetClientRect(m_hwnd, &client);
    const int right = M.width - S(6) - (Scrollable() ? M.scrollW + S(2) : 0);
    for (Row& r : m_rows) {
        int x = right;
        int cy = r.y + r.h / 2;
        if (r.chevron) x -= M.chevron + S(2);
        r.textRightFull = x - S(4);
        if (r.kind == RowKind::Toggle) {
            r.toggleRc = {x - M.switchW, cy - M.switchH / 2, x, cy - M.switchH / 2 + M.switchH};
            x -= M.switchW + S(4);
        }
        for (auto it = r.buttons.rbegin(); it != r.buttons.rend(); ++it) {
            it->rc = {x - M.btn, cy - M.btn / 2, x, cy - M.btn / 2 + M.btn};
            x -= M.btn + S(2);
        }
        r.textRight = x - S(4);
    }

    // Fixed buttons: title close + bottom bar shortcuts.
    m_fixed.clear();
    m_fixed.push_back({kGlyphClose, Cmd::Exit, L"Exit", {M.width - M.title, 0, M.width, M.title}});
    m_fixed.push_back({kGlyphMinimize, Cmd::Minimize, L"Minimize",
                       {M.width - 2 * M.title, 0, M.width - M.title, M.title}});
    const int by = client.bottom - M.bottom;
    const int bw = S(28), bh = S(24);
    const struct { wchar_t g; Cmd c; const wchar_t* tip; } bar[] = {
        {kGlyphNetwork, Cmd::OpenNcpa, L"Network connections (ncpa.cpl)"},
        {kGlyphGlobe, Cmd::FlushDns, L"Flush DNS cache"},
        {kGlyphRefresh, Cmd::RenewDhcp, L"Renew DHCP lease (expanded adapter, or all)"},
        {m_settings.showHidden ? kGlyphView : kGlyphHide, Cmd::ToggleShowHidden,
         m_settings.showHidden ? L"Hidden adapters are shown (click to leave them out)"
                               : L"Hidden adapters are left out (click to show them)"},
    };
    for (int i = 0; i < (int)std::size(bar); ++i) {
        int x = S(6) + i * (bw + S(2));
        int y = by + (M.bottom - bh) / 2;
        m_fixed.push_back({bar[i].g, bar[i].c, bar[i].tip, {x, y, x + bw, y + bh}});
    }
}

void MainWindow::UpdateWindowSize() {
    if (IsIconic(m_hwnd)) return;  // the minimized rect is off screen on purpose
    RECT wr;
    GetWindowRect(m_hwnd, &wr);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& work = mi.rcWork;
    int h = std::min<int>(M.title + m_contentH + M.bottom, work.bottom - work.top);
    int x = wr.left, y = wr.top;
    if (y + h > work.bottom) y = work.bottom - h;  // grow upwards near the taskbar
    if (y < work.top) y = work.top;
    if (x + M.width > work.right) x = work.right - M.width;
    if (x < work.left) x = work.left;
    if (wr.right - wr.left != M.width || wr.bottom - wr.top != h || x != wr.left || y != wr.top)
        SetWindowPos(m_hwnd, nullptr, x, y, M.width, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::ScrollTo(int y) {
    int maxScroll = std::max(0, m_contentH - ViewHeight());
    y = std::clamp(y, 0, maxScroll);
    if (y != m_scroll) {
        m_scroll = y;
        PositionEdit();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::EnsureVisible(int row) {
    if (row < 0 || row >= (int)m_rows.size()) return;
    const Row& r = m_rows[row];
    int view = ViewHeight();
    if (r.y < m_scroll) ScrollTo(r.y);
    else if (r.y + r.h > m_scroll + view) ScrollTo(r.y + r.h - view);
}

void MainWindow::Relayout() {
    if (!m_hwnd) return;
    BuildLayout();
    UpdateWindowSize();
    PlaceButtons();
    m_scroll = std::clamp(m_scroll, 0, std::max(0, m_contentH - ViewHeight()));
    PositionEdit();
    // Row indices changed; recompute what is under the mouse.
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_hover = (m_trackingMouse && PtInRect(&rc, pt)) ? HitTest(pt) : Hit{};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =========================================================================
// painting
// =========================================================================

void MainWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, m_font);

    Fill(dc, client, theme::Bg);

    // Content (clipped between title and bottom bar).
    const int top = M.title, bottom = client.bottom - M.bottom;
    IntersectClipRect(dc, 0, top, client.right, bottom);
    const int dy = top - m_scroll;
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if (r.y + r.h + dy < top || r.y + dy > bottom) continue;
        PaintRow(dc, r, i, dy);
    }
    if (Scrollable()) {
        int view = bottom - top;
        int th = std::max(S(20), view * view / std::max(1, m_contentH));
        int ty = top + (int)((long long)(view - th) * m_scroll / std::max(1, m_contentH - view));
        RECT thumb{client.right - M.scrollW - S(2), ty, client.right - S(2), ty + th};
        Fill(dc, thumb, m_hover.scrollbar || m_dragScroll ? theme::TextDim : theme::Border);
    }
    SelectClipRgn(dc, nullptr);
    PaintToast(dc, bottom, client.right);

    // Title strip.
    RECT title{0, 0, client.right, M.title};
    Fill(dc, title, theme::Bg);
    RECT tt{M.pad, 0, client.right - 2 * M.title, M.title};
    DrawStr(dc, m_fontBold, L"NIC Manager", tt, theme::TextDim);
    if (!m_elevated) {
        SIZE sz;
        SelectObject(dc, m_fontBold);
        GetTextExtentPoint32W(dc, L"NIC Manager", 11, &sz);
        RECT nr{tt.left + sz.cx + S(6), 0, tt.right, M.title};
        DrawStr(dc, m_fontSmall, L"not elevated", nr, theme::Red);
    }

    // Bottom bar.
    RECT bar{0, bottom, client.right, client.bottom};
    Fill(dc, bar, theme::Bg);
    RECT line{0, bottom, client.right, bottom + 1};
    Fill(dc, line, theme::Border);
    for (int i = 0; i < (int)m_fixed.size(); ++i)
        PaintButton(dc, m_fixed[i], m_fixed[i].rc, m_hover.fixed == i && !(m_busy && IsBlockedWhileBusy(m_fixed[i].cmd)), m_fontIcon);
    int sx = m_fixed.empty() ? M.pad : m_fixed.back().rc.right + S(8);
    RECT status{sx, bottom + 1, client.right - S(6), client.bottom};
    if (m_busy) {
        PaintSpinner(dc, sx + S(7), (bottom + client.bottom) / 2);
        status.left += S(18);
        DrawStr(dc, m_fontSmall, m_busyLabel, status, theme::Text);
    } else if (!m_status.empty()) {
        DrawStr(dc, m_fontSmall, m_status, status, m_statusColor);
    }

    // 1px frame.
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, theme::Border);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, client.right, client.bottom);

    BitBlt(hdc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(m_hwnd, &ps);
}

void MainWindow::PaintRow(HDC dc, const Row& r, int ri, int dy) {
    RECT rc{1, r.y + dy, M.width - 1, r.y + r.h + dy};
    const bool hover = m_hover.row == ri && !m_busy;
    const bool inDetail = r.adapter >= 0 && r.kind != RowKind::Adapter;
    auto paintButtons = [&] {
        for (int k = 0; k < (int)r.buttons.size(); ++k)
            PaintButton(dc, r.buttons[k], Offset(r.buttons[k].rc, dy), hover && m_hover.button == k, m_fontIcon);
    };

    switch (r.kind) {
    case RowKind::Header: {
        RECT t{M.pad, rc.top + S(4), r.textRight, rc.bottom};
        DrawStr(dc, m_fontSmall, r.text, t, theme::TextDim);
        paintButtons();
        break;
    }
    case RowKind::Adapter:
    case RowKind::Host: {
        RECT body = rc;
        body.bottom -= 1;  // 1px gap between entries
        COLORREF tint = r.state == theme::Green ? theme::GreenTint : r.state == theme::Red ? theme::RedTint
                                                                                           : theme::Surface;
        COLORREF bg = (hover || r.expanded) ? theme::SurfaceHover : Lerp(theme::Surface, tint, 0.55);
        Fill(dc, body, bg);
        RECT stripe{body.left, body.top, body.left + M.bar, body.bottom};
        Fill(dc, stripe, r.state);
        const bool showButtons = !r.editSlot && (!r.hoverButtons || hover || r.expanded);
        const int textRight = showButtons ? r.textRight : r.textRightFull;
        const int x = M.bar + M.pad;
        if (r.kind == RowKind::Adapter) {
            int mid = body.top + (body.bottom - body.top) / 2;
            RECT n{x, body.top + S(2), textRight, mid + S(1)};
            RECT s{x, mid, textRight, body.bottom - S(3)};
            DrawStr(dc, m_fontBold, r.text, n, r.dim ? theme::TextDisabled : theme::Text,
                    DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS);
            DrawStr(dc, m_fontSmall, r.sub, s, r.dim ? theme::TextDisabled : theme::TextDim,
                    DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
        } else if (!r.editSlot) {
            RECT t{x, body.top, textRight, body.bottom};
            DrawStr(dc, m_font, r.text, t, theme::Text);
            if (!r.sub.empty()) {  // the address next to a custom name
                SIZE sz{};
                SelectObject(dc, m_font);
                GetTextExtentPoint32W(dc, r.text.c_str(), (int)r.text.size(), &sz);
                RECT a{x + sz.cx + S(8), body.top, textRight, body.bottom};
                if (a.left < a.right) DrawStr(dc, m_fontSmall, r.sub, a, theme::TextDim);
            }
        }
        if (r.chevron) {
            RECT c{r.textRightFull + S(4), body.top, r.textRightFull + S(4) + M.chevron + S(2), body.bottom};
            DrawGlyph(dc, m_fontIcon, r.expanded ? kGlyphChevronDown : kGlyphChevronRight, c, theme::TextDim);
        }
        if (showButtons) paintButtons();
        break;
    }
    case RowKind::Toggle: {
        Fill(dc, rc, theme::Detail);
        RECT t{M.indent, rc.top, r.textRight, rc.bottom};
        DrawStr(dc, m_font, r.text, t, r.toggleEnabled ? theme::Text : theme::TextDisabled);
        PaintToggle(dc, Offset(r.toggleRc, dy), r.on, r.toggleEnabled);
        break;
    }
    case RowKind::Caption: {
        Fill(dc, rc, theme::Detail);
        RECT t{M.indent, rc.top + S(2), r.textRight, rc.bottom};
        DrawStr(dc, m_fontSmall, r.text, t, theme::TextDim);
        paintButtons();
        break;
    }
    case RowKind::Value: {
        Fill(dc, rc, r.editKind == EditKind::NewHost ? theme::Surface : theme::Detail);
        if (r.editSlot) break;
        if (hover && r.dblCmd != Cmd::None) {
            RECT hl{M.indent - S(4), rc.top + 1, r.textRightFull + S(4), rc.bottom - 1};
            Fill(dc, hl, Lerp(theme::Detail, theme::SurfaceHover, 0.6));
        }
        RECT t{M.indent + S(6), rc.top, r.textRight, rc.bottom};
        DrawStr(dc, m_font, r.text, t, r.text == L"-" || r.dim ? theme::TextDim : theme::Text);
        paintButtons();
        break;
    }
    case RowKind::Action: {
        Fill(dc, rc, hover ? Lerp(theme::Detail, theme::SurfaceHover, 0.6) : theme::Detail);
        RECT g{M.indent - S(4), rc.top, M.indent + S(12), rc.bottom};
        DrawGlyph(dc, m_fontIcon, kGlyphPlay, g, theme::Accent);
        RECT t{M.indent + S(14), rc.top, r.textRight, rc.bottom};
        DrawStr(dc, m_font, r.text, t, theme::Text);
        paintButtons();
        break;
    }
    case RowKind::Hint: {
        Fill(dc, rc, inDetail ? theme::Detail : theme::Bg);
        RECT t{inDetail ? M.indent : M.pad, rc.top, M.width - M.pad, rc.bottom};
        DrawStr(dc, m_fontSmall, r.text, t, theme::TextDim);
        break;
    }
    }
}

void MainWindow::PaintButton(HDC dc, const Button& b, RECT rc, bool hover, HFONT font) {
    if (hover) Fill(dc, rc, RGB(0x3C, 0x40, 0x46));
    COLORREF c = hover ? (b.cmd == Cmd::DelHost || b.cmd == Cmd::DelAction || b.cmd == Cmd::DelAddr ||
                                  b.cmd == Cmd::DelDns || b.cmd == Cmd::DelGw
                              ? theme::Red
                              : theme::Text)
                       : theme::TextDim;
    if (m_busy && IsBlockedWhileBusy(b.cmd)) c = theme::TextDisabled;
    DrawGlyph(dc, font, b.glyph, rc, c);
}

void MainWindow::PaintToggle(HDC dc, RECT rc, bool on, bool enabled) {
    COLORREF track = on ? theme::Accent : theme::ToggleOff;
    COLORREF knob = RGB(0xF2, 0xF2, 0xF2);
    if (!enabled || m_busy) {
        track = Lerp(theme::Detail, track, 0.45);
        knob = Lerp(theme::Detail, knob, 0.45);
    }
    int h = rc.bottom - rc.top;
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, track);
    SetDCBrushColor(dc, track);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, h, h);
    int d = h - S(4);
    int kx = on ? rc.right - S(2) - d : rc.left + S(2);
    SetDCPenColor(dc, knob);
    SetDCBrushColor(dc, knob);
    Ellipse(dc, kx, rc.top + S(2), kx + d, rc.top + S(2) + d);
}

void MainWindow::PaintSpinner(HDC dc, int cx, int cy) {
    const int radius = S(6), dot = std::max(2, S(3));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    for (int k = 0; k < 8; ++k) {
        double angle = k * 3.14159265358979 / 4;
        int age = (m_spin - k + 8) % 8;  // 0 = newest
        SetDCBrushColor(dc, Lerp(theme::Accent, theme::Bg, age / 8.0));
        int x = cx + (int)(radius * std::cos(angle)), y = cy + (int)(radius * std::sin(angle));
        Ellipse(dc, x - dot / 2, y - dot / 2, x - dot / 2 + dot + 1, y - dot / 2 + dot + 1);
    }
}

// =========================================================================
// input
// =========================================================================

MainWindow::Hit MainWindow::HitTest(POINT pt) const {
    Hit h;
    for (int i = 0; i < (int)m_fixed.size(); ++i)
        if (PtInRect(&m_fixed[i].rc, pt)) {
            h.fixed = i;
            return h;
        }
    if (!m_toastTitle.empty() && PtInRect(&m_toastRc, pt)) {
        h.toast = PtInRect(&m_toastCopyRc, pt) ? 2 : 1;
        return h;
    }
    RECT client;
    GetClientRect(m_hwnd, &client);
    if (pt.y < M.title || pt.y >= client.bottom - M.bottom) return h;
    if (Scrollable() && pt.x >= M.width - M.scrollW - S(6)) {
        h.scrollbar = true;
        return h;
    }
    POINT c{pt.x, pt.y - M.title + m_scroll};
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if (c.y < r.y || c.y >= r.y + r.h) continue;
        h.row = i;
        if (!r.editSlot)
            for (int k = 0; k < (int)r.buttons.size(); ++k)
                if (PtInRect(&r.buttons[k].rc, c)) h.button = k;
        if (r.kind == RowKind::Toggle) {
            RECT t = r.toggleRc;
            InflateRect(&t, S(4), S(4));
            h.toggle = PtInRect(&t, c) != FALSE;
        }
        break;
    }
    return h;
}

void MainWindow::OnMouseMove(POINT pt) {
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }
    if (m_dragScroll) {
        int view = ViewHeight();
        int track = std::max(1, view - std::max(S(20), view * view / std::max(1, m_contentH)));
        ScrollTo(m_dragStartScroll + (pt.y - m_dragStartY) * (m_contentH - view) / track);
        return;
    }
    Hit h = HitTest(pt);
    if (h.fixed != m_hover.fixed || h.row != m_hover.row || h.button != m_hover.button ||
        h.toggle != m_hover.toggle || h.scrollbar != m_hover.scrollbar) {
        m_hover = h;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    std::wstring tip;
    if (h.fixed >= 0) tip = m_fixed[h.fixed].tip;
    else if (h.row >= 0 && h.button >= 0) tip = m_rows[h.row].buttons[h.button].tip;
    else if (h.row >= 0 && !h.toggle) tip = m_rows[h.row].tip;
    SetTip(tip);
}

void MainWindow::SetTip(const std::wstring& tip) {
    if (tip == m_tipText) return;
    m_tipText = tip;
    TOOLINFOW ti{sizeof(ti)};
    ti.hwnd = m_hwnd;
    ti.uId = reinterpret_cast<UINT_PTR>(m_hwnd);
    ti.lpszText = const_cast<wchar_t*>(m_tipText.c_str());
    SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    if (m_tipText.empty()) SendMessageW(m_tooltip, TTM_POP, 0, 0);
}

// The title strip is HTCAPTION (for dragging), so moves there arrive as
// non-client messages that the subclassed tooltip never sees. Relay them as
// client moves so the version tip shows like any other.
void MainWindow::OnTitleMouseMove(POINT screenPt) {
    if (!m_trackingNc) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, m_hwnd, 0};
        TrackMouseEvent(&tme);
        m_trackingNc = true;
    }
    SetTip(L"NIC Manager " NICMGR_VERSION_WSTR);
    POINT pt = screenPt;
    ScreenToClient(m_hwnd, &pt);
    MSG m{m_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y), static_cast<DWORD>(GetMessageTime()), screenPt};
    SendMessageW(m_tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&m));
}

void MainWindow::OnLButtonDown(POINT pt, bool dbl) {
    if (m_edit) {  // any click outside the inline editor cancels it
        EndEdit();
        return;
    }
    Hit h = HitTest(pt);
    if (h.toast == 2) {
        CopyToClipboard(m_hwnd, m_toastDetails);
        SetStatus(L"Details copied", theme::TextDim, 2000);
        return;
    }
    if (h.toast) {
        HideToast();
        return;
    }
    if (h.scrollbar) {
        m_dragScroll = true;
        m_dragStartY = pt.y;
        m_dragStartScroll = m_scroll;
        SetCapture(m_hwnd);
        return;
    }
    if (dbl && h.row >= 0 && h.button < 0 && m_rows[h.row].dblCmd != Cmd::None) {
        m_press = {};
        Row row = m_rows[h.row];
        Execute(&row, row.dblCmd);
        return;
    }
    m_press = h;
    SetCapture(m_hwnd);
}

void MainWindow::OnLButtonUp(POINT pt) {
    ReleaseCapture();
    if (m_dragScroll) {
        m_dragScroll = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    Hit h = HitTest(pt);
    Hit p = m_press;
    m_press = {};
    if (h.fixed != p.fixed || h.row != p.row || h.button != p.button || h.toggle != p.toggle) return;
    if (h.fixed >= 0) {
        Execute(nullptr, m_fixed[h.fixed].cmd);
        return;
    }
    if (h.row < 0) return;
    Row row = m_rows[h.row];  // copy: Execute may rebuild m_rows
    if (h.button >= 0) Execute(&row, row.buttons[h.button].cmd);
    else if (h.toggle && row.toggleEnabled) Execute(&row, row.toggleCmd);
    else if (row.rowCmd != Cmd::None) Execute(&row, row.rowCmd);
}

void MainWindow::OnRButtonUp(POINT pt) {
    Hit h = HitTest(pt);
    if (h.row < 0) return;
    const Row& r = m_rows[h.row];
    POINT screen = pt;
    ClientToScreen(m_hwnd, &screen);
    if (r.kind == RowKind::Adapter) ShowAdapterMenu(r.adapter, screen);
    else if (r.kind == RowKind::Host) ShowHostMenu(h.row, screen);
}

bool MainWindow::IsBlockedWhileBusy(Cmd cmd) const {
    return cmd != Cmd::ExpandAdapter && cmd != Cmd::ExpandHost && cmd != Cmd::Hide && cmd != Cmd::Exit &&
           cmd != Cmd::Minimize && cmd != Cmd::OpenWifi && cmd != Cmd::OpenNcpa && cmd != Cmd::StopScan;
}

void MainWindow::Execute(const Row* rowp, Cmd cmd) {
    Row row = rowp ? *rowp : Row{};
    if (m_busy && IsBlockedWhileBusy(cmd)) {
        MessageBeep(MB_OK);
        return;
    }
    const Adapter* a = row.adapter >= 0 && row.adapter < (int)m_adapters.size() ? &m_adapters[row.adapter] : nullptr;
    HostEntry* host = row.host >= 0 && row.host < (int)m_settings.hosts.size() ? &m_settings.hosts[row.host] : nullptr;
    const int idx = row.index;
    Ipv4Config c = a ? a->cfg : Ipv4Config{};

    switch (cmd) {
    case Cmd::None: break;
    case Cmd::ExpandAdapter:
        if (!a) break;
        m_expandedAdapter = m_expandedAdapter == a->guid ? L"" : a->guid;  // only one open at a time
        settings::SaveExpandedAdapter(m_expandedAdapter);
        Relayout();
        for (int i = 0; i < (int)m_rows.size(); ++i)
            if (m_rows[i].kind == RowKind::Adapter && m_rows[i].adapter == row.adapter) {
                int last = i;
                while (last + 1 < (int)m_rows.size() && m_rows[last + 1].adapter == row.adapter) ++last;
                EnsureVisible(last);
                EnsureVisible(i);
            }
        break;
    case Cmd::ExpandHost:
        if (!host || host->actions.empty()) break;
        m_expandedHost = m_expandedHost == row.host ? -1 : row.host;
        Relayout();
        break;

    case Cmd::ToggleEnabled: {
        if (!a) break;
        std::wstring name = a->name;
        bool enable = !a->enabled;
        RunJob(enable ? L"Enabling adapter" : L"Disabling adapter",
               [name, enable](const netcmd::Deadline& dl) { return netcmd::SetAdapterEnabled(name, enable, dl); });
        break;
    }
    case Cmd::ToggleDhcp:
        if (!a) break;
        if (c.dhcp) {
            if (c.addresses.empty()) {  // nothing to keep: ask for the first static address
                BeginEdit(EditKind::NewAddress, row.adapter, -1, L"", true);
                break;
            }
            c.dhcp = false;  // keep the currently assigned values as static configuration
            c.autoDns = false;
        } else {
            c = Ipv4Config{};  // DHCP for addresses and DNS
        }
        ApplyConfig(*a, c);
        break;
    case Cmd::ToggleAutoDns:
        if (!a || !c.dhcp) break;
        c.autoDns = !c.autoDns;  // switching off keeps the current servers as static ones
        ApplyConfig(*a, c);
        break;

    case Cmd::EditAddr:
        if (a && idx >= 0 && idx < (int)c.addresses.size())
            BeginEdit(EditKind::Address, row.adapter, idx, ip::Format(c.addresses[idx]), c.dhcp);
        break;
    case Cmd::DelAddr:
        if (a && idx > 0 && idx < (int)c.addresses.size()) {
            c.addresses.erase(c.addresses.begin() + idx);
            ApplyConfig(*a, c);
        }
        break;
    case Cmd::AddAddr: if (a) BeginEdit(EditKind::NewAddress, row.adapter, -1, L""); break;
    case Cmd::EditGw:
        if (a && idx >= 0 && idx < (int)c.gateways.size()) BeginEdit(EditKind::Gateway, row.adapter, idx, c.gateways[idx]);
        break;
    case Cmd::DelGw:
        if (a && idx >= 0 && idx < (int)c.gateways.size()) {
            c.gateways.erase(c.gateways.begin() + idx);
            ApplyConfig(*a, c);
        }
        break;
    case Cmd::AddGw: if (a) BeginEdit(EditKind::NewGateway, row.adapter, -1, L""); break;
    case Cmd::EditDns:
        if (a && idx >= 0 && idx < (int)c.dns.size()) BeginEdit(EditKind::Dns, row.adapter, idx, c.dns[idx]);
        break;
    case Cmd::DelDns:
        if (a && idx >= 0 && idx < (int)c.dns.size()) {
            c.dns.erase(c.dns.begin() + idx);
            c.autoDns = false;
            ApplyConfig(*a, c);
        }
        break;
    case Cmd::AddDns: if (a) BeginEdit(EditKind::NewDns, row.adapter, -1, L""); break;

    case Cmd::AddHost: BeginEdit(EditKind::NewHost, -1, -1, L""); break;
    case Cmd::EditHost:
        if (host) {
            std::wstring name = host->name, address = host->address;
            bool ping = host->ping;
            if (dialogs::EditHost(m_hwnd, name, address, ping)) {
                HostEntry& h = m_settings.hosts[row.host];
                h.name = name;
                if (h.address != address) {
                    h.address = address;
                    m_hostState[row.host] = {m_nextHostId++};  // new id restarts the ping worker
                }
                SetHostPing(row.host, ping);
                settings::SaveHosts(m_settings.hosts);
                SyncPinger();
                Relayout();
            }
        }
        break;
    case Cmd::DelHost: {
        if (!host) break;
        if (!host->actions.empty() &&
            MessageBoxW(m_hwnd, (L"Remove " + host->name + L" and its actions?").c_str(), L"NIC Manager",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            break;
        m_settings.hosts.erase(m_settings.hosts.begin() + row.host);
        m_hostState.erase(m_hostState.begin() + row.host);
        if (m_expandedHost == row.host) m_expandedHost = -1;
        else if (m_expandedHost > row.host) --m_expandedHost;
        settings::SaveHosts(m_settings.hosts);
        SyncPinger();
        Relayout();
        break;
    }
    case Cmd::AddAction: {
        if (!host) break;
        HostAction act;
        if (EditActionDialog(act, host->address, true)) {
            m_settings.hosts[row.host].actions.push_back(act);
            m_expandedHost = row.host;
            settings::SaveHosts(m_settings.hosts);
            Relayout();
        }
        break;
    }
    case Cmd::RunAction:
        if (host && idx >= 0 && idx < (int)host->actions.size()) {
            const HostAction& act = host->actions[idx];
            std::wstring err = actions::Run(*host, act, m_settings.tightVncPath);
            if (!err.empty()) ShowToast(std::wstring(actions::TypeName(act)) + L" could not start", err, L"");
        }
        break;
    case Cmd::EditAction:
        if (host && idx >= 0 && idx < (int)host->actions.size()) {
            HostAction act = host->actions[idx];
            if (EditActionDialog(act, host->address, false)) {
                m_settings.hosts[row.host].actions[idx] = act;
                settings::SaveHosts(m_settings.hosts);
                Relayout();
            }
        }
        break;
    case Cmd::DelAction:
        if (host && idx >= 0 && idx < (int)host->actions.size()) {
            host->actions.erase(host->actions.begin() + idx);
            if (host->actions.empty() && m_expandedHost == row.host) m_expandedHost = -1;
            settings::SaveHosts(m_settings.hosts);
            Relayout();
        }
        break;

    case Cmd::OpenNcpa: netcmd::OpenNetworkConnections(); break;
    case Cmd::OpenWifi: netcmd::OpenWifiPicker(); break;
    case Cmd::ScanSubnet:
        if (a) ShowScanMenu(*a, idx);
        break;
    case Cmd::StopScan:
        if (m_scanning && m_scanCancel) {
            *m_scanCancel = true;
            m_busyLabel = L"Stopping scan...";
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;
    case Cmd::FlushDns:
        RunJob(L"Flushing DNS cache", [](const netcmd::Deadline& dl) { return netcmd::FlushDns(dl); });
        break;
    case Cmd::RenewDhcp: {
        std::wstring name;
        if (const Adapter* ex = FindAdapter(m_expandedAdapter)) name = ex->name;
        RunJob(name.empty() ? L"Renewing all DHCP leases" : L"Renewing DHCP lease",
               [name](const netcmd::Deadline& dl) { return netcmd::RenewDhcp(name, dl); });
        break;
    }
    case Cmd::Hide: ShowWindow(m_hwnd, SW_HIDE); break;
    case Cmd::Exit: DestroyWindow(m_hwnd); break;
    case Cmd::ToggleShowHidden:
        m_settings.showHidden = !m_settings.showHidden;
        settings::SaveGeneral(m_settings);
        Relayout();
        break;
    case Cmd::Minimize: ShowWindow(m_hwnd, m_settings.minimizeToTray ? SW_HIDE : SW_MINIMIZE); break;
    }
}

// =========================================================================
// inline editing
// =========================================================================

const Adapter* MainWindow::FindAdapter(const std::wstring& guid) const {
    if (guid.empty()) return nullptr;
    for (const auto& a : m_adapters)
        if (a.guid == guid) return &a;
    return nullptr;
}

LRESULT CALLBACK MainWindow::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<MainWindow*>(ref);
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) {
            PostMessageW(self->m_hwnd, WM_APP_EDITKEY, wp == VK_RETURN ? 1 : 0, self->m_editGen);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == 27) return 0;  // no beep
        break;
    case WM_KILLFOCUS:
        if (!self->m_endingEdit) PostMessageW(self->m_hwnd, WM_APP_EDITKEY, 0, self->m_editGen);
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 1); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void MainWindow::BeginEdit(EditKind kind, int adapterOrHost, int index, const std::wstring& text, bool fromDhcp) {
    EndEdit();
    m_editTarget = {};
    m_editTarget.kind = kind;
    m_editTarget.index = index;
    m_editTarget.fromDhcp = fromDhcp;
    if (kind == EditKind::Host || kind == EditKind::NewHost) m_editTarget.host = adapterOrHost;
    else if (adapterOrHost >= 0 && adapterOrHost < (int)m_adapters.size())
        m_editTarget.guid = m_adapters[adapterOrHost].guid;
    ++m_editGen;

    m_edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd,
                             reinterpret_cast<HMENU>((INT_PTR)kEditId), m_inst, nullptr);
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), FALSE);
    SendMessageW(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(4), S(4)));
    const wchar_t* cue = kind == EditKind::Address || kind == EditKind::NewAddress ? L"192.168.1.10/24"
                         : kind == EditKind::Host || kind == EditKind::NewHost  ? L"IP or host name"
                                                                                : L"192.168.1.1";
    SendMessageW(m_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
    SetWindowSubclass(m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));

    Relayout();
    for (int i = 0; i < (int)m_rows.size(); ++i)
        if (m_rows[i].editSlot) EnsureVisible(i);
    PositionEdit();
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
}

void MainWindow::PositionEdit() {
    if (!m_edit) return;
    RECT client;
    GetClientRect(m_hwnd, &client);
    for (const Row& r : m_rows) {
        if (!r.editSlot) continue;
        int left = r.kind == RowKind::Host || r.editKind == EditKind::NewHost ? M.bar + M.pad - S(4) : M.indent;
        int top = r.y + M.title - m_scroll + (r.h - M.editH) / 2;
        bool visible = top >= M.title && top + M.editH <= client.bottom - M.bottom;
        SetWindowPos(m_edit, nullptr, left, top, r.textRightFull - left + S(4), M.editH,
                     SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        return;
    }
    ShowWindow(m_edit, SW_HIDE);
}

void MainWindow::CommitEdit() {
    if (!m_edit) return;
    const std::wstring text = Trim(GetWindowTextStr(m_edit));
    const EditTarget t = m_editTarget;
    auto invalid = [&](const std::wstring& why) {
        SetStatus(why, theme::Red);
        MessageBeep(MB_ICONWARNING);
        SetFocus(m_edit);
    };

    if (t.kind == EditKind::Host || t.kind == EditKind::NewHost) {
        if (!ip::IsValidHost(text)) return invalid(L"Enter an IPv4 address or host name");
        if (t.kind == EditKind::NewHost) {
            m_settings.hosts.push_back({text, text, {}});
            m_hostState.push_back({m_nextHostId++});
        } else if (t.host >= 0 && t.host < (int)m_settings.hosts.size() && m_settings.hosts[t.host].address != text) {
            m_settings.hosts[t.host].address = text;
            m_hostState[t.host] = {m_nextHostId++};  // new id restarts the ping worker
        }
        settings::SaveHosts(m_settings.hosts);
        EndEdit();
        SyncPinger();
        return;
    }

    const Adapter* a = FindAdapter(t.guid);
    if (!a) {
        EndEdit();
        return;
    }
    Ipv4Config c = a->cfg;
    if (t.fromDhcp) {  // keep the leased values (other addresses, gateway, DNS) as static configuration
        c.dhcp = false;
        c.autoDns = false;
    }
    auto replaceOrAdd = [&](std::vector<std::wstring>& list, bool isNew, const std::wstring& v) {
        if (isNew) list.push_back(v);
        else if (t.index >= 0 && t.index < (int)list.size()) list[t.index] = v;
    };
    switch (t.kind) {
    case EditKind::Address:
    case EditKind::NewAddress: {
        IpEntry e;
        std::wstring err;
        if (!ip::ParseAddressInput(text, &e, &err)) return invalid(err);
        if (t.kind == EditKind::NewAddress) {
            for (const auto& x : c.addresses)
                if (x.ip == e.ip) return invalid(L"Address already configured");
            c.addresses.push_back(e);
        } else if (t.index >= 0 && t.index < (int)c.addresses.size()) {
            c.addresses[t.index] = e;
        }
        break;
    }
    case EditKind::Gateway:
    case EditKind::NewGateway:
        if (!ip::ParseIpv4(text)) return invalid(L"Invalid gateway address");
        replaceOrAdd(c.gateways, t.kind == EditKind::NewGateway, text);
        break;
    case EditKind::Dns:
    case EditKind::NewDns:
        if (!ip::ParseIpv4(text)) return invalid(L"Invalid DNS server address");
        if (t.kind == EditKind::NewDns && c.dns.size() >= 2) return invalid(L"Two DNS servers at most");
        replaceOrAdd(c.dns, t.kind == EditKind::NewDns, text);
        c.autoDns = false;
        break;
    default: break;
    }
    Adapter target = *a;  // EndEdit may swap in a newer adapter snapshot
    EndEdit();
    ApplyConfig(target, c);
}

void MainWindow::EndEdit() {
    m_editTarget = {};
    if (m_edit) {
        m_endingEdit = true;
        HWND e = m_edit;
        m_edit = nullptr;
        DestroyWindow(e);
        m_endingEdit = false;
        SetFocus(m_hwnd);
    }
    if (m_pendingAdapters) {
        m_adapters = std::move(*m_pendingAdapters);
        m_pendingAdapters.reset();
    }
    Relayout();
}

// =========================================================================
// operations
// =========================================================================

void MainWindow::ApplyConfig(const Adapter& a, const Ipv4Config& cfg) {
    if (cfg == a.cfg) return;
    std::wstring name = a.name;
    Ipv4Config prev = a.cfg;
    RunJob(L"Applying IPv4 settings", [name, prev, cfg](const netcmd::Deadline& dl) {
        return netcmd::ApplyIpv4(name, prev, cfg, dl);
    });
}

void MainWindow::RunJob(const std::wstring& label, std::function<netcmd::JobResult(const netcmd::Deadline&)> fn,
                        JobOptions opts) {
    if (m_busy) {
        MessageBeep(MB_OK);
        return;
    }
    if (m_job.joinable()) m_job.join();
    m_busy = true;
    m_busyBase = label;
    m_busyLabel = label + L"...";
    m_status.clear();
    SetTimer(m_hwnd, kTimerSpin, 80, nullptr);
    const DWORD timeoutMs = opts.timeoutMs;
    if (m_demo && opts.simulateInDemo)
        fn = [](const netcmd::Deadline&) {  // simulate: show the busy state, change nothing
            Sleep(1500);
            return netcmd::JobResult{};
        };
    HWND hwnd = m_hwnd;
    m_jobOpts = std::move(opts);
    m_job = std::thread([hwnd, timeoutMs, fn = std::move(fn)] {
        auto result = std::make_unique<netcmd::JobResult>(fn(netcmd::Deadline::In(timeoutMs)));
        if (PostMessageW(hwnd, WM_APP_JOBDONE, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnJobDone(netcmd::JobResult* raw) {
    std::unique_ptr<netcmd::JobResult> result(raw);
    if (m_job.joinable()) m_job.join();
    m_busy = false;
    KillTimer(m_hwnd, kTimerSpin);
    m_monitor.Trigger();
    JobOptions opts = std::move(m_jobOpts);
    m_jobOpts = {};
    if (m_scanning) {
        m_scanning = false;
        m_scanCancel.reset();
        Relayout();  // stop button back to scan button
    }
    if (result->ok) {
        SetStatus(L"Done", theme::Green, 2500);
        if (opts.onSuccess) opts.onSuccess();
    } else {
        SetStatus(L"Failed", theme::Red);
        const std::wstring& what = m_busyBase;
        std::wstring text = result->error.empty() ? L"The command failed." : result->error;
        if (!m_elevated && opts.adminHint) text += L" NIC Manager is not running as administrator.";
        ShowToast(what + L" failed", text, result->log);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnAdapters(std::vector<Adapter>* raw) {
    std::unique_ptr<std::vector<Adapter>> list(raw);
    m_adaptersLoaded = true;
    if (m_edit) {  // don't move rows under the user's cursor while editing
        m_pendingAdapters = std::move(list);
        return;
    }
    m_adapters = std::move(*list);
    if (!FindAdapter(m_expandedAdapter)) m_expandedAdapter.clear();
    Relayout();
}

void MainWindow::OnPing(uint32_t id, ping::State state, int rtt) {
    for (auto& h : m_hostState) {
        if (h.id != id) continue;
        h.rtt = rtt;
        if (h.state != state) {
            h.state = state;
            if (!m_edit) Relayout();
            else InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (!m_edit) {
            BuildLayout();  // refresh the rtt tooltip text without resizing
            PlaceButtons();
        }
        return;
    }
}

void MainWindow::SyncPinger() {
    std::vector<std::pair<uint32_t, std::wstring>> list;
    for (size_t i = 0; i < m_settings.hosts.size(); ++i)
        if (m_settings.hosts[i].ping) list.emplace_back(m_hostState[i].id, m_settings.hosts[i].address);
    m_pinger.Sync(list);
}

bool MainWindow::EditActionDialog(HostAction& act, const std::wstring& hostAddress, bool isNew) {
    std::wstring vnc = m_settings.tightVncPath;
    if (!dialogs::EditAction(m_hwnd, act, hostAddress, isNew, vnc)) return false;
    if (vnc != m_settings.tightVncPath) {
        m_settings.tightVncPath = vnc;
        settings::SaveTightVncPath(vnc);
    }
    return true;
}

void MainWindow::ShowToast(const std::wstring& title, const std::wstring& text, const std::wstring& details) {
    m_toastTitle = title;
    m_toastText = text;
    m_toastDetails = details;
    SetTimer(m_hwnd, kTimerToast, kToastMs, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::HideToast() {
    KillTimer(m_hwnd, kTimerToast);
    m_toastTitle.clear();
    m_toastRc = m_toastCopyRc = {};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::PaintToast(HDC dc, int bottom, int right) {
    if (m_toastTitle.empty()) return;
    const int pad = S(8), icon = S(16), btn = M.btn;
    const int left = S(6), textLeft = left + S(3) + pad + icon + S(6), textRight = right - S(6) - pad - btn;
    // Message wraps to at most four lines.
    SelectObject(dc, m_fontSmall);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    RECT calc{0, 0, std::max(1, textRight - textLeft), 0};
    DrawTextW(dc, m_toastText.c_str(), (int)m_toastText.size(), &calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    const int textH = std::min<int>(calc.bottom, tm.tmHeight * 4);
    const int titleH = S(18);
    const int h = pad + titleH + textH + pad;
    m_toastRc = {left, bottom - S(6) - h, right - S(6), bottom - S(6)};
    m_toastCopyRc = {m_toastRc.right - S(4) - btn, m_toastRc.top + S(4), m_toastRc.right - S(4), m_toastRc.top + S(4) + btn};

    Fill(dc, m_toastRc, theme::RedTint);
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, theme::Red);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, m_toastRc.left, m_toastRc.top, m_toastRc.right, m_toastRc.bottom);
    RECT barRc{m_toastRc.left, m_toastRc.top, m_toastRc.left + S(3), m_toastRc.bottom};
    Fill(dc, barRc, theme::Red);
    RECT iconRc{m_toastRc.left + S(3) + pad, m_toastRc.top + pad, m_toastRc.left + S(3) + pad + icon,
                m_toastRc.top + pad + titleH};
    DrawGlyph(dc, m_fontIcon, kGlyphError, iconRc, theme::Red);
    RECT titleRc{textLeft, m_toastRc.top + pad, textRight, m_toastRc.top + pad + titleH};
    DrawStr(dc, m_fontBold, m_toastTitle, titleRc, theme::Text);
    RECT textRc{textLeft, titleRc.bottom, textRight, titleRc.bottom + textH};
    DrawStr(dc, m_fontSmall, m_toastText, textRc, theme::Text, DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
    if (m_toastDetails.empty()) {
        m_toastCopyRc = {};
    } else {
        Button copy{kGlyphCopy, Cmd::None, L"Copy details"};
        PaintButton(dc, copy, m_toastCopyRc, m_hover.toast == 2, m_fontIcon);
    }
}

void MainWindow::SetStatus(const std::wstring& text, COLORREF color, UINT ms) {
    m_status = text;
    m_statusColor = color;
    SetTimer(m_hwnd, kTimerStatus, ms, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// =========================================================================
// window placement + menus
// =========================================================================

void MainWindow::ShowWindowNear(bool show) {
    POINT saved{m_settings.windowX, m_settings.windowY};
    bool useSaved = saved.x != INT_MIN && MonitorFromPoint(saved, MONITOR_DEFAULTTONULL) != nullptr;
    int x, y;
    if (useSaved) {
        x = saved.x;
        y = saved.y;
    } else {  // bottom right, next to the tray
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        x = mi.rcWork.right - M.width - S(12);
        y = mi.rcWork.bottom - S(400);
    }
    SetWindowPos(m_hwnd, nullptr, x, y, M.width, S(200), SWP_NOZORDER | SWP_NOACTIVATE);
    Relayout();
    if (show) ShowRestored();
    else if (!m_settings.minimizeToTray) ShowWindow(m_hwnd, SW_SHOWMINNOACTIVE);  // taskbar only
}

void MainWindow::SetUpStartTask() {
    bool autostart = m_startTask == 2 || m_startTask <= 0, shortcut = m_startTask <= 0;
    if (!dialogs::AskStartTask(m_hwnd, autostart, shortcut)) return;
    std::wstring err = starttask::Install(autostart, shortcut);
    m_startTask = -1;  // re-read on next menu open
    if (!err.empty()) ShowToast(L"Setting up the start task failed", err, L"");
    else SetStatus(autostart ? L"Starts without UAC prompt, also at logon" : L"Starts without UAC prompt",
                   theme::Green, 3000);
}

void MainWindow::RemoveStartTask() {
    if (MessageBoxW(m_hwnd, L"Remove the scheduled tasks and the desktop shortcut? Windows will ask for "
                            L"administrator rights again on every start.",
                    L"NIC Manager", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    std::wstring err = starttask::Remove();
    m_startTask = -1;
    if (!err.empty()) ShowToast(L"Removing the start task failed", err, L"");
    else SetStatus(L"Start task removed", theme::Green, 2500);
}

void MainWindow::ToggleCertificate(bool installed) {
    std::wstring q = installed
        ? L"Remove the NIC Manager signing certificate from \"Trusted Root\" and \"Trusted Publishers\"?"
        : L"Add the NIC Manager signing certificate to the computer's \"Trusted Root\" and \"Trusted Publishers\" "
          L"stores?\n\nWindows then shows \"NIC Manager\" as verified publisher of this app. The certificate can "
          L"only sign code and is used for nothing else.\n\nThumbprint: " + cert::Thumbprint();
    if (MessageBoxW(m_hwnd, q.c_str(), L"NIC Manager", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::wstring err = installed ? cert::Remove() : cert::Install();
    if (!err.empty()) ShowToast(installed ? L"Removing the certificate failed" : L"Installing the certificate failed", err, L"");
    else SetStatus(installed ? L"Certificate removed" : L"Certificate trusted", theme::Green, 2500);
}

void MainWindow::ShowRestored() {
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void MainWindow::ToggleVisible() {
    if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd) && GetForegroundWindow() == m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
    else ShowRestored();
}

void MainWindow::ApplyTaskbarMode() {
    // The taskbar only picks up the style change while the window is hidden.
    const bool visible = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    ShowWindow(m_hwnd, SW_HIDE);
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & ~(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
    ex |= m_settings.minimizeToTray ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (visible) ShowRestored();
}

void MainWindow::OpenPresetEditor() {
    ShowWindow(m_hwnd, SW_SHOW);
    if (dialogs::ManagePresets(m_hwnd, m_settings.presets)) settings::SavePresets(m_settings.presets);
}

void MainWindow::ShowTrayMenu() {
    const DWORD cur = m_settings.pingCycleMs;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SHOW, IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd) ? L"Hide window" : L"Show window");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PRESETS, L"Edit IPv4 presets...");

    HMENU cycle = CreatePopupMenu();
    bool matched = false;
    for (int i = 0; i < (int)(sizeof(kCycleChoices) / sizeof(kCycleChoices[0])); ++i) {
        bool on = kCycleChoices[i] == cur;
        matched = matched || on;
        AppendMenuW(cycle, MF_STRING | (on ? MF_CHECKED : 0), IDM_CYCLE_BASE + i, FormatMs(kCycleChoices[i]).c_str());
    }
    AppendMenuW(cycle, MF_SEPARATOR, 0, nullptr);
    std::wstring custom = matched ? L"Custom..." : L"Custom (" + FormatMs(cur) + L")...";
    AppendMenuW(cycle, MF_STRING | (matched ? 0 : MF_CHECKED), IDM_CYCLE_CUSTOM, custom.c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(cycle), (L"Ping cycle: " + FormatMs(cur)).c_str());

    AppendMenuW(menu, MF_STRING | (m_settings.alwaysOnTop ? MF_CHECKED : 0), IDM_TOPMOST, L"Always on top");
    AppendMenuW(menu, MF_STRING | (m_settings.minimizeToTray ? MF_CHECKED : 0), IDM_TO_TRAY, L"Minimize to tray");
    HMENU design = CreatePopupMenu();
    for (int i = 0; i < theme::kDesignCount; ++i)
        AppendMenuW(design, MF_STRING, IDM_DESIGN_BASE + i, theme::kDesigns[i].name);
    CheckMenuRadioItem(design, IDM_DESIGN_BASE, IDM_DESIGN_BASE + theme::kDesignCount - 1,
                       IDM_DESIGN_BASE + std::clamp(m_settings.design, 0, theme::kDesignCount - 1), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(design), L"Design");
    if (m_startTask < 0) m_startTask = starttask::IsInstalled() ? (starttask::AutostartInstalled() ? 2 : 1) : 0;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_startTask > 0 ? MF_CHECKED : 0), IDM_TASK_SETUP,
                m_startTask > 0 ? L"Start without UAC prompt: change..." : L"Start without UAC prompt...");
    AppendMenuW(menu, MF_STRING | (m_startTask > 0 ? 0 : MF_GRAYED), IDM_TASK_REMOVE, L"Remove start without UAC prompt");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool certInstalled = cert::HasEmbedded() && cert::IsInstalled();
    if (cert::HasEmbedded()) {
        AppendMenuW(menu, MF_STRING | (certInstalled ? MF_CHECKED : 0), IDM_CERT, L"Trust signing certificate");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    UINT id = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    auto setCycle = [&](DWORD ms) {
        m_settings.pingCycleMs = ms;
        settings::SaveGeneral(m_settings);
        m_pinger.SetCycle(ms);
    };
    if (id == IDM_SHOW) {
        if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
        else ShowRestored();
    }
    else if (id == IDM_PRESETS) OpenPresetEditor();
    else if (id == IDM_EXIT) DestroyWindow(m_hwnd);
    else if (id == IDM_TOPMOST) {
        m_settings.alwaysOnTop = !m_settings.alwaysOnTop;
        SetWindowPos(m_hwnd, m_settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        settings::SaveGeneral(m_settings);
    } else if (id == IDM_TASK_SETUP) {
        SetUpStartTask();
    } else if (id == IDM_TASK_REMOVE) {
        RemoveStartTask();
    } else if (id == IDM_CERT) {
        ToggleCertificate(certInstalled);
    } else if (id >= IDM_DESIGN_BASE && id < IDM_DESIGN_BASE + (UINT)theme::kDesignCount) {
        m_settings.design = (int)(id - IDM_DESIGN_BASE);
        theme::Apply(m_settings.design);
        settings::SaveGeneral(m_settings);
        Relayout();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (id == IDM_TO_TRAY) {
        m_settings.minimizeToTray = !m_settings.minimizeToTray;
        settings::SaveGeneral(m_settings);
        ApplyTaskbarMode();
    } else if (id >= IDM_CYCLE_BASE && id < IDM_CYCLE_BASE + sizeof(kCycleChoices) / sizeof(kCycleChoices[0])) {
        setCycle(kCycleChoices[id - IDM_CYCLE_BASE]);
    } else if (id == IDM_CYCLE_CUSTOM) {
        // Seconds with one decimal, "." or "," as separator: "0.5", "2", "12,5".
        auto parseSeconds = [](const std::wstring& s) -> DWORD {
            DWORD whole = 0, tenth = 0;
            size_t i = 0, digits = 0;
            for (; i < s.size() && iswdigit(s[i]) && digits < 3; ++i, ++digits) whole = whole * 10 + (s[i] - L'0');
            if (digits == 0) return 0;
            if (i < s.size() && (s[i] == L'.' || s[i] == L',')) {
                if (++i < s.size() && iswdigit(s[i])) tenth = s[i++] - L'0';
                else if (i < s.size()) return 0;
            }
            return i == s.size() ? whole * 1000 + tenth * 100 : 0;
        };
        std::wstring init = std::to_wstring(cur / 1000);
        if (cur % 1000) init += L"." + std::to_wstring(cur % 1000 / 100);
        auto text = dialogs::PromptText(
            m_hwnd, L"Ping cycle", L"Interval per host in seconds (0.1 - 30.0):", init,
            [&](const std::wstring& s, std::wstring& err) {
                DWORD v = parseSeconds(s);
                if (v < settings::kMinPingCycleMs || v > settings::kMaxPingCycleMs) {
                    err = L"Enter seconds between 0.1 and 30.0 (one decimal).";
                    return false;
                }
                return true;
            });
        if (text) setCycle(parseSeconds(*text));
    }
}

void MainWindow::ShowAdapterMenu(int ai, POINT screen) {
    if (ai < 0 || ai >= (int)m_adapters.size()) return;
    const Adapter a = m_adapters[ai];  // copy: the list may refresh while the menu is open
    const bool canUse = a.enabled && a.ipv4Enabled;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (IsHidden(a.guid) ? MF_CHECKED : 0), IDM_HIDE_ADAPTER, L"Hidden");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (canUse ? 0 : MF_GRAYED), IDM_SAVE_PRESET, L"Save IPv4 config as preset...");
    HMENU apply = CreatePopupMenu();
    for (int i = 0; i < (int)m_settings.presets.size(); ++i)
        AppendMenuW(apply, MF_STRING, IDM_APPLY_PRESET_BASE + i, m_settings.presets[i].name.c_str());
    if (m_settings.presets.empty()) AppendMenuW(apply, MF_STRING | MF_GRAYED, 0, L"(no presets)");
    AppendMenuW(menu, MF_POPUP | (canUse && !m_busy ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(apply),
                L"Apply preset");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_REFRESH, L"Refresh");

    UINT id = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (id == IDM_HIDE_ADAPTER) {
        auto& list = m_settings.hiddenAdapters;
        auto it = std::find(list.begin(), list.end(), a.guid);
        if (it != list.end()) list.erase(it);
        else list.push_back(a.guid);
        settings::SaveHiddenAdapters(list);
        Relayout();
    } else if (id == IDM_REFRESH) {
        m_monitor.Trigger();
    } else if (id == IDM_SAVE_PRESET) {
        auto name = dialogs::PromptText(m_hwnd, L"Save IPv4 preset", L"Preset name:", a.name,
                                        [](const std::wstring& s, std::wstring& err) {
                                            if (s.size() > 64) {
                                                err = L"Use at most 64 characters.";
                                                return false;
                                            }
                                            return true;
                                        });
        if (!name) return;
        Preset p{*name, a.cfg};
        if (p.cfg.dhcp) {  // a DHCP preset only stores the modes (+ static DNS)
            p.cfg.addresses.clear();
            p.cfg.gateways.clear();
            if (p.cfg.autoDns) p.cfg.dns.clear();
        }
        auto it = std::find_if(m_settings.presets.begin(), m_settings.presets.end(),
                               [&](const Preset& e) { return _wcsicmp(e.name.c_str(), p.name.c_str()) == 0; });
        if (it != m_settings.presets.end()) {
            if (MessageBoxW(m_hwnd, (L"Replace preset \"" + it->name + L"\"?").c_str(), L"NIC Manager",
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            *it = p;
        } else {
            m_settings.presets.push_back(p);
        }
        settings::SavePresets(m_settings.presets);
        SetStatus(L"Preset saved", theme::Green, 2500);
    } else if (id >= IDM_APPLY_PRESET_BASE && id < IDM_APPLY_PRESET_BASE + m_settings.presets.size()) {
        ApplyConfig(a, m_settings.presets[id - IDM_APPLY_PRESET_BASE].cfg);
    }
}

void MainWindow::ShowHostMenu(int rowIndex, POINT screen) {
    const Row row = m_rows[rowIndex];  // copy: a ping result may rebuild the rows while the menu is open
    if (row.host < 0 || row.host >= (int)m_settings.hosts.size()) return;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (m_settings.hosts[row.host].ping ? MF_CHECKED : 0), IDM_HOST_PING, L"Ping");
    UINT id = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (id == IDM_HOST_PING) {
        SetHostPing(row.host, !m_settings.hosts[row.host].ping);
        settings::SaveHosts(m_settings.hosts);
        SyncPinger();
        Relayout();
    }
}

void MainWindow::SetHostPing(int i, bool on) {
    if (m_settings.hosts[i].ping == on) return;
    m_settings.hosts[i].ping = on;
    m_hostState[i] = {m_nextHostId++};  // new id: a late reply from the old worker is ignored
}

// ---- subnet scans -------------------------------------------------------

void MainWindow::ShowScanMenu(Adapter a, int index) {
    if (index < 0 || index >= (int)a.cfg.addresses.size()) return;
    const IpEntry entry = a.cfg.addresses[index];
    uint32_t addr = 0;
    if (!ip::ParseIpv4(entry.ip, &addr)) return;
    const int prefix = entry.prefix;
    const uint32_t mask = prefix <= 0 ? 0 : 0xFFFFFFFFu << (32 - prefix);
    const uint32_t network = addr & mask;
    const std::wstring target = ip::FormatIpv4(network) + L"/" + std::to_wstring(prefix);
    const bool tooLarge = prefix < scan::kMinPrefix;
    const UINT state = tooLarge ? MF_GRAYED : 0;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"Scan " + target).c_str());
    if (tooLarge)
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                    (L"Subnet is larger than /24 (" + std::to_wstring(1ull << (32 - prefix)) + L" addresses); not scanned")
                        .c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | state, IDM_SCAN_ARP, L"ARP scan");
    AppendMenuW(menu, MF_STRING | state, IDM_SCAN_PING, L"Ping scan");
    AppendMenuW(menu, MF_STRING | state, IDM_SCAN_TCP, L"TCP port...");
    POINT pt;
    GetCursorPos(&pt);
    UINT id = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (id == IDM_SCAN_ARP) StartScan(a, index, scan::Kind::Arp, network, prefix, 0);
    else if (id == IDM_SCAN_PING) StartScan(a, index, scan::Kind::Ping, network, prefix, 0);
    else if (id == IDM_SCAN_TCP) {
        static std::wstring lastPort = L"22";
        auto text = dialogs::PromptText(m_hwnd, L"TCP port check", L"Hosts in " + target + L" with this TCP port open:",
                                        lastPort, [](const std::wstring& s, std::wstring& err) {
                                            bool digits = !s.empty() && s.size() <= 5 &&
                                                          std::all_of(s.begin(), s.end(), iswdigit);
                                            int v = digits ? _wtoi(s.c_str()) : 0;
                                            if (v < 1 || v > 65535) {
                                                err = L"Enter a port between 1 and 65535.";
                                                return false;
                                            }
                                            return true;
                                        });
        if (!text) return;
        lastPort = *text;
        StartScan(a, index, scan::Kind::Tcp, network, prefix, _wtoi(text->c_str()));
    }
}

void MainWindow::StartScan(const Adapter& a, int index, scan::Kind kind, uint32_t network, int prefix, int port) {
    if (m_busy) {
        MessageBeep(MB_OK);
        return;
    }
    const std::wstring target = ip::FormatIpv4(network) + L"/" + std::to_wstring(prefix);
    const std::wstring what =
        kind == scan::Kind::Tcp ? L"TCP port " + std::to_wstring(port) : std::wstring(scan::KindName(kind));
    auto result = std::make_shared<scan::ScanResult>();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    uint32_t source = 0;
    ip::ParseIpv4(a.cfg.addresses[index].ip, &source);
    const bool demo = m_demo;
    const HWND hwnd = m_hwnd;

    m_scanning = true;
    m_scanGuid = a.guid;
    m_scanIndex = index;
    m_scanCancel = cancel;

    JobOptions opts;
    opts.timeoutMs = scan::kScanTimeoutMs;
    opts.simulateInDemo = false;
    opts.onSuccess = [this, result, kind, target, what, port] {
        const auto& r = *result;
        std::vector<std::vector<std::wstring>> rows;
        for (const auto& h : r.hosts) rows.push_back({ip::FormatIpv4(h.ip), h.name, h.mac, h.vendor});
        std::wstring summary = std::to_wstring(r.hosts.size()) + (r.hosts.size() == 1 ? L" host" : L" hosts") +
                               L" in " + target +
                               (kind == scan::Kind::Tcp ? L" with TCP port " + std::to_wstring(port) + L" open"
                                                        : L" answered the " + what + L" scan");
        if (r.cancelled || r.timedOut)
            summary += L" (" + std::wstring(r.cancelled ? L"stopped" : L"time limit reached") + L" after " +
                       std::to_wstring(r.done) + L" of " + std::to_wstring(r.total) + L" addresses)";
        SetStatus(r.cancelled ? L"Scan stopped" : L"Scan finished", theme::Green, 3000);
        auto addToPingList = [this, result](const std::vector<int>& picks) {
            int added = 0;
            for (int i : picks) {
                if (i < 0 || i >= (int)result->hosts.size()) continue;
                const auto& h = result->hosts[i];
                std::wstring address = ip::FormatIpv4(h.ip);
                bool listed = std::any_of(m_settings.hosts.begin(), m_settings.hosts.end(), [&](const HostEntry& e) {
                    return _wcsicmp(e.address.c_str(), address.c_str()) == 0 ||
                           (!h.name.empty() && _wcsicmp(e.address.c_str(), h.name.c_str()) == 0);
                });
                if (listed) continue;
                m_settings.hosts.push_back({h.name.empty() ? address : h.name, address, {}});
                m_hostState.push_back({m_nextHostId++});
                ++added;
            }
            if (added) {
                settings::SaveHosts(m_settings.hosts);
                SyncPinger();
                Relayout();
            }
            return added;
        };
        dialogs::ShowList(m_hwnd, L"Subnet scan: " + target, summary, rows, addToPingList);
    };
    RunJob(L"Scanning " + target + L" (" + what + L")",
           [=](const netcmd::Deadline& dl) {
               auto progress = [hwnd](int done, int total) {
                   PostMessageW(hwnd, WM_APP_SCANPROGRESS, (WPARAM)done, (LPARAM)total);
               };
               if (demo) {  // sample hosts, no probes
                   auto targets = scan::Targets(network, prefix);
                   result->total = (int)targets.size();
                   for (uint32_t t : targets) {
                       if (*cancel) {
                           result->cancelled = true;
                           break;
                       }
                       Sleep(25);
                       uint32_t h = t - network;
                       if (h == 1 || h == 7 || h == 23 || h == 50 || h == 101 || h == 254) {
                           scan::Host x;
                           x.ip = t;
                           if (kind == scan::Kind::Arp) {
                               wchar_t mac[18];
                               swprintf_s(mac, L"3C:52:82:1F:%02X:%02X", (h * 37) & 0xFF, h & 0xFF);
                               x.mac = mac;
                               x.vendor = h == 1 ? L"AVM GmbH" : L"Hewlett Packard";
                           }
                           if (h == 1) x.name = L"fritz.box";
                           if (kind != scan::Kind::Tcp || h % 2 == 1) result->hosts.push_back(x);
                       }
                       progress(++result->done, result->total);
                   }
                   return netcmd::JobResult{};
               }
               return scan::Scan(kind, network, prefix, source, port, dl, *cancel, progress, *result);
           },
           std::move(opts));
    Relayout();  // the scan button becomes a stop button
}

void MainWindow::OnScanProgress(int done, int total) {
    if (!m_busy || !m_scanning) return;
    if (m_scanCancel && *m_scanCancel) return;  // keeps "Stopping..."
    // Count first: the bottom bar cuts the end of long labels.
    m_busyLabel = std::to_wstring(done) + L"/" + std::to_wstring(total) + L"  " + m_busyBase;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}
