// SPDX-License-Identifier: GPL-3.0-or-later
// The floating dock follows the approach of AmberWat's DockLike theme for
// Ramen Software's Windows 11 Taskbar Styler: a transparent bar with one
// rounded background behind the app group. Element names and the stock
// button geometry were read from the live taskbar tree. See
// THIRD_PARTY_NOTICES.md. No styling engine or Windhawk code is included.
#include "dock.hpp"
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <limits>
#include <optional>

namespace acrylic {
namespace xaml = winrt::Windows::UI::Xaml;
namespace media = xaml::Media;
namespace shapes = xaml::Shapes;
namespace controls = xaml::Controls;
namespace foundation = winrt::Windows::Foundation;
namespace {
constexpr unsigned MaxVisited = 256;
constexpr int MaxChildren = 256;
winrt::hstring ClassName(const xaml::DependencyObject& object) { return winrt::get_class_name(object); }

template <typename T> std::optional<int32_t> EnumValue(const foundation::IInspectable& value) {
    if (const auto typed = value.try_as<foundation::IReference<T>>()) return static_cast<int32_t>(typed.Value());
    if (const auto number = value.try_as<foundation::IReference<int32_t>>()) return number.Value();
    return std::nullopt;
}
// Boxed values compare by identity, and local values come back in new boxes.
bool Same(const foundation::IInspectable& a, const foundation::IInspectable& b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (const auto x = a.try_as<foundation::IReference<double>>()) {
        const auto y = b.try_as<foundation::IReference<double>>();
        return y && (x.Value() == y.Value() || (std::isnan(x.Value()) && std::isnan(y.Value())));
    }
    if (const auto x = a.try_as<foundation::IReference<xaml::Thickness>>()) {
        const auto y = b.try_as<foundation::IReference<xaml::Thickness>>();
        if (!y) return false;
        const auto p = x.Value(), q = y.Value();
        return p.Left == q.Left && p.Top == q.Top && p.Right == q.Right && p.Bottom == q.Bottom;
    }
    if (const auto x = a.try_as<foundation::IReference<xaml::CornerRadius>>()) {
        const auto y = b.try_as<foundation::IReference<xaml::CornerRadius>>();
        if (!y) return false;
        const auto p = x.Value(), q = y.Value();
        return p.TopLeft == q.TopLeft && p.TopRight == q.TopRight && p.BottomRight == q.BottomRight && p.BottomLeft == q.BottomLeft;
    }
    if (const auto x = EnumValue<xaml::VerticalAlignment>(a)) return x == EnumValue<xaml::VerticalAlignment>(b);
    if (const auto x = EnumValue<xaml::HorizontalAlignment>(a)) return x == EnumValue<xaml::HorizontalAlignment>(b);
    return false;
}
// Each overridden property keeps its stock local value or binding. A value
// the shell writes while the override is active becomes the new stock value.
class Overrides {
    struct Item {
        xaml::FrameworkElement element{nullptr};
        xaml::DependencyProperty key{nullptr};
        foundation::IInspectable local{nullptr}, value{nullptr};
        xaml::Data::BindingBase binding{nullptr};
        int64_t token = 0;
    };
    std::vector<std::unique_ptr<Item>> items_;
    bool writing_ = false;
    static void Save(Item& item) {
        item.local = item.element.ReadLocalValue(item.key);
        const auto expression = item.element.GetBindingExpression(item.key);
        item.binding = expression ? expression.ParentBinding() : nullptr;
    }
    void Write(Item& item) {
        writing_ = true;
        try { item.element.SetValue(item.key, item.value); }
        catch (...) { writing_ = false; throw; }
        writing_ = false;
    }
    void Changed(Item& item) noexcept {
        if (writing_) return;
        try {
            // An animation outranking our local value leaves it in place, and
            // the saved stock value stays correct.
            if (Same(item.element.ReadLocalValue(item.key), item.value)) return;
            Save(item);
            Write(item);
        } catch (...) { writing_ = false; }
    }
public:
    Overrides() = default;
    Overrides(const Overrides&) = delete;
    Overrides& operator=(const Overrides&) = delete;
    ~Overrides() { Clear(); }
    void Set(const xaml::FrameworkElement& element, const xaml::DependencyProperty& key, const foundation::IInspectable& value) {
        for (const auto& item : items_) {
            if (item->element != element || item->key != key) continue;
            if (Same(item->value, value) && Same(element.ReadLocalValue(key), value)) return;
            item->value = value;
            Write(*item);
            return;
        }
        auto item = std::make_unique<Item>();
        item->element = element; item->key = key; item->value = value;
        Save(*item);
        Item* raw = item.get();
        item->token = element.RegisterPropertyChangedCallback(key, [this, raw](auto&&, auto&&) { Changed(*raw); });
        items_.push_back(std::move(item));
        Write(*raw);
    }
    // Leaves the property alone when it already has the value.
    void Ensure(const xaml::FrameworkElement& element, const xaml::DependencyProperty& key, const foundation::IInspectable& value) {
        if (Same(element.GetValue(key), value) && !Contains(element, key)) return;
        Set(element, key, value);
    }
    bool Contains(const xaml::FrameworkElement& element, const xaml::DependencyProperty& key) const {
        for (const auto& item : items_) if (item->element == element && item->key == key) return true;
        return false;
    }
    bool Empty() const noexcept { return items_.empty(); }
    void Clear() noexcept {
        writing_ = true;
        for (const auto& item : items_) {
            try { if (item->token) item->element.UnregisterPropertyChangedCallback(item->key, item->token); } catch (...) {}
            try {
                item->element.ClearValue(item->key);
                if (item->binding) item->element.SetBinding(item->key, item->binding);
                else if (item->local != xaml::DependencyProperty::UnsetValue()) item->element.SetValue(item->key, item->local);
            } catch (...) {}
        }
        items_.clear();
        writing_ = false;
    }
};
DockBox Bounds(const xaml::FrameworkElement& element, const xaml::UIElement& relativeTo) {
    const auto rect = element.TransformToVisual(relativeTo).TransformBounds(
        {0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
    return {rect.X, rect.Y, static_cast<double>(rect.X) + rect.Width, static_cast<double>(rect.Y) + rect.Height};
}
bool Shown(const xaml::FrameworkElement& element) {
    return element && element.Visibility() == xaml::Visibility::Visible && element.ActualWidth() > 0 && element.ActualHeight() > 0;
}
bool SameRects(const std::vector<RECT>& a, const std::vector<RECT>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const RECT& x, const RECT& y) { return EqualRect(&x, &y) != FALSE; });
}
HRGN BuildRegion(const std::vector<RECT>& rects) {
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    for (const auto& rect : rects) {
        if (!region) return nullptr;
        HRGN part = CreateRectRgnIndirect(&rect);
        if (!part) { DeleteObject(region); return nullptr; }
        CombineRgn(region, region, part, RGN_OR);
        DeleteObject(part);
    }
    return region;
}
}

struct DockView::Impl {
    xaml::FrameworkElement frame;
    DockClassName className;
    shapes::Rectangle fill{nullptr}, stroke{nullptr};
    xaml::FrameworkElement background{nullptr}, repeater{nullptr};
    Overrides overrides, heights, corners;
    std::vector<DockBox> hits;
    DockBox pill;
    media::Brush strokeBrush{nullptr};
    std::function<void()> moved;
    winrt::event_token layout{};
    LONG barHeight = 0;
    bool applied = false, listening = false, updating = false;
    HRESULT error = S_OK;

    Impl(const xaml::FrameworkElement& value, DockClassName name) : frame(value), className(name ? name : ClassName) {
        if (!frame) winrt::throw_hresult(E_INVALIDARG);
    }
    ~Impl() { Restore(); }
    // The two background rectangles share a grid; the repeater holds the
    // buttons. Nothing below the repeater is searched.
    void Find() {
        shapes::Rectangle nextFill{nullptr}, nextStroke{nullptr};
        xaml::FrameworkElement nextRepeater{nullptr};
        std::vector<xaml::DependencyObject> pending{frame};
        for (unsigned visited = 0; !pending.empty() && visited < MaxVisited; ++visited) {
            const auto item = pending.back(); pending.pop_back();
            if (const auto element = item.try_as<xaml::FrameworkElement>()) {
                const auto name = element.Name();
                if (name == L"TaskbarFrameRepeater") { if (!nextRepeater) nextRepeater = element; continue; }
                if (name == L"BackgroundFill") { if (!nextFill) nextFill = item.try_as<shapes::Rectangle>(); continue; }
                if (name == L"BackgroundStroke") { if (!nextStroke) nextStroke = item.try_as<shapes::Rectangle>(); continue; }
            }
            const int count = media::VisualTreeHelper::GetChildrenCount(item);
            if (count > MaxChildren) continue;
            for (int i = count - 1; i >= 0; --i) pending.push_back(media::VisualTreeHelper::GetChild(item, i));
        }
        xaml::FrameworkElement nextBackground{nullptr};
        if (nextFill) nextBackground = media::VisualTreeHelper::GetParent(nextFill).try_as<xaml::FrameworkElement>();
        if (nextStroke && media::VisualTreeHelper::GetParent(nextStroke) != nextBackground) nextStroke = nullptr;
        if (nextFill == fill && nextStroke == stroke && nextRepeater == repeater && nextBackground == background) return;
        // A replaced template gets back its stock values before the new one is used.
        Release();
        fill = nextFill; stroke = nextStroke; repeater = nextRepeater; background = nextBackground;
    }
    // The app group in the background's coordinates. Parts that stay out of
    // the pill (widgets on the far side, a tray hosted in the repeater) are
    // reported in root coordinates instead.
    DockBox Group(std::vector<DockBox>& outside) {
        const double width = background.ActualWidth(), height = background.ActualHeight();
        DockBox group;
        bool any = false;
        const int count = media::VisualTreeHelper::GetChildrenCount(repeater);
        for (int i = 0; i < count && i < MaxChildren; ++i) {
            const auto child = media::VisualTreeHelper::GetChild(repeater, i).try_as<xaml::FrameworkElement>();
            if (!Shown(child)) continue;
            const auto box = Bounds(child, background);
            // The repeater parks recycled buttons far outside the bar.
            if (box.right <= 0 || box.left >= width || box.bottom <= 0 || box.top >= height) continue;
            const auto type = className(child);
            if (type == L"Taskbar.AugmentedEntryPointButton" || type == L"Taskbar.TaskbarExtensionElement") {
                outside.push_back(Bounds(child, nullptr));
                continue;
            }
            if (!any) { group = box; any = true; continue; }
            group = {(std::min)(group.left, box.left), (std::min)(group.top, box.top), (std::max)(group.right, box.right), (std::max)(group.bottom, box.bottom)};
        }
        return any ? group : DockBox{};
    }
    std::vector<DockBox> Collect(const DockBox& next, std::vector<DockBox> outside) {
        const auto root = Bounds(background, nullptr);
        std::vector<DockBox> result{{root.left + next.left, root.top + next.top, root.left + next.right, root.top + next.bottom}};
        result.insert(result.end(), outside.begin(), outside.end());
        // Other parts of the bar beside the background and the repeater. The
        // tablet gripper takes no XAML input but marks where the shell's own
        // gesture lives, so it stays in.
        const auto bar = media::VisualTreeHelper::GetParent(repeater).try_as<xaml::FrameworkElement>();
        xaml::DependencyObject backgroundControl = background;
        while (backgroundControl && media::VisualTreeHelper::GetParent(backgroundControl) != bar)
            backgroundControl = media::VisualTreeHelper::GetParent(backgroundControl);
        auto add = [&](const xaml::DependencyObject& parent, const xaml::DependencyObject& skipA, const xaml::DependencyObject& skipB) {
            const int count = parent ? media::VisualTreeHelper::GetChildrenCount(parent) : 0;
            for (int i = 0; i < count && i < MaxChildren; ++i) {
                const auto item = media::VisualTreeHelper::GetChild(parent, i);
                if (item == skipA || item == skipB) continue;
                const auto element = item.try_as<xaml::FrameworkElement>();
                if (!Shown(element) || !(element.IsHitTestVisible() || element.Name() == L"GripperControl")) continue;
                result.push_back(Bounds(element, nullptr));
            }
        };
        add(bar, backgroundControl, repeater);
        // Parts beside the frame in the same island, such as the notification area.
        add(media::VisualTreeHelper::GetParent(frame), frame, nullptr);
        return result;
    }
    void Shape(const shapes::Rectangle& shape, const xaml::Thickness& margin, double corner) {
        overrides.Set(shape, xaml::FrameworkElement::MarginProperty(), winrt::box_value(margin));
        overrides.Set(shape, shapes::Rectangle::RadiusXProperty(), winrt::box_value(corner));
        overrides.Set(shape, shapes::Rectangle::RadiusYProperty(), winrt::box_value(corner));
        overrides.Ensure(shape, xaml::FrameworkElement::HorizontalAlignmentProperty(), winrt::box_value(xaml::HorizontalAlignment::Stretch));
        overrides.Ensure(shape, xaml::FrameworkElement::VerticalAlignmentProperty(), winrt::box_value(xaml::VerticalAlignment::Stretch));
        const auto automatic = winrt::box_value(std::numeric_limits<double>::quiet_NaN());
        overrides.Ensure(shape, xaml::FrameworkElement::WidthProperty(), automatic);
        overrides.Ensure(shape, xaml::FrameworkElement::HeightProperty(), automatic);
    }
    // Returns whether the hits changed.
    bool Update(const media::Brush& brush) {
        std::vector<DockBox> outside;
        const auto group = Group(outside);
        const double width = background.ActualWidth(), height = background.ActualHeight();
        const auto next = DockPill(group, width, height);
        if (next.Empty()) {
            const bool had = !hits.empty();
            Release();
            return had;
        }
        if (!applied || next != pill || brush != strokeBrush) {
            const xaml::Thickness margin{next.left, next.top, width - next.right, height - next.bottom};
            const double corner = DockCorner(next);
            Shape(fill, margin, corner);
            if (stroke) {
                Shape(stroke, margin, corner);
                if (brush) overrides.Set(stroke, shapes::Shape::StrokeProperty(), brush);
            }
            pill = next; strokeBrush = brush; applied = true;
        }
        auto next_hits = Collect(next, std::move(outside));
        if (next_hits == hits) return false;
        hits = std::move(next_hits);
        return true;
    }
    void Listen() {
        if (listening) return;
        layout = frame.LayoutUpdated([this](auto&&, auto&&) { OnLayout(); });
        listening = true;
    }
    void Unlisten() noexcept {
        if (!listening) return;
        try { frame.LayoutUpdated(layout); } catch (...) {}
        listening = false;
    }
    xaml::FrameworkElement TrayFrame() {
        const auto parent = media::VisualTreeHelper::GetParent(frame);
        const int count = parent ? media::VisualTreeHelper::GetChildrenCount(parent) : 0;
        for (int i = 0; i < count && i < MaxChildren; ++i) {
            const auto child = media::VisualTreeHelper::GetChild(parent, i);
            if (className(child) == L"SystemTray.SystemTrayFrame") return child.try_as<xaml::FrameworkElement>();
        }
        return nullptr;
    }
    // Held while the dock is on: each button's hover and running highlight
    // gets an app icon's rounded corners. With the taller bar, the frame and
    // the notification area beside it hold its height, and each highlight
    // stays a square in the middle of its button.
    void Hold() {
        if (barHeight <= 0) heights.Clear();
        else {
            // Whole pixels at the window's DPI, as Explorer sizes the window, so
            // the frame neither leaves a gap nor gets cut off at uneven scales.
            const auto root = frame.XamlRoot();
            const double scale = root ? root.RasterizationScale() : 1.0;
            const auto tall = winrt::box_value(MulDiv(barHeight, static_cast<int>(std::lround(scale * 96)), 96) / scale);
            heights.Set(frame, xaml::FrameworkElement::HeightProperty(), tall);
            if (const auto tray = TrayFrame()) heights.Set(tray, xaml::FrameworkElement::HeightProperty(), tall);
        }
        const int count = media::VisualTreeHelper::GetChildrenCount(repeater);
        for (int i = 0; i < count && i < MaxChildren; ++i) {
            const auto button = media::VisualTreeHelper::GetChild(repeater, i);
            if (media::VisualTreeHelper::GetChildrenCount(button) < 1) continue;
            const auto panel = media::VisualTreeHelper::GetChild(button, 0).try_as<controls::Grid>();
            if (!Shown(panel) || (panel.Name() != L"IconPanel" && panel.Name() != L"ExperienceToggleButtonRootPanel")) continue;
            if (barHeight > 0) {
                const auto padding = panel.Padding();
                const double side = panel.ActualWidth() - padding.Left - padding.Right;
                const double inset = (panel.ActualHeight() - side) / 2;
                if (side > 0 && inset >= 0)
                    heights.Set(panel, controls::Grid::PaddingProperty(), winrt::box_value(xaml::Thickness{padding.Left, inset, padding.Right, inset}));
            }
            Corners(panel);
        }
    }
    // The highlight and its overlay are borders up to two levels below the panel.
    void Corners(const xaml::DependencyObject& panel) {
        std::vector<std::pair<xaml::DependencyObject, int>> pending{{panel, 0}};
        while (!pending.empty()) {
            const auto [item, depth] = pending.back();
            pending.pop_back();
            if (const auto border = item.try_as<controls::Border>()) {
                const double side = (std::min)(border.ActualWidth(), border.ActualHeight());
                const double radius = std::round(side * DockHighlightCorner);
                if (side > 0) corners.Set(border, controls::Border::CornerRadiusProperty(), winrt::box_value(xaml::CornerRadius{radius, radius, radius, radius}));
            }
            if (depth == 2) continue;
            const int count = media::VisualTreeHelper::GetChildrenCount(item);
            for (int i = 0; i < count && i < MaxChildren; ++i) pending.push_back({media::VisualTreeHelper::GetChild(item, i), depth + 1});
        }
    }
    // Buttons come and go with a layout pass; follow them in the same pass.
    void OnLayout() noexcept {
        if (updating || !listening || !fill || !repeater || !background) return;
        updating = true;
        bool changed = false;
        try { changed = Update(strokeBrush); Hold(); }
        catch (...) {
            error = winrt::to_hresult();
            Release();
            changed = true;
        }
        updating = false;
        if (changed && moved) { try { moved(); } catch (...) {} }
    }
    // Stock geometry, still following the layout.
    void Release() noexcept {
        overrides.Clear();
        applied = false; pill = {}; strokeBrush = nullptr;
        hits.clear();
    }
    void Refresh(bool enabled, const media::Brush& brush, LONG dips) {
        error = S_OK;
        if (!enabled) { Restore(); return; }
        updating = true;
        try {
            Find();
            barHeight = dips;
            if (!fill || !repeater || !background) { Release(); heights.Clear(); corners.Clear(); }
            else { Listen(); Update(brush); Hold(); }
        } catch (...) {
            error = winrt::to_hresult();
            Restore();
        }
        updating = false;
    }
    void Restore() noexcept {
        Unlisten();
        Release();
        heights.Clear();
        corners.Clear();
        barHeight = 0;
    }
};
DockView::DockView(const xaml::FrameworkElement& frame, DockClassName className) : impl(std::make_unique<Impl>(frame, className)) {}
DockView::~DockView() = default;
void DockView::Refresh(bool enabled, const media::Brush& stroke, LONG height) { impl->Refresh(enabled, stroke, height); }
void DockView::Restore() noexcept { impl->Restore(); }
bool DockView::Applied() const noexcept { return impl->applied; }
LONG DockView::Height() const noexcept { return impl->heights.Empty() ? 0 : impl->barHeight; }
const std::vector<DockBox>& DockView::Hits() const noexcept { return impl->hits; }
void DockView::OnMoved(std::function<void()> callback) { impl->moved = std::move(callback); }
HRESULT DockView::Error() const noexcept { return impl->error; }

namespace {
struct Island { HWND window; POINT offset; RECT bounds; bool mirrored; };
struct Search { SIZE size; std::vector<Island> found; };
BOOL CALLBACK Candidate(HWND window, LPARAM context) {
    auto& search = *reinterpret_cast<Search*>(context);
    wchar_t name[64]{};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    if (wcscmp(name, L"Shell_TrayWnd") && wcscmp(name, L"Shell_SecondaryTrayWnd")) return TRUE;
    RECT frame{};
    if (!GetWindowRect(window, &frame)) return TRUE;
    for (HWND child = nullptr; (child = FindWindowExW(window, child, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)) != nullptr;) {
        RECT bounds{};
        if (!GetWindowRect(child, &bounds)) continue;
        if (std::labs((bounds.right - bounds.left) - search.size.cx) > 2 || std::labs((bounds.bottom - bounds.top) - search.size.cy) > 2) continue;
        try {
            search.found.push_back({window, {bounds.left - frame.left, bounds.top - frame.top}, bounds,
                (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0});
        } catch (...) { return FALSE; }
    }
    return TRUE;
}
}
HWND FindDockWindow(SIZE island, const POINT* screen, DWORD thread, POINT& offset, HRESULT& error) noexcept {
    Search search{island, {}};
    EnumThreadWindows(thread, Candidate, reinterpret_cast<LPARAM>(&search));
    auto candidates = search.found.size();
    const Island* match = candidates == 1 ? &search.found[0] : nullptr;
    if (candidates > 1 && screen) {
        candidates = 0;
        for (const auto& item : search.found) if (PtInRect(&item.bounds, *screen)) { match = &item; ++candidates; }
        if (candidates != 1) match = nullptr;
    }
    if (!match) {
        error = search.found.empty() ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : TYPE_E_AMBIGUOUSNAME;
        return nullptr;
    }
    // A mirrored window mirrors its region too. Leave right-to-left taskbars stock.
    if (match->mirrored) { error = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return nullptr; }
    offset = match->offset;
    error = S_OK;
    return match->window;
}
HWND FindTaskbarWindow(const xaml::FrameworkElement& element, POINT& offset, double& scale, HRESULT& error) noexcept {
    try {
        const auto root = element.XamlRoot();
        if (!root) { error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND); return nullptr; }
        scale = root.RasterizationScale();
        const auto size = root.Size();
        const SIZE island{std::lround(size.Width * scale), std::lround(size.Height * scale)};
        HWND window = FindDockWindow(island, nullptr, GetCurrentThreadId(), offset, error);
        if (!window && error == TYPE_E_AMBIGUOUSNAME) {
            // Identical monitors: the element's automation peer knows where on screen it is.
            if (const auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)) {
                const auto bounds = peer.GetBoundingRectangle();
                const POINT center{std::lround(bounds.X + bounds.Width / 2), std::lround(bounds.Y + bounds.Height / 2)};
                window = FindDockWindow(island, &center, GetCurrentThreadId(), offset, error);
            }
        }
        return window;
    } catch (...) {
        error = winrt::to_hresult();
        return nullptr;
    }
}
HWND TaskbarBounds(const xaml::FrameworkElement& element, RECT& bounds, double& scale, HRESULT& error) noexcept {
    POINT offset{};
    const HWND window = FindTaskbarWindow(element, offset, scale, error);
    if (!window) return nullptr;
    try {
        const auto box = element.TransformToVisual(nullptr).TransformBounds(
            {0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
        bounds = {offset.x + std::lround(box.X * scale), offset.y + std::lround(box.Y * scale),
            offset.x + std::lround((box.X + box.Width) * scale), offset.y + std::lround((box.Y + box.Height) * scale)};
        return window;
    } catch (...) {
        error = winrt::to_hresult();
        return nullptr;
    }
}

bool DockRegion::Ours() const noexcept {
    HRGN current = CreateRectRgn(0, 0, 0, 0), mine = BuildRegion(rects_);
    const bool same = current && mine && GetWindowRgn(window_, current) != ERROR && EqualRgn(current, mine);
    if (current) DeleteObject(current);
    if (mine) DeleteObject(mine);
    return same;
}
DockRegion::Stock DockRegion::Current(HWND window) noexcept {
    HRGN current = CreateRectRgn(0, 0, 0, 0);
    if (!current) return Stock::Other;
    Stock stock = Stock::Other;
    RECT bounds{};
    if (GetWindowRgn(window, current) == ERROR) stock = Stock::None;
    else if (GetWindowRect(window, &bounds)) {
        HRGN whole = CreateRectRgn(0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        if (whole && EqualRgn(current, whole)) stock = Stock::Whole;
        if (whole) DeleteObject(whole);
    }
    DeleteObject(current);
    return stock;
}
HRESULT DockRegion::Apply(HWND window, const std::vector<RECT>& rects) noexcept {
    if (!window || rects.empty()) { Restore(); return S_OK; }
    if (window != window_) {
        Restore();
        const auto stock = Current(window);
        // Another program shapes this taskbar: leave it alone.
        if (stock == Stock::Other) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        whole_ = stock == Stock::Whole;
    } else if (Ours()) {
        if (SameRects(rects, rects_)) return S_OK;
    } else {
        // The shell wrote its stock region again: keep it as the stock state
        // and set ours again below. Anything else is another program's.
        const auto stock = Current(window);
        if (stock == Stock::Other) { window_ = nullptr; rects_.clear(); return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); }
        whole_ = stock == Stock::Whole;
    }
    HRGN region = BuildRegion(rects);
    if (!region) return E_OUTOFMEMORY;
    // The system owns the region once SetWindowRgn succeeds.
    if (!SetWindowRgn(window, region, TRUE)) {
        const DWORD failure = GetLastError();
        DeleteObject(region);
        return HRESULT_FROM_WIN32(failure ? failure : ERROR_INVALID_WINDOW_HANDLE);
    }
    window_ = window;
    rects_ = rects;
    return S_OK;
}
void DockRegion::Restore() noexcept {
    if (!window_) return;
    RECT bounds{};
    if (IsWindow(window_) && Ours()) {
        // The whole window at its current size, as the shell sets it.
        HRGN stock = whole_ && GetWindowRect(window_, &bounds) ? CreateRectRgn(0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top) : nullptr;
        if (!SetWindowRgn(window_, stock, TRUE) && stock) DeleteObject(stock);
    }
    window_ = nullptr;
    rects_.clear();
    whole_ = false;
}
}
