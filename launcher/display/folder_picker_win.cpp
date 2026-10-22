#include "launcher/display/folder_picker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>

#include <string>

namespace launcher::display
{
namespace
{
std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

std::string wideToUtf8(const wchar_t* text)
{
    if (!text || !*text)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}
} // namespace

std::string itemPath(IShellItem* item)
{
    if (!item)
        return {};
    PWSTR widePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)) || !widePath)
        return {};
    std::string result = wideToUtf8(widePath);
    CoTaskMemFree(widePath);
    return result;
}

bool pickFolderNative(const std::string& initialDir,
                      std::string& path, std::string& error,
                      std::string* lastDir)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                    COINIT_DISABLE_OLE1DDE);
    const bool uninit  = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
    {
        error = "CoInitializeEx failed";
        return false;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog)
    {
        if (uninit)
            CoUninitialize();
        error = "could not create the folder dialog";
        return false;
    }

    DWORD options = 0;
    hr = dialog->GetOptions(&options);
    if (SUCCEEDED(hr))
        hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                FOS_PATHMUSTEXIST);
    if (SUCCEEDED(hr))
        hr = dialog->SetTitle(L"选择 ROM 游戏库目录");

    if (SUCCEEDED(hr) && !initialDir.empty())
    {
        const std::wstring wideDir = utf8ToWide(initialDir);
        IShellItem* folder = nullptr;
        if (!wideDir.empty() &&
            SUCCEEDED(SHCreateItemFromParsingName(wideDir.c_str(), nullptr,
                                                  IID_PPV_ARGS(&folder))))
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    hr = SUCCEEDED(hr) ? dialog->Show(nullptr) : hr;
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        // Remember where the user left the dialog even when they cancel.
        if (lastDir)
        {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(dialog->GetFolder(&folder)) && folder)
            {
                *lastDir = itemPath(folder);
                folder->Release();
            }
        }
        dialog->Release();
        if (uninit)
            CoUninitialize();
        return false; // cancelled
    }
    if (FAILED(hr))
    {
        dialog->Release();
        if (uninit)
            CoUninitialize();
        error = "the folder dialog failed to open";
        return false;
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (FAILED(hr) || !item)
    {
        dialog->Release();
        if (uninit)
            CoUninitialize();
        error = "no folder was returned";
        return false;
    }

    PWSTR widePath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &widePath);
    item->Release();
    dialog->Release();
    if (uninit)
        CoUninitialize();
    if (FAILED(hr) || !widePath)
    {
        error = "could not read the selected folder path";
        return false;
    }

    path = wideToUtf8(widePath);
    CoTaskMemFree(widePath);
    if (path.empty())
    {
        error = "could not convert the selected folder path";
        return false;
    }
    if (lastDir)
        *lastDir = path;
    return true;
}
} // namespace launcher::display
