// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <algorithm>
namespace acrylic {
enum class StartSpacingRule { None, Heading, Left, Right, Both };
inline StartSpacingRule StartSpacingFor(const sx::FrameworkElement& element, StartClassName name = nullptr) {
    if (!element) return StartSpacingRule::None;
    bool start = false, list = false;
    const auto parent = sx::Media::VisualTreeHelper::GetParent(element).try_as<sx::FrameworkElement>();
    auto current = sx::Media::VisualTreeHelper::GetParent(element);
    for (unsigned depth=0; current && depth<48; ++depth) {
        const auto type = name ? name(current) : winrt::get_class_name(current);
        const std::wstring_view text(type);
        if (text.substr(0,10)==L"StartMenu." || text.substr(0,12)==L"StartDocked.") { start = true; break; }
        // Only the nearest list owns this ItemsWrapGrid. Never style folder or
        // app-item grids which happen to sit deeper inside the main app list.
        if (const auto grid = current.try_as<sx::Controls::GridView>()) {
            list = grid.Name()==L"AllAppsGrid" || grid.Name()==L"PinnedList";
            break;
        }
        current = sx::Media::VisualTreeHelper::GetParent(current);
    }
    if (!start) {
        current = sx::Media::VisualTreeHelper::GetParent(element);
        for (unsigned depth=0; current && depth<48; ++depth) {
            const auto type = name ? name(current) : winrt::get_class_name(current);
            const std::wstring_view text(type);
            if (text.substr(0,10)==L"StartMenu." || text.substr(0,12)==L"StartDocked.") {start=true;break;}
            current=sx::Media::VisualTreeHelper::GetParent(current);
        }
    }
    if (!start) return StartSpacingRule::None;
    const auto id = element.Name();
    if (element.try_as<sx::Controls::TextBlock>() && (id==L"PinnedListHeaderText" || id==L"AllListHeadingText"
        || id==L"TopLevelSuggestionsListHeaderText" || id==L"MoreSuggestionsListHeaderText" || id==L"ZoomedOutHeading")) return StartSpacingRule::Heading;
    if (element.try_as<sx::Controls::Grid>() && id==L"NavPanePlaceholder" && parent && parent.Name()==L"MainContent") return StartSpacingRule::Both;
    if (element.try_as<sx::Controls::Grid>() && id==L"TopLevelSuggestionsContainer") return StartSpacingRule::Left;
    if (element.try_as<sx::Controls::Control>() && (id==L"ViewSelectionButton" || id==L"ShowMoreSuggestionsButton"
        || id==L"ZoomInButton" || id==L"HideMoreSuggestionsButton" || (parent && parent.Name()==L"ShowMorePinnedGrid"))) return StartSpacingRule::Right;
    if (list && element.try_as<sx::Controls::ItemsWrapGrid>()) return StartSpacingRule::Both;
    return StartSpacingRule::None;
}
inline sx::Thickness CompactStartMargin(sx::Thickness margin, StartSpacingRule rule) {
    const auto inset=[](double original) {return original>0 ? std::max(12.0, original-40.0) : original;};
    if (rule==StartSpacingRule::Heading) margin.Left=24;
    else if (rule==StartSpacingRule::Left || rule==StartSpacingRule::Both) margin.Left=inset(margin.Left);
    if (rule==StartSpacingRule::Right || rule==StartSpacingRule::Both) margin.Right=inset(margin.Right);
    return margin;
}
class StartSpacing {
    sx::FrameworkElement element_;
    StartSpacingRule rule_;
    StartSearchValues values_;
    bool applied_=false;
public:
    StartSpacing(const sx::FrameworkElement& element,StartSpacingRule rule):element_(element),rule_(rule) {}
    void Refresh(bool enabled) {
        if (!enabled) {Restore();return;}
        if (applied_ || rule_==StartSpacingRule::None) return;
        values_.Set(element_,sx::FrameworkElement::MarginProperty(),nullptr,[rule=rule_](const auto& value) {
            return winrt::box_value(CompactStartMargin(winrt::unbox_value<sx::Thickness>(value),rule));
        });
        applied_=true;
    }
    void Restore() noexcept {values_.Clear();applied_=false;}
    bool Applied() const noexcept {return applied_;}
};
}
