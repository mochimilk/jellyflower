// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <array>
namespace acrylic {
inline bool IsStartGrid(const sx::Controls::GridView& view,StartClassName name=nullptr) {
    if(!view || (view.Name()!=L"AllAppsGrid" && view.Name()!=L"PinnedList" && view.Name()!=L"RecommendedList")) return false;
    auto p=sx::Media::VisualTreeHelper::GetParent(view);
    for(unsigned i=0;p && i<48;++i) {
        const auto type=name?name(p):winrt::get_class_name(p);
        const std::wstring_view text(type);
        if(text.substr(0,10)==L"StartMenu." || text.substr(0,12)==L"StartDocked.") return true;
        p=sx::Media::VisualTreeHelper::GetParent(p);
    }
    return false;
}
inline unsigned StartColumns(double width,double nativeCell,int maximum) noexcept {
    if(!std::isfinite(width) || !std::isfinite(nativeCell) || width<=0 || nativeCell<=0) return 0;
    unsigned columns=static_cast<unsigned>(std::clamp(std::round(width/nativeCell),1.0,64.0));
    if(maximum>0) columns=std::min(columns,static_cast<unsigned>(maximum));
    return columns;
}
// The shell changes templates and fixed container widths between category,
// name-grid, name-list and Recommended. Own just each top-level GridView's
// ItemsPanelRoot; never reach into a folder, category's mini-grid or jump list.
class StartGrid {
    struct Cell {
        sx::Controls::GridViewItem element;
        StartSearchValues values,content;
        sx::FrameworkElement glyph{nullptr};
        explicit Cell(const sx::Controls::GridViewItem& item,bool recommended):element(item) {
            for(auto key:{sx::FrameworkElement::WidthProperty(),sx::FrameworkElement::MinWidthProperty(),sx::FrameworkElement::MaxWidthProperty()}) {
                const double value=key==sx::FrameworkElement::MinWidthProperty()?0:
                    key==sx::FrameworkElement::MaxWidthProperty()?std::numeric_limits<double>::infinity():std::numeric_limits<double>::quiet_NaN();
                values.Set(item,key,winrt::box_value(value));
            }
            values.Set(item,sx::Controls::Control::HorizontalContentAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
            values.Set(item,sx::FrameworkElement::MarginProperty(),nullptr,[recommended](const auto& value){
                auto m=winrt::unbox_value<sx::Thickness>(value);
                // Keep category gaps, split equally across the cell. List and
                // recommendation rows use their own native 12-DIP inner padding.
                m.Left=m.Right=recommended?0:(m.Left+m.Right)/2;return winrt::box_value(m);
            });
        }
        double NativeWidth() const {
            const auto number=[&](auto key){return winrt::unbox_value<double>(values.Original(element,key));};
            double width=number(sx::FrameworkElement::WidthProperty());
            if(!std::isfinite(width) || width<=0) width=number(sx::FrameworkElement::MinWidthProperty());
            if(!std::isfinite(width) || width<=0) width=number(sx::FrameworkElement::MaxWidthProperty());
            if(!std::isfinite(width) || width<=0) return 0; // Unknown template: keep its stock layout.
            auto m=winrt::unbox_value<sx::Thickness>(values.Original(element,sx::FrameworkElement::MarginProperty()));
            return width+std::max(0.0,m.Left)+std::max(0.0,m.Right);
        }
        void Content(StartClassName name) {
            if(glyph && sx::Media::VisualTreeHelper::GetParent(glyph))return;
            sx::FrameworkElement found{nullptr};std::vector<sx::DependencyObject> nodes{element};
            for(size_t i=0;i<nodes.size() && i<64;++i) {
                auto f=nodes[i].try_as<sx::FrameworkElement>();if(!f)continue;
                const auto type=name?name(f):winrt::get_class_name(f);
                if(type==L"StartMenu.CategoryControl" || type==L"StartMenu.PinnedListTile" || type==L"StartMenu.PinnedListCollapsedFolder") {found=f;break;}
                if(i && f.try_as<sx::Controls::ItemsControl>()) continue;
                for(int c=0;c<sx::Media::VisualTreeHelper::GetChildrenCount(f) && nodes.size()<64;++c)nodes.push_back(sx::Media::VisualTreeHelper::GetChild(f,c));
            }
            if(found==glyph) return;
            content.Clear();glyph=found;if(!found)return;
            content.Set(found,sx::FrameworkElement::WidthProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
            content.Set(found,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));
            content.Set(found,sx::FrameworkElement::MaxWidthProperty(),winrt::box_value(std::numeric_limits<double>::infinity()));
            content.Set(found,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
        }
    };
    sx::Controls::GridView view_;
    winrt::hstring id_;
    sx::FrameworkElement pane_{nullptr};
    sx::Controls::ItemsWrapGrid panel_{nullptr};
    StartSearchValues viewValues_,panelValues_;
    std::unordered_map<void*,std::unique_ptr<Cell>> cells_;
    winrt::event_token layout_{};
    sx::DispatcherTimer timer_{nullptr};
    winrt::event_token tick_{};
    struct TickState {StartGrid* owner;};
    std::shared_ptr<TickState> pending_;
    StartClassName name_;
    bool applied_=false,updating_=false;
    double slot_=0;
    unsigned passes_=0;
    double insetLeft_=0,insetRight_=0;
    struct Observation {
        void *panel=nullptr,*first=nullptr,*last=nullptr;
        uint32_t count=0;
        std::array<double,11> geometry{};
        bool operator==(const Observation& o)const {return panel==o.panel && first==o.first && last==o.last && count==o.count && geometry==o.geometry;}
    } observed_;
    bool observedValid_=false;
    Observation Observe() {
        Observation o;auto panel=view_.ItemsPanelRoot().try_as<sx::Controls::ItemsWrapGrid>();o.panel=winrt::get_abi(panel);
        auto margin=view_.Margin();
        o.geometry={view_.ActualWidth(),view_.ActualHeight(),margin.Left,margin.Right};
        if(!pane_){auto p=sx::Media::VisualTreeHelper::GetParent(view_);for(unsigned i=0;p && i<48;++i){auto f=p.try_as<sx::FrameworkElement>();if(f && f.Name()==L"MainContent"){pane_=f;break;}p=sx::Media::VisualTreeHelper::GetParent(p);}}
        if(pane_){o.geometry[4]=pane_.ActualWidth();o.geometry[5]=view_.TransformToVisual(pane_).TransformPoint({0,0}).X;}
        if(panel){
            const auto children=panel.Children();o.count=children.Size();
            if(o.count){o.first=winrt::get_abi(children.GetAt(0).try_as<sx::Controls::GridViewItem>());o.last=winrt::get_abi(children.GetAt(o.count-1));}
            auto m=panel.Margin();o.geometry[6]=panel.ActualWidth();o.geometry[7]=m.Left;o.geometry[8]=m.Right;
            auto cell=cells_.find(o.first);if(cell!=cells_.end())o.geometry[9]=cell->second->NativeWidth();
            auto source=winrt::unbox_value<double>(panelValues_.Original(panel,sx::Controls::ItemsWrapGrid::ItemWidthProperty()));o.geometry[10]=std::isfinite(source)?source:0;
        }
        return o;
    }
    void Schedule() noexcept {
        // LayoutUpdated runs inside the shell's measure/arrange pass. Coalesce
        // changes onto a later UI turn instead of invalidating that same pass.
        try {
            if(!applied_ || !timer_)return;
            auto current=Observe();if(observedValid_ && current==observed_)return;
            observed_=current;observedValid_=true;
            if(!timer_.IsEnabled())timer_.Start();
        }catch(...){}
    }
    void Layout() noexcept {
        if(!applied_ || updating_)return;
        ++passes_;
        updating_=true;
        try {
            auto panel=view_.ItemsPanelRoot().try_as<sx::Controls::ItemsWrapGrid>();
            if(panel!=panel_) {
                cells_.clear();panelValues_.Clear();panel_=panel;slot_=0;
                if(panel_) {
                    panelValues_.Set(panel_,sx::FrameworkElement::MarginProperty(),nullptr,[](const auto& value){
                        auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=m.Right=12;return winrt::box_value(m);
                    });
                }
            }
            if(panel_) {
                sx::FrameworkElement pane{nullptr};auto p=sx::Media::VisualTreeHelper::GetParent(view_);
                for(unsigned i=0;p && i<48;++i) {
                    auto f=p.try_as<sx::FrameworkElement>();if(f && f.Name()==L"MainContent"){pane=f;break;}
                    p=sx::Media::VisualTreeHelper::GetParent(p);
                }
                if(pane && pane.ActualWidth()>0 && view_.ActualWidth()>0) {
                    if(id_==L"RecommendedList" && view_.Width()!=pane.ActualWidth())
                        viewValues_.Update(view_,sx::FrameworkElement::WidthProperty(),winrt::box_value(pane.ActualWidth()));
                    auto pos=view_.TransformToVisual(pane).TransformPoint({0,0});auto m=view_.Margin();
                    if(view_.Name()!=L"PinnedList") {
                        // Some Recommended layouts are hosted in a React panel
                        // with its own old inset. Native negative margins expand
                        // the actual list allocation; its clicks/scrolling follow.
                        const double left=pos.X-m.Left,right=pane.ActualWidth()-pos.X-view_.ActualWidth()-m.Right;
                        if(std::abs(left-insetLeft_)>.5 || std::abs(right-insetRight_)>.5) {
                            insetLeft_=left;insetRight_=right;
                            auto original=winrt::unbox_value<sx::Thickness>(viewValues_.Original(view_,sx::FrameworkElement::MarginProperty()));
                            original.Left=-left;original.Right=-right;
                            viewValues_.Update(view_,sx::FrameworkElement::MarginProperty(),winrt::box_value(original));
                        }
                    } else {
                        // Keep the native pinned page indicator's column free,
                        // reserving the same space at the opposite side.
                        const double right=pane.ActualWidth()-pos.X-view_.ActualWidth();
                        const double edge=std::max({12.0,static_cast<double>(pos.X),right});
                        auto margin=panel_.Margin();margin.Left=edge-pos.X;margin.Right=edge-right;
                        const auto current=panel_.Margin();
                        if(std::abs(current.Left-margin.Left)>.5 || std::abs(current.Right-margin.Right)>.5)
                            panelValues_.Update(panel_,sx::FrameworkElement::MarginProperty(),winrt::box_value(margin));
                    }
                }
                std::vector<void*> live;double native=0;
                for(const auto& child:panel_.Children()) {
                    auto item=child.try_as<sx::Controls::GridViewItem>();if(!item)continue;
                    const auto key=winrt::get_abi(item);live.push_back(key);
                    auto& cell=cells_[key];if(!cell) {
                        if((!std::isfinite(item.Width()) || item.Width()<=0) && item.MinWidth()<=0
                            && (!std::isfinite(item.MaxWidth()) || item.MaxWidth()<=0)) {cells_.erase(key);continue;}
                        cell=std::make_unique<Cell>(item,view_.Name()==L"RecommendedList");
                    }
                    const auto width=cell->NativeWidth();
                    if(width<230 && view_.Name()!=L"RecommendedList")cell->Content(name_);
                    if(!native)native=width;
                }
                for(auto i=cells_.begin();i!=cells_.end();) {
                    if(std::find(live.begin(),live.end(),i->first)==live.end())i=cells_.erase(i);else ++i;
                }
                auto source=winrt::unbox_value<double>(panelValues_.Original(panel_,sx::Controls::ItemsWrapGrid::ItemWidthProperty()));
                if(std::isfinite(source) && source>0)native=source;
                // ActualWidth on a wrap panel can be its content extent. Feeding
                // that back into ItemWidth creates a measure/arrange cycle.
                // Use the stable scroll viewport instead, excluding panel margins.
                double width=view_.ActualWidth()-view_.Padding().Left-view_.Padding().Right;
                auto ancestor=sx::Media::VisualTreeHelper::GetParent(panel_);
                for(unsigned i=0;ancestor && ancestor!=view_ && i<16;++i) {
                    if(auto viewport=ancestor.try_as<sx::Controls::ScrollContentPresenter>()) {width=viewport.ActualWidth();break;}
                    ancestor=sx::Media::VisualTreeHelper::GetParent(ancestor);
                }
                width=std::max(0.0,width-panel_.Margin().Left-panel_.Margin().Right);
                const auto columns=StartColumns(width,native,panel_.MaximumRowsOrColumns());
                if(columns) {
                    const double slot=width/columns;
                    if(std::abs(slot-slot_)>.01) {
                        panelValues_.Update(panel_,sx::Controls::ItemsWrapGrid::ItemWidthProperty(),winrt::box_value(slot));slot_=slot;
                    }
                }
            }
        }catch(...){/* Unknown/retemplating trees are retried on the next layout. */}
        updating_=false;
    }
public:
    explicit StartGrid(const sx::Controls::GridView& view,StartClassName name=nullptr):view_(view),id_(view.Name()),name_(name) {}
    ~StartGrid(){Restore();}
    StartGrid(const StartGrid&)=delete;StartGrid& operator=(const StartGrid&)=delete;
    void Refresh(bool enabled) {
        if(!enabled){Restore();return;}
        if(!applied_) {
            applied_=true;
            try {
                Observe();
                // React owns Recommended's explicit layout metrics. Keep a
                // concrete width there: NaN makes its auto-size feedback loop
                // alternate between Yoga's width and the expanded XAML width.
                const double width=id_==L"RecommendedList" && pane_ && pane_.ActualWidth()>0?pane_.ActualWidth():std::numeric_limits<double>::quiet_NaN();
                viewValues_.Set(view_,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));
                viewValues_.Set(view_,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));
                viewValues_.Set(view_,sx::FrameworkElement::MaxWidthProperty(),winrt::box_value(std::numeric_limits<double>::infinity()));
                viewValues_.Set(view_,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
                if(view_.Name()!=L"PinnedList")viewValues_.Set(view_,sx::FrameworkElement::MarginProperty(),nullptr,[this](const auto& value){
                    auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=-insetLeft_;m.Right=-insetRight_;return winrt::box_value(m);
                });
                timer_=sx::DispatcherTimer();timer_.Interval(std::chrono::milliseconds(1));
                pending_=std::make_shared<TickState>(TickState{this});
                tick_=timer_.Tick([weak=std::weak_ptr<TickState>(pending_)](auto&&,auto&&){
                    if(auto live=weak.lock();live && live->owner){auto self=live->owner;self->timer_.Stop();self->Layout();}
                });
                layout_=view_.LayoutUpdated([this](auto&&,auto&&){Schedule();});
            }catch(...){Restore();throw;}
        }
        Schedule();
    }
    void Restore() noexcept {
        applied_=false;
        if(pending_)pending_->owner=nullptr;pending_.reset();
        try{if(timer_){timer_.Stop();if(tick_.value)timer_.Tick(tick_);}}catch(...){}tick_={};timer_=nullptr;
        try{if(layout_.value)view_.LayoutUpdated(layout_);}catch(...){}layout_={};
        cells_.clear();panelValues_.Clear();panel_=nullptr;pane_=nullptr;viewValues_.Clear();slot_=0;insetLeft_=insetRight_=0;observedValid_=false;
    }
    bool Applied()const noexcept{return applied_;}
    unsigned LayoutPasses()const noexcept{return passes_;}
    std::wstring_view Name()const noexcept{return id_;}
};
}
