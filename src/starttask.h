// Start without a UAC prompt: scheduled tasks that run this exe with the
// highest privileges for the current user. Creating them needs one elevated
// run (the app always is); afterwards the tasks start it without asking.
//
//   "NIC Manager"            on demand; the desktop shortcut runs it
//   "NIC Manager Autostart"  at logon, with --minimized
#pragma once

#include <string>

namespace starttask {

bool IsInstalled();         // the on-demand task exists
bool AutostartInstalled();  // the logon task exists

// Creates the tasks (and the shortcut). Empty string on success, else the reason.
std::wstring Install(bool autostart, bool desktopShortcut);
// Removes both tasks and the desktop shortcut.
std::wstring Remove();

}  // namespace starttask
