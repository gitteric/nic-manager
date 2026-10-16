# NIC Manager: plan

A small tray tool for switching network adapter settings and watching a
list of hosts, rewritten in C++ against the plain Win32 API.

## 1. GUI sketch

200 px wide (scaled with DPI). The window grows with its content and only
scrolls when it would be taller than the desktop work area. It sits next
to the tray, grows upwards, and remembers where you dragged it.

```
┌──────────────────────────────┐
│ NIC Manager               ✕  │  title strip (drag to move, ✕ hides to tray)
│ ADAPTERS                     │
│▌Ethernet                  ›  │  green bar = connected
│▌192.168.1.57                 │
│▌Lab USB                   ⌄  │  expanded entry (only one at a time)
│▌10.10.0.5 (...)              │  "(...)" = more than one address
│   Enabled              [●━]  │  switch
│   Device                     │
│    USB 3.0 Gigabit Ether…    │
│   DHCP                 [━○]  │
│   IP address / subnet     +  │  + only while DHCP is off
│    10.10.0.5/16           ✎  │  double-click or ✎ edits inline
│    172.16.4.20/24      ✎  🗑  │  first address can't be removed
│   Auto DNS             [━○]  │  only switchable while DHCP is on
│   DNS servers                │
│    10.10.0.1           ✎  🗑  │
│    1.1.1.1             ✎  🗑  │
│   Gateways                +  │
│    10.10.0.1           ✎  🗑  │
│▌WiFi                      ›  │  red bar = not connected
│▌Not connected                │
│▌Bluetooth                 ›  │  grey + dimmed text = disabled
│▌Disabled                     │
│ HOSTS                     +  │
│▌10.0.0.5        +  ✎  🗑   ⌄  │  icons appear on hover; green = ping ok
│   ▶ RDP admin@10.0.0.5  ✎ 🗑  │  click runs the action
│▌nas.local                    │  red = ping failed
├──────────────────────────────┤
│ [net] [dns] [dhcp]  ◌ Apply… │  shortcuts + busy spinner / status
└──────────────────────────────┘
```

Right-click on an adapter: **Save IPv4 config as preset…**, **Apply preset ▸**, **Refresh**.

Tray icon: left click shows/hides. Right click opens **Show/Hide**, **Edit IPv4 presets…**, **Ping cycle ▸** (100 ms … 10 s, Custom 1–10000 ms), **Always on top**, **Exit**.

Bottom bar: open the classic Network Connections window (`control ncpa.cpl`), flush the DNS cache (`ipconfig /flushdns`), and renew DHCP (`ipconfig /renew`, for the expanded adapter or for all of them).

Run `NicManager.exe --demo` to see the real UI with sample adapters.

### Colour theme (flat dark)

| Role | Hex |
|---|---|
| Window background | `#1E1F22` |
| List row | `#27292D` (hover `#32353A`) |
| Expanded detail area | `#222427` |
| Edit fields | `#161719` |
| Border / separators | `#3A3D42` |
| Text / secondary / disabled | `#E6E6E6` / `#9AA0A6` / `#5F6368` |
| Accent (switch on, spinner) | `#4C9AFF` |
| Connected / ping ok | `#3FB950` (row tint `#223327`) |
| Not connected / ping failed | `#F85149` (row tint `#382526`) |
| Disabled | `#6E7681` |

All colours live in `src/theme.h`. The font is Segoe UI, and the icons come from Segoe MDL2 Assets, which ships with every Windows 10 and 11 install, so there are no image files.

## 2. C++ or Rust?

**C++.** Both compile to native code that is equally fast for this job. The
speed you will notice comes from the design, not the language: the UI
thread never waits on the network, and every slow call runs on a worker.

C++ wins on practical grounds:

- Win32, IP Helper, SetupAPI and the shell APIs are C APIs. In C++ you call them directly, and every Microsoft sample and doc applies one to one. In Rust (`windows-rs`) you would write the same calls inside `unsafe` blocks, with more ceremony and a heavier build.
- An owner-drawn Win32 window is mostly straightforward GDI code, where Rust's safety guarantees add little.
- The result is a single exe of about 1.3 MB, with no runtime and no dependencies beyond Windows' own DLLs.

Rust would be the better choice if the tool grew a lot of parsing or concurrent logic. For a small GUI tool that wraps system calls, it is not.

## 3. VS Code

VS Code works well for this. It edits and debugs, and the Microsoft C++ compiler (MSVC) builds the code.

1. Install the **Build Tools for Visual Studio 2022** (free) with the workload "Desktop development with C++". Make sure *MSVC v143* and a *Windows 11 SDK* are ticked. CMake comes with it.
2. In VS Code, install the extensions **C/C++** (`ms-vscode.cpptools`) and **CMake Tools** (`ms-vscode.cmake-tools`). The repo's `.vscode/extensions.json` suggests both.
3. Open the folder. CMake Tools reads `CMakePresets.json` and asks for a preset:
   - `x64`: 64-bit build.
   - `x86`: 32-bit build, for Windows 10 32-bit.
   - `x64-noadmin`: a build that doesn't request elevation, for debugging the UI.
4. Build with **F7** (or *CMake: Build*). Debug with **F5**, which uses `.vscode/launch.json`.

The normal build requests administrator rights, because netsh needs them to change adapters. To debug that build, start VS Code as administrator. Otherwise, use the `x64-noadmin` preset: the UI, ping list and presets all work, and applying a change shows an "access denied" message. Running `NicManager.exe --demo` shows sample adapters and only simulates commands, which is handy for UI work.

From a terminal:

```
cmake --preset x64
cmake --build --preset x64-release
build\x64\Release\NicManager.exe
```

## 4. Frameworks and libraries

**No framework and no third-party code.** Everything comes from the Windows SDK:

| Need | API / library |
|---|---|
| Window, drawing, menus, tray | user32, gdi32, shell32 (`Shell_NotifyIcon`) |
| Tooltips, inline edit subclassing | comctl32 v6 |
| Dark title bar / controls, Win11 corners | dwmapi, uxtheme |
| Adapter list incl. disabled ones | SetupAPI + cfgmgr32 (`CM_Get_DevNode_Status`) |
| Addresses, gateways, DNS, DHCP server, change notifications | iphlpapi (`GetAdaptersAddresses`, `NotifyIpInterfaceChange`, …) |
| Ping | iphlpapi `IcmpSendEcho`, ws2_32 `GetAddrInfoW` |
| Changing settings | `netsh.exe` / `ipconfig.exe` child processes with a timeout |
| Settings | advapi32 registry, crypt32 DPAPI for stored passwords |
| Preset files | small built-in JSON reader and writer (`src/json.cpp`) |
| Open/save dialogs | comdlg32 |

I thought about a GUI framework (Qt, wxWidgets, Dear ImGui) and decided
none is needed. The flat look is easier to get exactly right with
owner-drawing, and a framework would add 10 to 30 MB and a deployment
step. One visual compromise: the few modal dialogs (action editor, preset
list, name prompt) use standard controls with a dark theme applied. On
Windows 10 builds older than 1809 their buttons stay light. If you want
those dialogs fully custom-drawn too, that's the one place I'd discuss
doing more.

## 5. Implementation notes

### Threads

- **UI thread:** layout, painting and input. It never blocks.
- **Adapter monitor:** one thread. It wakes on IP interface, address and route change notifications, and polls every 2 s to catch enable/disable and cable changes. It posts a new snapshot only when something changed, and posts nothing while you are editing a field, so rows don't move under the cursor.
- **Pinger:** one thread per host. Each one resolves the name (cached for 30 s), sends `IcmpSendEcho` with a 1 s timeout, posts the result, then waits for the ping cycle. A down host never delays the others.
- **Command job:** one worker at a time. While it runs, the spinner shows and other commands are refused. A single netsh/ipconfig call is killed after 20 s, and a whole job after 60 s.

### How changes are applied

Each edit builds the complete new IPv4 configuration and applies it with netsh:

```
netsh interface ipv4 set address name="X" source=static address=A mask=M gateway=G1 gwmetric=0
netsh interface ipv4 add address name="X" address=A2 mask=M2            (more addresses)
netsh interface ipv4 add route prefix=0.0.0.0/0 interface="X" nexthop=G2 store=persistent   (more gateways)
netsh interface ipv4 set dnsservers name="X" source=static address=D1 register=primary validate=no
netsh interface ipv4 add dnsservers name="X" address=D2 index=2 validate=no
netsh interface ipv4 set address name="X" source=dhcp / set dnsservers ... source=dhcp
netsh interface set interface name="X" admin=enabled|disabled
```

The commands are not localized. Only their output is, and the tool shows that output only when something fails.

Behaviour I chose where your list left it open:

- Switching DHCP off keeps the currently assigned address, gateway and DNS as the static configuration. If there is no address, it asks for one inline first.
- Switching DHCP on also switches DNS back to automatic. Auto DNS off keeps the current DNS servers as static ones.
- Your list gave the DNS rows a switch and a trash icon. I read the switch as a pencil, so DNS servers are edited like IPs, and only while Auto DNS is off.
- Gateways get a trash icon too, because a gateway is optional. Each list has a + in its caption to add an entry, shown only while that list is editable. DNS is limited to two servers.
- Presets also have **Apply preset ▸** in the adapter context menu, because saving them is only half the job.
- Address input accepts `10.0.0.5/16`, `10.0.0.5 255.255.0.0` or `10.0.0.5` (which means /24).
- Hosts accept an IPv4 address or an RFC 1123 host name. `10.0.0` and `a..b` are rejected.

### Storage (`HKCU\Software\NicManager`)

| Value | Type | Content |
|---|---|---|
| `PingCycleMs` | DWORD | 1–10000 |
| `WindowX`, `WindowY` | DWORD | last position |
| `AlwaysOnTop` | DWORD | 0/1 |
| `Hosts` | SZ | JSON: hosts and their actions; passwords DPAPI-encrypted (`"dpapi:…"`) |
| `Presets` | SZ | JSON, the same format as the export file |

Preset export file:

```json
{
  "format": "nic-manager-ipv4-presets",
  "version": 1,
  "presets": [
    { "name": "Lab", "dhcp": false, "autoDns": false,
      "addresses": [{ "ip": "10.10.0.5", "prefix": 16 }],
      "gateways": ["10.10.0.1"], "dns": ["10.10.0.1", "1.1.1.1"] }
  ]
}
```

Import validates every address and replaces presets that have the same name.

### Action hulls

`src/actions.cpp` has `ConnectRdp(owner, RdpSettings)`, `ConnectPutty(owner, PuttySettings)` and `ConnectTightVnc(owner, VncSettings)`. Each one receives the stored settings and currently shows a "stub" message. The comments in each sketch the launch command (`mstsc /v: /f` with `cmdkey`, `putty -ssh -P -pw`, `tvnviewer -host= -port= -password=`). The settings structs are in `src/model.h`.

### Source map

| File | Purpose |
|---|---|
| `src/main.cpp` | entry point, single instance, `--demo` switch |
| `src/mainwindow.*` | owner-drawn window, layout, input, inline editing, tray, menus |
| `src/netinfo.*` | adapter enumeration and change monitor |
| `src/netcmd.*` | netsh/ipconfig runner with timeouts |
| `src/pinger.*` | per-host ping threads |
| `src/settings.*` | registry, DPAPI, JSON mapping, file I/O |
| `src/dialogs.*` | action editor, preset manager, text prompt |
| `src/ipaddr.*`, `src/json.*` | validation and JSON |
| `src/actions.*` | RDP / PuTTY / VNC hulls |
| `src/theme.h`, `src/model.h` | colours, data types |
| `res/` | dialog templates, manifest (elevation, DPI, common controls v6) |

## 6. What was verified, and what wasn't

- **Verified:** the code compiles without warnings for 64-bit and 32-bit Windows (MinGW-w64 GCC 13, `-Wall -Wextra`). The exe links only Windows system DLLs. Unit checks for address, host name and JSON parsing, and for the preset round-trip, pass. Under Wine, the window, tray icon, host add/validate/edit, ping colouring, the action dialog, registry persistence, the expanded adapter view (demo data), inline IP editing and the busy spinner all work.
- **Not verified:** a real Windows run, an MSVC build, and the actual netsh changes against real adapters. Neither was possible from here. Those need a first run on your machine, ideally against a spare or USB adapter. MSVC is stricter about some things and may raise a few `/W4` warnings. Please send me any build errors and I'll fix them.
