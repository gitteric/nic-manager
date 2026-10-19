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

// Options for the "start without UAC prompt" tasks; false on cancel.
bool AskStartTask(HWND owner, bool& autostart, bool& shortcut);

// Name and address of a host entry; an empty name becomes the address.
bool EditHost(HWND owner, std::wstring& name, std::wstring& address, bool& ping);

// Edits `action` in place; returns false on cancel. Actions connect to
// `hostAddress` (shown read-only; the browser URL is preset to it).
// `vncPath` is the global TightVNC viewer path, shown with VNC actions.
bool EditAction(HWND owner, HostAction& action, const std::wstring& hostAddress, bool isNew, std::wstring& vncPath);

// List with delete / export / import. Returns true if `presets` changed.
bool ManagePresets(HWND owner, std::vector<Preset>& presets);

// Read-only result list: one row per entry, cells shown in tab-aligned
// columns. Copy puts the selected rows (or all) on the clipboard. With
// `addRows`, two buttons hand the selected or all row indices to it; it
// returns how many were added.
using AddRows = std::function<int(const std::vector<int>& rows)>;
void ShowList(HWND owner, const std::wstring& caption, const std::wstring& summary,
              const std::vector<std::vector<std::wstring>>& rows, AddRows addRows = {});

// Dark title bar + dark control themes for any top-level window.
void ApplyDarkFrame(HWND hwnd);

}  // namespace dialogs
