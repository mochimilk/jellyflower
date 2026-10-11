// SPDX-License-Identifier: GPL-3.0-or-later
#include "branding.hpp"
#include "resource.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>

namespace acrylic {
void RefreshAppBranding(HWND window, bool light) {
    // Shared resource handles live for the process lifetime.
    const int resource = light ? IDI_APP_LIGHT : IDI_APP_DARK;
    for (const auto size : {ICON_SMALL, ICON_BIG}) {
        const int pixels = GetSystemMetrics(size == ICON_SMALL ? SM_CXSMICON : SM_CXICON);
        const auto icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_ICON, pixels, pixels, LR_SHARED);
        if (icon) SendMessageW(window, WM_SETICON, size, reinterpret_cast<LPARAM>(icon));
    }
    // The installed Start menu shortcut can use the corresponding themed ICO.
    // Explorer's embedded EXE icon itself is static, as with other native EXEs.
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) return;
    const std::wstring shortcut = std::wstring(programs) + L"\\jellyflower.lnk";
    CoTaskMemFree(programs);
    if (GetFileAttributesW(shortcut.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr; IPersistFile* file = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && SUCCEEDED(file->Load(shortcut.c_str(), STGM_READWRITE))) {
            wchar_t target[32768]{}, self[32768]{};
            GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
            if (SUCCEEDED(link->GetPath(target, static_cast<int>(std::size(target)), nullptr, SLGP_RAWPATH)) && _wcsicmp(target, self) == 0) {
                std::wstring icon(self); icon.resize(icon.find_last_of(L'\\') + 1);
                icon += light ? L"app-light.ico" : L"app-dark.ico";
                if (GetFileAttributesW(icon.c_str()) != INVALID_FILE_ATTRIBUTES && SUCCEEDED(link->SetIconLocation(icon.c_str(), 0))
                    && SUCCEEDED(file->Save(shortcut.c_str(), TRUE))) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shortcut.c_str(), nullptr);
            }
        }
        if (file) file->Release(); link->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
}
