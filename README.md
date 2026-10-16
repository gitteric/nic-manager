# NIC Manager

A 200 px wide tray tool for Windows 10/11 (32 and 64 bit). It switches
network adapter IPv4 settings, keeps IPv4 presets, and pings a list of
hosts. It is plain C++17 on the Win32 API, with no framework and no
third-party dependencies.

- Plan, GUI sketch, theme, design decisions: [docs/PLAN.md](docs/PLAN.md)
- Build: open the folder in VS Code (C/C++ and CMake Tools extensions, plus
  VS 2022 Build Tools), pick the `x64` or `x86` preset, and press F7.
  Or from a terminal: `cmake --preset x64 && cmake --build --preset x64-release`.
- Run `NicManager.exe` (it asks for elevation), or `NicManager.exe --demo` to
  see sample adapters without changing anything.
- Action stubs to fill in: `src/actions.cpp`.
