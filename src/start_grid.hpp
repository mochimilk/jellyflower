// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "start_properties.hpp"
#include "start_metrics.hpp"
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
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
        const auto runtime=name?name(p):winrt::get_class_name(p);
        const std::wstring_view type(runtime);
        if(type.substr(0,10)==L"StartMenu." || type.substr(0,12)==L"StartDocked.") return true;
        p=sx::Media::VisualTreeHelper::GetParent(p);
    }
    return false;
}
inline unsigned StartColumns(double width,double nativeCell,int maximum) noexcept {
    if(!std::isfinite(width) || !std::isfinite(nativeCell) || width<=0 || nativeCell<=0) return 0;
    unsigned columns=static_cast<unsigned>(std::clamp(std::round(width/nativeCell),1.0,64.0));
    return maximum>0?std::min(columns,static_cast<unsigned>(maximum)):columns;
}
// Layout slots exclude the render transforms Windows uses to navigate between
// home/Show all. Hidden or animating pages must not move our layout anchors.
inline double StartLayoutX(sx::DependencyObject child,const sx::DependencyObject& pane) {
    double x=0;
    for(unsigned i=0;child && child!=pane && i<64;++i) {
        if(auto f=child.try_as<sx::FrameworkElement>())
            x+=sx::Controls::Primitives::LayoutInformation::GetLayoutSlot(f).X+f.Margin().Left;
        child=sx::Media::VisualTreeHelper::GetParent(child);
    }
    return x;
}
class StartGrid {
    struct Cell {
        sx::Controls::GridViewItem element;
        StartSearchValues values,content;
        sx::FrameworkElement root{nullptr},category{nullptr};
        std::vector<sx::FrameworkElement> roots;
        sx::Controls::Border square{nullptr};
        sx::Controls::Button categoryButton{nullptr};
        sx::Controls::ItemsWrapGrid mini{nullptr};
        std::vector<sx::FrameworkElement> miniTargets;
        double native=0,gutter=0,lastWidth=-1;
        bool pinned=false;
        double NativeWidth() {
            if(root){const double width=winrt::unbox_value<double>(content.Original(root,sx::FrameworkElement::WidthProperty()));
                if(std::isfinite(width) && width>0){const auto m=winrt::unbox_value<sx::Thickness>(content.Original(root,sx::FrameworkElement::MarginProperty()));return width+m.Left+m.Right+element.Padding().Left+element.Padding().Right;}}
            if(native<=0)native=element.ActualWidth();return native;
        }
        explicit Cell(const sx::Controls::GridViewItem& item,bool pin):element(item),pinned(pin) {
            // Real shell containers usually have no Width/MinWidth/MaxWidth.
            // Their content/template determines the stock slot, unlike our old
            // fixture. Capture it before changing either wrapper or content.
            native=item.ActualWidth()+item.Margin().Left+item.Margin().Right;
            if(!std::isfinite(native) || native<=0)native=item.DesiredSize().Width;
            for(auto key:{sx::FrameworkElement::WidthProperty(),sx::FrameworkElement::MinWidthProperty(),sx::FrameworkElement::MaxWidthProperty()})
                values.Set(item,key,winrt::box_value(key==sx::FrameworkElement::MinWidthProperty()?0:key==sx::FrameworkElement::MaxWidthProperty()?std::numeric_limits<double>::infinity():std::numeric_limits<double>::quiet_NaN()));
            values.Set(item,sx::Controls::Control::HorizontalContentAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
            values.Set(item,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
            if(pin) {
                values.Set(item,sx::FrameworkElement::HeightProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
                values.Set(item,sx::Controls::Control::VerticalContentAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Stretch));
            }
            values.Set(item,sx::FrameworkElement::MarginProperty(),nullptr,[this](const auto& value){
                auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=m.Right=gutter/2;
                if(pinned)m.Top=m.Bottom=StartMetrics::PinnedRowGap/2;
                return winrt::box_value(m);
            });
        }
        void Stretch(const sx::FrameworkElement& f,bool height=false,bool stripMargin=true) {
            if(!f || std::find(roots.begin(),roots.end(),f)!=roots.end())return;
            roots.push_back(f);
            content.Set(f,sx::FrameworkElement::WidthProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
            content.Set(f,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));
            content.Set(f,sx::FrameworkElement::MaxWidthProperty(),winrt::box_value(std::numeric_limits<double>::infinity()));
            content.Set(f,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(sx::HorizontalAlignment::Stretch));
            if(stripMargin)content.Set(f,sx::FrameworkElement::MarginProperty(),nullptr,[](const auto& value){auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=m.Right=0;return winrt::box_value(m);});
            if(height){
                content.Set(f,sx::FrameworkElement::HeightProperty(),winrt::box_value(std::numeric_limits<double>::quiet_NaN()));
                content.Set(f,sx::FrameworkElement::VerticalAlignmentProperty(),winrt::box_value(sx::VerticalAlignment::Stretch));
            }
        }
        void ResetContent() {content.Clear();roots.clear();root=nullptr;category=nullptr;square=nullptr;categoryButton=nullptr;mini=nullptr;miniTargets.clear();lastWidth=-1;}
        void Discover(StartClassName name) {
            if(root && sx::Media::VisualTreeHelper::GetParent(root))return;
            ResetContent();
            std::vector<sx::DependencyObject> nodes{element};
            for(size_t i=0;i<nodes.size() && i<96;++i) {
                auto f=nodes[i].try_as<sx::FrameworkElement>();if(!f)continue;
                const auto type=name?name(f):winrt::get_class_name(f);
                auto parent=sx::Media::VisualTreeHelper::GetParent(f).try_as<sx::Controls::ContentPresenter>();
                // Category/name views use RootGrid. Pinned and Recommended
                // use a data-template root directly below ContentPresenter.
                if(i && (f.Name()==L"RootGrid" || (parent && f.try_as<sx::Controls::Grid>()))) {
                    if(!root)root=f;Stretch(f,pinned);
                }
                if(type==L"StartMenu.CategoryControl" || type==L"StartDocked.CategoryControl") {
                    category=f;Stretch(f);break;
                }
                if(type==L"StartMenu.PinnedListTile" || type==L"StartMenu.PinnedListCollapsedFolder" || type==L"StartDocked.PinnedListTile" || type==L"StartDocked.PinnedListCollapsedFolder") {Stretch(f,true);break;}
                // CategoryControl itself derives from ItemsControl. Recognize
                // it before guarding unrelated nested item collections.
                if(i && f.try_as<sx::Controls::ItemsControl>())continue;
                for(int c=0;c<sx::Media::VisualTreeHelper::GetChildrenCount(f) && nodes.size()<96;++c)nodes.push_back(sx::Media::VisualTreeHelper::GetChild(f,c));
            }
            if(category) {
                auto grid=FindStartElement(category,L"RootGrid").try_as<sx::Controls::Grid>();
                if(grid){
                    Stretch(grid);
                    for(auto child:grid.Children()) {
                        if(auto b=child.try_as<sx::Controls::Border>();b && std::isfinite(b.Width()) && b.Width()>64){square=b;Stretch(b);content.Set(b,sx::FrameworkElement::HeightProperty(),winrt::box_value(b.Height()));}
                        if(auto scroll=child.try_as<sx::Controls::ScrollViewer>()) {
                            Stretch(scroll,false,false);
                            mini=scroll.Content().try_as<sx::Controls::ItemsWrapGrid>();
                            // ItemsPresenter hosts the real category mini-grid.
                            if(!mini){std::vector<sx::DependencyObject> pending{scroll};for(size_t i=0;i<pending.size() && i<48;++i){if(auto p=pending[i].try_as<sx::Controls::ItemsWrapGrid>()){mini=p;break;}for(int c=0;c<sx::Media::VisualTreeHelper::GetChildrenCount(pending[i]) && pending.size()<48;++c)pending.push_back(sx::Media::VisualTreeHelper::GetChild(pending[i],c));}}
                        }
                        if(auto b=child.try_as<sx::Controls::Button>();b && b.Name()==L"SeeAllButton"){categoryButton=b;Stretch(b);}
                    }
                }
                if(mini){
                    content.Set(mini,sx::Controls::ItemsWrapGrid::ItemWidthProperty(),winrt::box_value(mini.ItemWidth()));
                    content.Set(mini,sx::Controls::ItemsWrapGrid::ItemHeightProperty(),winrt::box_value(mini.ItemHeight()));
                    for(auto child:mini.Children()) {
                        if(auto presenter=child.try_as<sx::Controls::ContentPresenter>()) {
                            auto target=presenter.Content().try_as<sx::FrameworkElement>();
                            if(!target){std::vector<sx::DependencyObject> pending{presenter};for(size_t n=0;n<pending.size() && n<24;++n){if(auto button=pending[n].try_as<sx::Controls::Button>();button && (button.Name()==L"LogoContainer" || button.Name()==L"FolderPlate")){target=button;break;}for(int c=0;c<sx::Media::VisualTreeHelper::GetChildrenCount(pending[n]) && pending.size()<24;++c)pending.push_back(sx::Media::VisualTreeHelper::GetChild(pending[n],c));}}
                            if(target) {
                                miniTargets.push_back(target);Stretch(target,true);
                            }
                        }
                    }
                }
            }
        }
        void Layout(double slot,double gap,StartClassName name) {
            Discover(name);
            if(std::abs(gutter-gap)>.01){gutter=gap;auto m=element.Margin();m.Left=m.Right=gap/2;values.Update(element,sx::FrameworkElement::MarginProperty(),winrt::box_value(m));}
            const double width=std::max(0.0,slot-gap-element.Padding().Left-element.Padding().Right);
            if(std::abs(width-lastWidth)<.01)return;lastWidth=width;
            // A concrete width is required at the React boundary; it is always
            // derived from this viewport's current slot, never a shell constant.
            if(root)content.Update(root,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));
            if(square){content.Update(square,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));content.Update(square,sx::FrameworkElement::HeightProperty(),winrt::box_value(width));}
            if(mini){
                const double inner=std::max(0.0,width-category.as<sx::Controls::Control>().Padding().Left-category.as<sx::Controls::Control>().Padding().Right)/2;
                content.Update(mini,sx::Controls::ItemsWrapGrid::ItemWidthProperty(),winrt::box_value(inner));
                content.Update(mini,sx::Controls::ItemsWrapGrid::ItemHeightProperty(),winrt::box_value(inner));
                for(const auto& target:miniTargets){content.Update(target,sx::FrameworkElement::WidthProperty(),winrt::box_value(inner));content.Update(target,sx::FrameworkElement::HeightProperty(),winrt::box_value(inner));}
            }
        }
    };
    sx::Controls::GridView view_;
    winrt::hstring id_;
    sx::FrameworkElement pane_{nullptr};
    sx::Controls::ItemsWrapGrid panel_{nullptr};
    StartSearchValues viewValues_,panelValues_;
    std::unordered_map<void*,std::unique_ptr<Cell>> cells_;
    winrt::event_token layout_{};
    sx::DispatcherTimer timer_{nullptr};winrt::event_token tick_{};
    struct TickState {StartGrid* owner;};std::shared_ptr<TickState> pending_;
    StartClassName name_;
    bool applied_=false,updating_=false,contentDirty_=false;unsigned passes_=0;
    double panelLeft_=0,panelRight_=0,hostLeft_=0;
    struct Observation {
        void *panel=nullptr,*first=nullptr,*last=nullptr;uint32_t count=0;
        std::array<double,10> geometry{};
        bool operator==(const Observation& o)const{return panel==o.panel && first==o.first && last==o.last && count==o.count && geometry==o.geometry;}
    } observed_;
    bool observedValid_=false;
    Observation Observe() {
        Observation o;auto panel=view_.ItemsPanelRoot().try_as<sx::Controls::ItemsWrapGrid>();o.panel=winrt::get_abi(panel);
        if(!pane_){auto p=sx::Media::VisualTreeHelper::GetParent(view_);for(unsigned i=0;p && i<64;++i){auto f=p.try_as<sx::FrameworkElement>();if(f && f.Name()==L"MainContent"){pane_=f;break;}p=sx::Media::VisualTreeHelper::GetParent(p);}}
        o.geometry={view_.ActualWidth(),view_.ActualHeight(),pane_?pane_.ActualWidth():0,pane_?StartLayoutX(view_,pane_)-view_.Margin().Left:0};
        if(panel){const auto children=panel.Children();o.count=children.Size();if(o.count){o.first=winrt::get_abi(children.GetAt(0));o.last=winrt::get_abi(children.GetAt(o.count-1));}o.geometry[4]=panel.ActualHeight();o.geometry[5]=panel.MaximumRowsOrColumns();
            auto source=winrt::unbox_value<double>(panelValues_.Original(panel,sx::Controls::ItemsWrapGrid::ItemWidthProperty()));o.geometry[6]=std::isfinite(source)?source:0;
            for(const auto& item:cells_)if(item.second->root && !sx::Media::VisualTreeHelper::GetParent(item.second->root))++o.geometry[7];
            o.geometry[8]=children.Size()?children.GetAt(0).as<sx::FrameworkElement>().ActualHeight():0;
            auto cell=cells_.find(o.first);if(cell!=cells_.end())o.geometry[9]=cell->second->NativeWidth();
        }
        return o;
    }
    void Schedule() noexcept {
        try{if(!applied_ || !timer_)return;auto current=Observe();if(observedValid_ && current==observed_)return;observed_=current;observedValid_=true;if(!timer_.IsEnabled())timer_.Start();}catch(...){}
    }
    void Layout() noexcept {
        if(!applied_ || updating_)return;++passes_;updating_=true;
        try {
            Observe();auto panel=view_.ItemsPanelRoot().try_as<sx::Controls::ItemsWrapGrid>();
            if(panel!=panel_){cells_.clear();panelValues_.Clear();panel_=panel;
                if(panel_){panelLeft_=panel_.Margin().Left;panelRight_=panel_.Margin().Right;
                    panelValues_.Set(panel_,sx::FrameworkElement::MarginProperty(),nullptr,[this](const auto& value){auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=panelLeft_;m.Right=panelRight_;return winrt::box_value(m);});}}
            if(!panel_ || !pane_ || pane_.ActualWidth()<=0){updating_=false;return;}
            const bool pinned=id_==L"PinnedList",recommended=id_==L"RecommendedList";
            if(!pinned){
                const double width=pane_.ActualWidth();if(view_.Width()!=width)viewValues_.Update(view_,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));
                const double host=StartLayoutX(view_,pane_)-view_.Margin().Left;hostLeft_=host;
                if(std::abs(view_.Margin().Left+host)>.25){auto m=view_.Margin();m.Left=-host;m.Right=0;viewValues_.Update(view_,sx::FrameworkElement::MarginProperty(),winrt::box_value(m));}
            }
            std::vector<void*> live;double native=0;
            for(const auto& child:panel_.Children())if(auto item=child.try_as<sx::Controls::GridViewItem>()) {
                const auto key=winrt::get_abi(item);live.push_back(key);auto& cell=cells_[key];if(!cell)cell=std::make_unique<Cell>(item,pinned);if(contentDirty_)cell->ResetContent();if(!native)native=cell->NativeWidth();
            }
            for(auto i=cells_.begin();i!=cells_.end();)if(std::find(live.begin(),live.end(),i->first)==live.end())i=cells_.erase(i);else ++i;
            auto source=winrt::unbox_value<double>(panelValues_.Original(panel_,sx::Controls::ItemsWrapGrid::ItemWidthProperty()));if(std::isfinite(source) && source>0)native=source;
            double viewport=view_.ActualWidth()-view_.Padding().Left-view_.Padding().Right;
            auto ancestor=sx::Media::VisualTreeHelper::GetParent(panel_);
            for(unsigned i=0;ancestor && ancestor!=view_ && i<16;++i){if(auto p=ancestor.try_as<sx::Controls::ScrollContentPresenter>()){viewport=p.ActualWidth();break;}ancestor=sx::Media::VisualTreeHelper::GetParent(ancestor);}
            const bool rows=recommended || native>pane_.ActualWidth()/2 || panel_.MaximumRowsOrColumns()==1;
            const double gap=rows || pinned?0:StartMetrics::GridGap;
            const double inset=rows?StartMetrics::RowInset:StartMetrics::SectionInset-gap/2;
            auto margin=panel_.Margin();margin.Left=inset; margin.Right=inset;
            if(pinned)margin.Right=std::max(0.0,inset-(pane_.ActualWidth()-view_.ActualWidth()));
            panelLeft_=margin.Left;panelRight_=margin.Right;
            const auto current=panel_.Margin();if(std::abs(current.Left-margin.Left)>.01 || std::abs(current.Right-margin.Right)>.01)panelValues_.Update(panel_,sx::FrameworkElement::MarginProperty(),winrt::box_value(margin));
            const double width=std::max(0.0,viewport-margin.Left-margin.Right);
            const auto columns=StartColumns(width,native,panel_.MaximumRowsOrColumns());
            if(columns){const double slot=width/columns;
                if(panel_.ItemWidth()!=slot)panelValues_.Update(panel_,sx::Controls::ItemsWrapGrid::ItemWidthProperty(),winrt::box_value(slot));
                if(pinned && panel_.ItemHeight()!=StartMetrics::PinnedHeight+StartMetrics::PinnedRowGap)panelValues_.Update(panel_,sx::Controls::ItemsWrapGrid::ItemHeightProperty(),winrt::box_value(StartMetrics::PinnedHeight+StartMetrics::PinnedRowGap));
                for(auto& item:cells_)item.second->Layout(slot,gap,name_);
            }
        }catch(...){/* Unknown/retemplating trees retry after their next layout. */}
        contentDirty_=false;updating_=false;
    }
public:
    explicit StartGrid(const sx::Controls::GridView& view,StartClassName name=nullptr):view_(view),id_(view.Name()),name_(name){}
    ~StartGrid(){Restore();}
    StartGrid(const StartGrid&)=delete;StartGrid& operator=(const StartGrid&)=delete;
    void Refresh(bool enabled){
        if(!enabled){Restore();return;}
        if(!applied_){applied_=true;try{
            Observe();const double width=id_!=L"PinnedList" && pane_ && pane_.ActualWidth()>0?pane_.ActualWidth():std::numeric_limits<double>::quiet_NaN();
            viewValues_.Set(view_,sx::FrameworkElement::WidthProperty(),winrt::box_value(width));
            viewValues_.Set(view_,sx::FrameworkElement::MinWidthProperty(),winrt::box_value(0.0));
            viewValues_.Set(view_,sx::FrameworkElement::MaxWidthProperty(),winrt::box_value(std::numeric_limits<double>::infinity()));
            viewValues_.Set(view_,sx::FrameworkElement::HorizontalAlignmentProperty(),winrt::box_value(id_==L"PinnedList"?sx::HorizontalAlignment::Stretch:sx::HorizontalAlignment::Left));
            if(id_!=L"PinnedList")viewValues_.Set(view_,sx::FrameworkElement::MarginProperty(),nullptr,[this](const auto& value){auto m=winrt::unbox_value<sx::Thickness>(value);m.Left=-hostLeft_;m.Right=0;return winrt::box_value(m);});
            timer_=sx::DispatcherTimer();timer_.Interval(std::chrono::milliseconds(1));pending_=std::make_shared<TickState>(TickState{this});
            tick_=timer_.Tick([weak=std::weak_ptr<TickState>(pending_)](auto&&,auto&&){if(auto live=weak.lock();live && live->owner){auto self=live->owner;self->timer_.Stop();self->Layout();}});
            layout_=view_.LayoutUpdated([this](auto&&,auto&&){Schedule();});
        }catch(...){Restore();throw;}}
        Schedule();
    }
    void Restore()noexcept{
        applied_=false;if(pending_)pending_->owner=nullptr;pending_.reset();
        try{if(timer_){timer_.Stop();if(tick_.value)timer_.Tick(tick_);}}catch(...){}tick_={};timer_=nullptr;
        try{if(layout_.value)view_.LayoutUpdated(layout_);}catch(...){}layout_={};
        cells_.clear();panelValues_.Clear();panel_=nullptr;pane_=nullptr;viewValues_.Clear();observedValid_=false;contentDirty_=false;hostLeft_=panelLeft_=panelRight_=0;
    }
    bool Applied()const noexcept{return applied_;}
    unsigned LayoutPasses()const noexcept{return passes_;}
    std::wstring_view Name()const noexcept{return id_;}
    sx::Controls::GridView Element()const{return view_;}
    void Invalidate(){contentDirty_=true;observedValid_=false;Schedule();}
};
}
