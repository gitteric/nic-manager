// Colour theme and layout metrics (at 96 DPI; scaled at runtime).
#pragma once

#include <windows.h>

namespace theme {

// Flat dark theme.
constexpr COLORREF Bg            = RGB(0x1E, 0x1F, 0x22);  // window background
constexpr COLORREF Surface       = RGB(0x27, 0x29, 0x2D);  // list rows
constexpr COLORREF SurfaceHover  = RGB(0x32, 0x35, 0x3A);
constexpr COLORREF Detail        = RGB(0x22, 0x24, 0x27);  // expanded area
constexpr COLORREF Field         = RGB(0x16, 0x17, 0x19);  // edit fields
constexpr COLORREF Border        = RGB(0x3A, 0x3D, 0x42);
constexpr COLORREF Text          = RGB(0xE6, 0xE6, 0xE6);
constexpr COLORREF TextDim       = RGB(0x9A, 0xA0, 0xA6);
constexpr COLORREF TextDisabled  = RGB(0x5F, 0x63, 0x68);
constexpr COLORREF Accent        = RGB(0x4C, 0x9A, 0xFF);
constexpr COLORREF Green         = RGB(0x3F, 0xB9, 0x50);
constexpr COLORREF Red           = RGB(0xF8, 0x51, 0x49);
constexpr COLORREF Grey          = RGB(0x6E, 0x76, 0x81);
constexpr COLORREF ToggleOff     = RGB(0x4A, 0x4E, 0x55);

// Row tints behind the state colour bar.
constexpr COLORREF GreenTint     = RGB(0x22, 0x33, 0x27);
constexpr COLORREF RedTint       = RGB(0x38, 0x25, 0x26);

}  // namespace theme
