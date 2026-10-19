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
// State colours (up / down). Not constexpr: Apply swaps them per design.
inline COLORREF Green            = RGB(0x3F, 0xB9, 0x50);
inline COLORREF Red              = RGB(0xF8, 0x51, 0x49);
constexpr COLORREF Grey          = RGB(0x6E, 0x76, 0x81);
constexpr COLORREF ToggleOff     = RGB(0x4A, 0x4E, 0x55);

// Row tints behind the state colour bar.
inline COLORREF GreenTint        = RGB(0x22, 0x33, 0x27);
inline COLORREF RedTint          = RGB(0x38, 0x25, 0x26);

// Designs selectable in the tray menu. Only the state colours differ.
struct Design {
    const wchar_t* name;
    COLORREF up, down, upTint, downTint;
};
inline constexpr Design kDesigns[] = {
    {L"Standard", RGB(0x3F, 0xB9, 0x50), RGB(0xF8, 0x51, 0x49), RGB(0x22, 0x33, 0x27), RGB(0x38, 0x25, 0x26)},
    // Blue / orange (Okabe-Ito palette, brightened for the dark background):
    // stays distinct with red-green colour blindness.
    {L"Color-blind friendly", RGB(0x56, 0xB4, 0xE9), RGB(0xE6, 0x9F, 0x00), RGB(0x1F, 0x2E, 0x3A),
     RGB(0x3A, 0x2E, 0x1A)},
};
inline constexpr int kDesignCount = (int)(sizeof(kDesigns) / sizeof(kDesigns[0]));

inline void Apply(int design) {
    const Design& d = kDesigns[design >= 0 && design < kDesignCount ? design : 0];
    Green = d.up;
    Red = d.down;
    GreenTint = d.upTint;
    RedTint = d.downTint;
}

}  // namespace theme
