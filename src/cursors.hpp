// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include "cursor_pack.hpp"

namespace acrylic {
inline constexpr UINT CursorDefault = 0, CursorDisabled = 10;
struct CursorColor { const wchar_t* folder; const wchar_t* label; };
inline constexpr std::array<CursorColor, 9> CursorColors{{
    {L"light", L"light"}, {L"dark", L"dark"}, {L"blueberry", L"blueberry"},
    {L"greentea", L"green tea"}, {L"mint", L"mint"}, {L"strawberry", L"strawberry"},
    {L"thaitea", L"thai tea"}, {L"vanilla", L"vanilla"}, {L"yogurt", L"yogurt"}
}};
class CursorManager {
public:
    HICON MenuIcon(int pixels) const;
    explicit CursorManager(std::wstring importFolder = CursorDataFolder());
    ~CursorManager();
    void Refresh();
    DWORD Select(UINT mode);
    // Hover previews never change Mode() or the stored preference.
    DWORD Preview(UINT mode);
    DWORD EndPreview();
    UINT Mode() const { return mode_; }
    DWORD Error() const { return error_; }
    bool Applied() const { return applied_; }
    bool SaveFailed() const { return saveFailed_; }
    UINT Available() const { return (available_ ? (1u << CursorDisabled) - 1 : 0) | (1u << CursorDisabled); }
    bool TakeImportResult(DWORD& error);
    static DWORD ConfigurePack(const wchar_t* folder);
    static DWORD VerifyPack(const std::wstring& folder);
    static const wchar_t* Name(UINT mode);
private:
    DWORD Apply(UINT mode, bool light);
    DWORD RestoreSelection();
    std::wstring folder_;
    CursorPackImporter importer_;
    UINT mode_ = CursorDefault;
    DWORD error_ = 0;
    bool applied_ = false, touched_ = false, started_ = false, light_ = false, saveFailed_ = false;
    bool available_ = false, importNotice_ = false;
    DWORD importError_ = 0;
    UINT previewMode_ = CursorDisabled;
    bool previewing_ = false, previewWindows_ = false;
};
} // namespace acrylic
