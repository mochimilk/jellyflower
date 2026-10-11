// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_style.hpp"
#include <functional>
namespace acrylic {
inline sx::FrameworkElement FindStartElement(const sx::DependencyObject& root, std::wstring_view name) {
    std::vector<sx::DependencyObject> pending{root};
    for (size_t i = 0; i < pending.size() && i < 12000; ++i) {
        if (auto element = pending[i].try_as<sx::FrameworkElement>(); element && element.Name() == name) return element;
        const auto count = sx::Media::VisualTreeHelper::GetChildrenCount(pending[i]);
        for (int child = 0; child < count && pending.size() < 12000; ++child)
            pending.push_back(sx::Media::VisualTreeHelper::GetChild(pending[i], child));
    }
    return nullptr;
}
// Each override owns its original local value/binding. Shell visual-state and
// localization changes become the new originals while the override is active.
class StartSearchValues {
    struct Item {
        sx::FrameworkElement object{nullptr};
        sx::DependencyProperty key{nullptr};
        winrt::Windows::Foundation::IInspectable local{nullptr}, custom{nullptr};
        sx::Data::BindingBase binding{nullptr};
        int64_t token = 0;
        std::function<winrt::Windows::Foundation::IInspectable(const winrt::Windows::Foundation::IInspectable&)> transform;
    };
    std::vector<Item> items_;
    bool writing_ = false;
    void Save(Item& item) {
        item.local = item.object.ReadLocalValue(item.key);
        const auto expression = item.object.GetBindingExpression(item.key);
        item.binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Changed(size_t index) noexcept {
        if (writing_) return;
        try {
            auto& item = items_[index]; Save(item);
            if (item.transform) item.custom = item.transform(item.object.GetValue(item.key));
            writing_ = true;
            item.object.SetValue(item.key, item.custom); writing_ = false;
        } catch (...) { writing_ = false; }
    }
public:
    StartSearchValues() = default;
    StartSearchValues(const StartSearchValues&) = delete;
    StartSearchValues& operator=(const StartSearchValues&) = delete;
    ~StartSearchValues() { Clear(); }
    void Set(const sx::FrameworkElement& object, const sx::DependencyProperty& key,
        const winrt::Windows::Foundation::IInspectable& custom,
        std::function<winrt::Windows::Foundation::IInspectable(const winrt::Windows::Foundation::IInspectable&)> transform = {}) {
        const auto index = items_.size();
        Item item{object, key, nullptr, custom}; Save(item); item.transform = std::move(transform);
        if (item.transform) item.custom = item.transform(object.GetValue(key));
        items_.push_back(item);
        items_.back().token = object.RegisterPropertyChangedCallback(key, [this, index](auto&&, auto&&) { Changed(index); });
        writing_ = true;
        try { object.SetValue(key, items_.back().custom); writing_ = false; }
        catch (...) { writing_ = false; throw; }
    }
    void Clear() noexcept {
        writing_ = true;
        for (auto& item : items_) {
            try { if (item.token) item.object.UnregisterPropertyChangedCallback(item.key, item.token); } catch (...) {}
            try {
                item.object.ClearValue(item.key);
                if (item.binding) item.object.SetBinding(item.key, item.binding);
                else if (item.local != sx::DependencyProperty::UnsetValue()) item.object.SetValue(item.key, item.local);
            } catch (...) {}
        }
        items_.clear(); writing_ = false;
    }
};
}
