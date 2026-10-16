# NIC Manager

A 300 px wide tray tool for Windows 10/11 (32 and 64 bit). It switches
network adapter IPv4 settings, keeps IPv4 presets, and pings a list of
hosts. It is plain C++17 on the Win32 API, with no framework and no
third-party dependencies.

- Plan, GUI sketch, theme, design decisions: [docs/PLAN.md](docs/PLAN.md)
- Build: open the folder in VS Code (C/C++ and CMake Tools extensions, MSYS2
  with gcc, cmake and ninja), pick the `mingw-x64-debug` preset, and press F7.
  Or from a terminal: `cmake --preset mingw-x64-release && cmake --build --preset mingw-x64-release`.
- Run `NicManager.exe` (it asks for elevation), or `NicManager.exe --demo` to
  see sample adapters without changing anything.
- Action stubs to fill in: `src/actions.cpp`.
