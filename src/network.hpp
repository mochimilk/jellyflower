// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "volume.hpp"
namespace acrylic {
struct NetworkTarget {
    winrt::Windows::UI::Xaml::Controls::Grid slot{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock glyph{nullptr};
    explicit operator bool() const noexcept { return slot && glyph; }
};
NetworkTarget FindNetworkTarget(const winrt::Windows::UI::Xaml::DependencyObject& object, XamlClassName className = nullptr);
bool ConnectedNetworkGlyph(const winrt::hstring& value) noexcept;
class NetworkView {
public:
    explicit NetworkView(const NetworkTarget& target);
    ~NetworkView();
    NetworkView(const NetworkView&) = delete;
    NetworkView& operator=(const NetworkView&) = delete;
    void Refresh(bool enabled);
    void Restore();
    bool Applied() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
