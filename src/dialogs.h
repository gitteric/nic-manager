// Modal dialogs (templates in res/app.rc.in), styled dark to match.
#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "model.h"

namespace dialogs {

// Returns the entered text, or nullopt on cancel. `validate` may reject the
// input by returning false and filling `error`.
using Validator = std::function<bool(const std::wstring& text, std::wstring& error)>;
std::optional<std::wstring> PromptText(HWND owner, const std::wstring& title, const std::wstring& label,
                                       const std::wstring& initial, Validator validate = {});

// Edits `action` in place; returns false on cancel. `defaultHost` pre-fills
// the host for a new action.
bool EditAction(HWND owner, HostAction& action, const std::wstring& defaultHost, bool isNew);

// List with delete / export / import. Returns true if `presets` changed.
bool ManagePresets(HWND owner, std::vector<Preset>& presets);

// Dark title bar + dark control themes for any top-level window.
void ApplyDarkFrame(HWND hwnd);

}  // namespace dialogs
