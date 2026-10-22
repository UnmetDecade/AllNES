#include "launcher/display/folder_picker.h"

#import <Cocoa/Cocoa.h>

namespace launcher::display
{
bool pickFolderNative(const std::string& initialDir,
                      std::string& path, std::string& error,
                      std::string* lastDir)
{
    @autoreleasepool
    {
        if (!NSApp)
        {
            error = "no NSApplication (the launcher is not running in a window)";
            return false;
        }

        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles          = NO;
        panel.canChooseDirectories    = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories    = YES;
        panel.prompt                  = @"选择";
        panel.message                 = @"选择 ROM 游戏库目录";

        if (!initialDir.empty())
        {
            NSString* dir = [NSString stringWithUTF8String:initialDir.c_str()];
            if (dir)
                panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];
        }

        [NSApp activateIgnoringOtherApps:YES];
        const NSModalResponse response = [panel runModal];

        if (response != NSModalResponseOK)
        {
            // Keep the directory the user browsed to, so reopening the panel
            // resumes there even though nothing was selected.
            if (lastDir)
            {
                NSString* shown = panel.directoryURL.path;
                const char* shownPath = shown ? shown.fileSystemRepresentation : nullptr;
                if (shownPath)
                    *lastDir = shownPath;
            }
            return false; // cancelled
        }

        const char* selected = panel.URL.fileSystemRepresentation;
        if (!selected || !*selected)
        {
            error = "selected folder has no filesystem path";
            return false;
        }
        path = selected;
        if (lastDir)
            *lastDir = path;
        return true;
    }
}
} // namespace launcher::display
