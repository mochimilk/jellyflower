// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <memory>
namespace acrylic {
// The virtualized app list caches a separate header measurement. Reopening an
// offscreen Start page or changing its pinned/recommended content must refresh
// that cache, even when the app grid's own dimensions did not change.
class StartHeader {
    sx::FrameworkElement header_;
    sx::DispatcherTimer timer_{nullptr};
    winrt::event_token tick_{},size_{},layout_{},loaded_{};
    struct Pending {StartHeader* owner;};std::shared_ptr<Pending> pending_;
    bool applied_=false,inViewport_=false;
    unsigned passes_=0;
    void Queue() noexcept {try{if(applied_ && timer_ && !timer_.IsEnabled())timer_.Start();}catch(...) {}}
    void ObserveViewport() noexcept {
        try {
            const auto root=header_.XamlRoot();if(!root)return;
            const auto point=header_.TransformToVisual(nullptr).TransformPoint({0,0});const auto size=root.Size();
            const bool inside=point.X<size.Width && point.Y<size.Height
                && point.X+header_.ActualWidth()>0 && point.Y+header_.ActualHeight()>0;
            if(inside && !inViewport_)Queue();inViewport_=inside;
        }catch(...){}
    }
    void Measure() noexcept {
        try {
            auto parent=sx::Media::VisualTreeHelper::GetParent(header_);
            sx::Controls::ItemsPresenter presenter{nullptr};
            for(unsigned i=0;parent && i<12;++i){
                if((presenter=parent.try_as<sx::Controls::ItemsPresenter>()))break;
                parent=sx::Media::VisualTreeHelper::GetParent(parent);
            }
            if(!presenter)return;
            ++passes_;header_.InvalidateMeasure();header_.InvalidateArrange();
            // Invalidate the complete header branch, including the native
            // ContentControl/ContentPresenter and the virtualizing presenter.
            // Never force UpdateLayout from a layout callback or reset scrolling.
            parent=sx::Media::VisualTreeHelper::GetParent(header_);
            for(unsigned i=0;parent && i<12;++i){
                if(auto ui=parent.try_as<sx::UIElement>()){ui.InvalidateMeasure();ui.InvalidateArrange();}
                if(parent==presenter)break;
                parent=sx::Media::VisualTreeHelper::GetParent(parent);
            }
        }catch(...) {}
    }
public:
    explicit StartHeader(const sx::FrameworkElement& header):header_(header){}
    ~StartHeader(){Restore();}
    StartHeader(const StartHeader&)=delete;StartHeader& operator=(const StartHeader&)=delete;
    void Refresh(bool enabled) {
        if(!enabled){Restore();return;}if(applied_){ObserveViewport();return;}
        try {
            applied_=true;timer_=sx::DispatcherTimer();timer_.Interval(std::chrono::milliseconds(1));
            pending_=std::make_shared<Pending>(Pending{this});
            tick_=timer_.Tick([weak=std::weak_ptr<Pending>(pending_)](auto&&,auto&&){if(auto p=weak.lock();p && p->owner){auto self=p->owner;self->timer_.Stop();self->Measure();}});
            size_=header_.SizeChanged([this](auto&&,auto&&){Queue();});
            loaded_=header_.Loaded([this](auto&&,auto&&){Queue();});
            layout_=header_.LayoutUpdated([this](auto&&,auto&&){ObserveViewport();});
            ObserveViewport();
            Queue();
        }catch(...){Restore();throw;}
    }
    void Restore() noexcept {
        applied_=false;if(pending_)pending_->owner=nullptr;pending_.reset();
        try{if(timer_){timer_.Stop();if(tick_.value)timer_.Tick(tick_);}}catch(...){}tick_={};timer_=nullptr;
        try{if(size_.value)header_.SizeChanged(size_);if(loaded_.value)header_.Loaded(loaded_);if(layout_.value)header_.LayoutUpdated(layout_);}catch(...){}
        size_=layout_=loaded_={};inViewport_=false;
    }
    bool Applied()const noexcept{return applied_;}
    unsigned LayoutPasses()const noexcept{return passes_;}
};
}
