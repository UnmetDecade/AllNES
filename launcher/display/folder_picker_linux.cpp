#include "launcher/display/folder_picker.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

namespace launcher::display
{
namespace
{
std::string shellQuote(const std::string& text)
{
    std::string quoted = "'";
    for (const char c : text)
    {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

// Runs a picker command and captures its stdout. Returns true on selection,
// false on cancel or when the command is unavailable.
bool runPicker(const std::string& command, std::string& path)
{
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return false;

    std::string output;
    char buffer[1024];
    while (std::fgets(buffer, sizeof buffer, pipe))
        output += buffer;

    const int status = pclose(pipe);
    if (!WIFEXITED(status))
        return false;

    const int code = WEXITSTATUS(status);
    if (code != 0)
        return false; // 1 = cancel, 127 = command not found

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    if (output.empty())
        return false;

    path = output;
    return true;
}

bool commandAvailable(const char* name)
{
    const std::string command = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}
} // namespace

bool pickFolderNative(const std::string& initialDir,
                      std::string& path, std::string& error,
                      std::string* lastDir)
{
    const std::string start = initialDir.empty() ? std::string()
                                                 : shellQuote(initialDir);

    if (commandAvailable("zenity"))
    {
        std::string command =
            "zenity --file-selection --directory --title=选择游戏库目录";
        if (!start.empty())
            command += " --filename=" + start + "/";
        if (runPicker(command, path))
        {
            if (lastDir)
                *lastDir = path;
            return true;
        }
        // zenity ran but the user cancelled: there is no way to query where
        // they browsed to, so resume at the directory we opened with.
        if (lastDir)
            *lastDir = initialDir;
        return false;
    }

    if (commandAvailable("kdialog"))
    {
        std::string command = "kdialog --getexistingdirectory ";
        command += start.empty() ? "\"$HOME\"" : start;
        command += " --title 选择游戏库目录";
        if (runPicker(command, path))
        {
            if (lastDir)
                *lastDir = path;
            return true;
        }
        if (lastDir)
            *lastDir = initialDir;
        return false;
    }

    error = "未找到 zenity 或 kdialog，请安装其一，或用 --library 指定游戏库目录";
    return false;
}
} // namespace launcher::display
