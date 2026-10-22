#pragma once

#include <string>

// Platform-native directory chooser used by the SDL display backend. Each
// platform provides its own definition of pickFolderNative():
//   macOS   - NSOpenPanel (AppKit)
//   Windows - IFileOpenDialog with FOS_PICKFOLDERS
//   Linux   - zenity or kdialog, whichever is installed
//
// Returns true and fills `path` when the user picks a folder. `initialDir`
// seeds the dialog when non-empty. Returns false on cancel with `error`
// untouched, or with `error` set when the picker cannot be opened at all.
// When `lastDir` is given it receives the directory the dialog was showing
// when it closed, including on cancel, so the next call can resume there.
namespace launcher::display
{
bool pickFolderNative(const std::string& initialDir,
                      std::string& path, std::string& error,
                      std::string* lastDir = nullptr);
} // namespace launcher::display
