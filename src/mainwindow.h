// The single owner-drawn main window plus the tray icon.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "model.h"
#include "netcmd.h"
#include "netinfo.h"
#include "scan.h"
#include "pinger.h"
#include "settings.h"

// How MainWindow::RunJob runs a background job.
struct JobOptions {
    DWORD timeoutMs = 60000;     // whole job; single netsh calls are capped at 20 s
    bool adminHint = true;       // failure text mentions missing elevation
    bool simulateInDemo = true;  // --demo: replace fn with a short sleep
    std::function<void()> onSuccess;  // UI thread, after a successful job
};

class MainWindow {
public:
    static constexpr const wchar_t* kClassName = L"NicManager.MainWindow";
    static constexpr UINT WM_APP_SHOW = WM_APP + 50;  // sent by a second instance

    // demo: show sample adapters and simulate commands instead of running them.
    bool Create(HINSTANCE inst, bool demo = false, bool minimized = false);

private:
    // ---- layout model ----
    enum class Cmd {
        None, ExpandAdapter, ExpandHost,
        ToggleEnabled, ToggleDhcp, ToggleAutoDns,
        EditAddr, DelAddr, AddAddr, EditGw, DelGw, AddGw, EditDns, DelDns, AddDns,
        AddHost, EditHost, DelHost, AddAction, RunAction, EditAction, DelAction,
        OpenNcpa, OpenWifi, FlushDns, RenewDhcp, Hide, Exit, Minimize, ScanSubnet, StopScan,
        ToggleShowHidden,
    };
    enum class EditKind { None, Address, NewAddress, Gateway, NewGateway, Dns, NewDns, Host, NewHost };
    enum class RowKind { Header, Adapter, Toggle, Caption, Value, Host, Action, Hint };

    struct Button {
        wchar_t glyph;
        Cmd cmd;
        const wchar_t* tip;
        RECT rc{};
    };
    struct Row {
        RowKind kind = RowKind::Hint;
        int y = 0, h = 0;              // content coordinates
        int adapter = -1, host = -1, index = -1;
        std::wstring text, sub, tip;
        COLORREF state = 0;            // colour bar for adapters/hosts
        bool dim = false, expanded = false, chevron = false, hoverButtons = false;
        bool on = false, toggleEnabled = true;
        Cmd rowCmd = Cmd::None, dblCmd = Cmd::None, toggleCmd = Cmd::None;
        EditKind editKind = EditKind::None;
        bool editSlot = false;         // the inline editor sits on this row
        RECT toggleRc{};
        int textRight = 0;      // text end when buttons are shown
        int textRightFull = 0;  // text end without buttons
        std::vector<Button> buttons;
    };
    struct EditTarget {
        EditKind kind = EditKind::None;
        std::wstring guid;  // adapter
        int host = -1;
        int index = -1;
        bool fromDhcp = false;  // committing switches DHCP off (other leased values stay as static)
    };
    struct Metrics {
        int width, title, bottom, header, adapter, host, toggle, caption, value, action, hint;
        int pad, indent, btn, bar, scrollW, editH, switchW, switchH, chevron;
    };
    struct HostState {
        uint32_t id = 0;
        ping::State state = ping::State::Unknown;
        int rtt = 0;
    };
    struct Hit {
        int fixed = -1;   // index into m_fixed
        int row = -1;
        int button = -1;  // index into row.buttons
        bool toggle = false;
        bool scrollbar = false;
        int toast = 0;  // 1 = message body (dismiss), 2 = copy details
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // setup
    void OnCreate();
    void UpdateDpi(UINT dpi);
    void CreateFonts();
    void DestroyFonts();
    void AddTrayIcon();
    int S(int v) const { return MulDiv(v, (int)m_dpi, 96); }

    // layout
    Row& Push(RowKind kind, int h);
    void BuildLayout();
    void AddAdapterDetails(int ai);
    void PlaceButtons();
    void UpdateWindowSize();
    int ViewHeight() const;
    bool Scrollable() const { return m_contentH > ViewHeight(); }
    void ScrollTo(int y);
    void EnsureVisible(int row);
    void Relayout();

    // painting
    void OnPaint();
    void PaintRow(HDC dc, const Row& r, int rowIndex, int dy);
    void PaintButton(HDC dc, const Button& b, RECT rc, bool hover, HFONT font);
    void PaintToggle(HDC dc, RECT rc, bool on, bool enabled);
    void PaintSpinner(HDC dc, int cx, int cy);

    // input
    Hit HitTest(POINT pt) const;
    void OnMouseMove(POINT pt);
    void OnTitleMouseMove(POINT screenPt);
    void SetTip(const std::wstring& tip);
    void OnLButtonDown(POINT pt, bool dbl);
    void OnLButtonUp(POINT pt);
    void OnRButtonUp(POINT pt);
    void Execute(const Row* row, Cmd cmd);
    bool IsBlockedWhileBusy(Cmd cmd) const;

    // inline editing
    void BeginEdit(EditKind kind, int adapterOrHost, int index, const std::wstring& text, bool fromDhcp = false);
    void CommitEdit();
    void EndEdit();
    void PositionEdit();
    const Adapter* FindAdapter(const std::wstring& guid) const;

    // operations
    void ApplyConfig(const Adapter& a, const Ipv4Config& cfg);
    void RunJob(const std::wstring& label, std::function<netcmd::JobResult(const netcmd::Deadline&)> fn,
                JobOptions opts = {});
    void OnJobDone(netcmd::JobResult* result);
    void OnAdapters(std::vector<Adapter>* list);
    void OnPing(uint32_t id, ping::State state, int rtt);
    void SyncPinger();
    void SetStatus(const std::wstring& text, COLORREF color, UINT ms = 4000);
    // Error strip above the bottom bar; hides itself after a few seconds.
    bool EditActionDialog(HostAction& act, const std::wstring& hostAddress, bool isNew);
    void ShowToast(const std::wstring& title, const std::wstring& text, const std::wstring& details);
    void HideToast();
    void PaintToast(HDC dc, int bottom, int right);

    // menus
    void ShowTrayMenu();
    void ShowAdapterMenu(int ai, POINT screen);
    void ShowHostMenu(int rowIndex, POINT screen);
    void SetHostPing(int i, bool on);  // caller saves and syncs the pinger
    void ShowWindowNear(bool show = true);
    void ToggleVisible();
    void ShowRestored();
    void ToggleCertificate(bool installed);
    void SetUpStartTask();
    void RemoveStartTask();
    void ApplyTaskbarMode();  // taskbar button only while not minimizing to tray
    void OpenPresetEditor();
    void ShowScanMenu(Adapter a, int index);  // copy: the list may refresh during the prompt
    void StartScan(const Adapter& a, int index, scan::Kind kind, uint32_t network, int prefix, int port);
    void OnScanProgress(int done, int total);

    std::wstring AdapterSummary(const Adapter& a) const;
    std::wstring AdapterTip(const Adapter& a) const;
    bool IsHidden(const std::wstring& guid) const;
    COLORREF AdapterColor(const Adapter& a) const;

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr, m_edit = nullptr, m_tooltip = nullptr;
    UINT m_dpi = 96;
    Metrics M{};
    HFONT m_font = nullptr, m_fontBold = nullptr, m_fontSmall = nullptr, m_fontIcon = nullptr;
    HICON m_iconSmall = nullptr, m_iconLarge = nullptr;
    UINT m_taskbarCreated = 0;
    bool m_elevated = false;
    bool m_demo = false;
    int m_startTask = -1;  // -1 unknown, 0 none, 1 on demand, 2 on demand + autostart

    AppSettings m_settings;
    std::vector<Adapter> m_adapters;
    std::unique_ptr<std::vector<Adapter>> m_pendingAdapters;
    bool m_adaptersLoaded = false;
    std::wstring m_expandedAdapter;
    int m_expandedHost = -1;
    std::vector<HostState> m_hostState;
    uint32_t m_nextHostId = 1;

    std::vector<Row> m_rows;
    std::vector<Button> m_fixed;  // title + bottom bar, client coordinates
    int m_layoutY = 0, m_contentH = 0, m_scroll = 0;
    Hit m_hover, m_press;
    bool m_trackingMouse = false;
    bool m_trackingNc = false;   // TME_NONCLIENT tracking over the title strip
    int m_trayRetries = 0;       // failed tray icon adds so far (see AddTrayIcon)
    bool m_dragScroll = false;
    int m_dragStartY = 0, m_dragStartScroll = 0;
    RECT m_dragStartRect{};      // window rect and cursor when a window move starts (WM_MOVING)
    POINT m_dragStartCursor{};
    std::wstring m_tipText;

    EditTarget m_editTarget;
    bool m_endingEdit = false;
    int m_editGen = 0;

    bool m_busy = false;
    std::wstring m_busyLabel;  // bottom bar text while busy
    std::wstring m_busyBase;   // the job's name, for progress and errors
    JobOptions m_jobOpts;
    // Running subnet scan: the address row shows a stop button.
    bool m_scanning = false;
    std::wstring m_scanGuid;
    int m_scanIndex = -1;
    std::shared_ptr<std::atomic<bool>> m_scanCancel;
    int m_spin = 0;
    std::thread m_job;
    std::wstring m_status;
    std::wstring m_toastTitle, m_toastText, m_toastDetails;
    RECT m_toastRc{}, m_toastCopyRc{};
    COLORREF m_statusColor = 0;

    ping::Pinger m_pinger;
    netinfo::AdapterMonitor m_monitor;
};
