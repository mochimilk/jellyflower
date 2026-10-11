// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <memory>
#include <string>
#include "clock_options.hpp"

namespace acrylic {
std::wstring FormatClock(const SYSTEMTIME& time, bool date = true);

// Owns only presentation changes inside the stock clock's content. The shell
// keeps its button, bindings, tooltip, accessible name, and calendar handlers.
class ClockView {
public:
    explicit ClockView(const winrt::Windows::UI::Xaml::Controls::StackPanel& panel);
    ~ClockView();
    ClockView(const ClockView&) = delete;
    ClockView& operator=(const ClockView&) = delete;
    void Refresh(bool enabled, ClockOptions options = {});
    void Restore();
    bool Applied() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Only accepts the expected pair of named, direct TextBlock children.
bool IsClockPanel(const winrt::Windows::UI::Xaml::Controls::StackPanel& panel);
}
