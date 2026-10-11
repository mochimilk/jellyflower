// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include <array>

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
    CursorManager();
    ~CursorManager();
    void Refresh();
    DWORD Select(UINT mode);
    UINT Mode() const { return mode_; }
    DWORD Error() const { return error_; }
    bool Applied() const { return applied_; }
    bool SaveFailed() const { return saveFailed_; }
    static DWORD ConfigurePack(const wchar_t* folder);
    static DWORD VerifyPack(const std::wstring& folder);
    static const wchar_t* Name(UINT mode);
private:
    DWORD Apply(UINT mode, bool light);
    std::wstring folder_;
    UINT mode_ = CursorDefault;
    DWORD error_ = 0;
    bool applied_ = false, touched_ = false, started_ = false, light_ = false, saveFailed_ = false;
};
} // namespace acrylic
