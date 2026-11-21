// SPDX-License-Identifier: GPL-3.0-or-later
// Hosts a stand-in for the touch keyboard's key grid in a XAML island and
// checks the adapter: the compact arrangement with hidden keys, gaps and
// corners, re-arrangement after the keyboard rewrites its own layout, unknown
// pages, the oscillation guard and restoration of every changed property.
#include "../src/keyboard_view.hpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace acrylic;
namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
void Pump() { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
void Layout(const sx::FrameworkElement& root) { for (unsigned i = 0; i < 8; ++i) { root.UpdateLayout(); Pump(); Sleep(10); } }
bool Near(double a, double b, double tolerance = .01) { return std::abs(a - b) <= tolerance; }

struct Stock { const wchar_t* label; const wchar_t* secondary; double left, top, width, height; };
// Windows 11 25H2 portrait keyboard, measured live (labels as the keyboard shows them).
std::vector<Stock> Alphabet() {
    std::vector<Stock> keys;
    const wchar_t* row1[] = {L"q", L"w", L"e", L"r", L"t", L"y", L"u", L"i", L"o", L"p"};
    const double left1[] = {0, 52, 103, 154, 205, 257, 308, 359, 410, 462}, width1[] = {52, 51, 51, 51, 52, 51, 51, 51, 52, 51};
    const wchar_t* digits[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};
    for (int i = 0; i < 10; ++i) keys.push_back({row1[i], digits[i], left1[i], 0, width1[i], 62});
    keys.push_back({L"\uE750", L"", 513, 0, 102, 62});
    const wchar_t* row2[] = {L"a", L"s", L"d", L"f", L"g", L"h", L"j", L"k", L"l", L"'"};
    const double left2[] = {26, 77, 128, 180, 231, 282, 334, 385, 436, 488}, width2[] = {51, 51, 52, 51, 51, 52, 51, 51, 52, 51};
    for (int i = 0; i < 10; ++i) keys.push_back({row2[i], L"", left2[i], 62, width2[i], 61});
    keys.push_back({L"\uE751", L"", 539, 62, 76, 61});
    keys.push_back({L"\uE3D7", L"", 0, 123, 52, 62});
    const wchar_t* row3[] = {L"z", L"x", L"c", L"v", L"b", L"n", L"m", L",", L".", L"?"};
    const double left3[] = {52, 103, 154, 205, 257, 308, 359, 410, 462, 513}, width3[] = {51, 51, 51, 52, 51, 51, 51, 52, 51, 51};
    for (int i = 0; i < 10; ++i) keys.push_back({row3[i], L"", left3[i], 123, width3[i], 62});
    keys.push_back({L"\uE3D7", L"", 564, 123, 51, 62});
    keys.push_back({L"&123", L"", 0, 185, 52, 61});
    keys.push_back({L"Ctrl", L"", 52, 185, 51, 61});
    keys.push_back({L"\uE720", L"", 103, 185, 51, 61});
    keys.push_back({L" ", L"", 154, 185, 308, 61});
    keys.push_back({L"\uE76B", L"", 462, 185, 51, 61});
    keys.push_back({L"\uE76C", L"", 513, 185, 51, 61});
    keys.push_back({L"", L"ENG", 564, 185, 51, 61});
    return keys;
}
// Indexes into Alphabet().
constexpr size_t Q = 0, Backspace = 10, A = 11, Apostrophe = 20, Enter = 21, Z = 23, RightShift = 33, Symbols = 34, Ctrl = 35,
    Space = 37, LeftArrow = 38, RightArrow = 39, Language = 40;

// A key as the keyboard builds it: positioned by Margin, sized explicitly,
// a visual-bounds grid (1-DIP inset) holding the background and labels.
sx::FrameworkElement MakeKey(const Stock& stock, bool parts = true) {
    sx::Controls::Grid key;
    key.HorizontalAlignment(sx::HorizontalAlignment::Left);
    key.VerticalAlignment(sx::VerticalAlignment::Top);
    key.Margin({stock.left, stock.top, 0, 0});
    key.Width(stock.width);
    key.Height(stock.height);
    if (parts) {
        sx::Controls::Grid bounds; bounds.Name(L"TEMPLATE_PART_KeyVisualBounds"); bounds.Margin({1, 1, 1, 1});
        sx::Controls::Border background; background.Name(L"TEMPLATE_PART_KeyBackgroundBorder"); background.CornerRadius({5, 5, 5, 5});
        sx::Controls::TextBlock label; label.Name(L"TEMPLATE_PART_KeyLabel"); label.Text(stock.label);
        if (stock.label[0] >= 0xE000 && stock.label[0] <= 0xF8FF) label.FontSize(16.1);  // icon keys, as measured live
        sx::Controls::TextBlock secondary; secondary.Name(L"TEMPLATE_PART_KeySecondaryLabel"); secondary.Text(stock.secondary);
        secondary.FontSize(12); secondary.Foreground(sx::Media::SolidColorBrush(winrt::Windows::UI::Color{0xC5, 255, 255, 255}));
        bounds.Children().Append(background); bounds.Children().Append(label); bounds.Children().Append(secondary);
        key.Children().Append(bounds);
    }
    return key;
}
void Fill(const sx::Controls::Grid& grid, const std::vector<Stock>& keys) {
    grid.Children().Clear();
    for (const auto& stock : keys) grid.Children().Append(MakeKey(stock));
}
sx::FrameworkElement Key(const sx::Controls::Grid& grid, size_t index) { return grid.Children().GetAt(static_cast<uint32_t>(index)).as<sx::FrameworkElement>(); }
sx::FrameworkElement Bounds(const sx::FrameworkElement& key) { return FindStartElement(key, L"TEMPLATE_PART_KeyVisualBounds"); }
sx::Controls::Border Background(const sx::FrameworkElement& key) { return FindStartElement(key, L"TEMPLATE_PART_KeyBackgroundBorder").as<sx::Controls::Border>(); }
bool Shown(const sx::FrameworkElement& key) { return key.Visibility() == sx::Visibility::Visible; }
// The hint text (digits over q..p, ENG on the language key): size and ink alpha.
bool HintIs(const sx::FrameworkElement& key, double size, uint8_t alpha) {
    const auto element = FindStartElement(key, L"TEMPLATE_PART_KeySecondaryLabel");
    const auto hint = element ? element.try_as<sx::Controls::TextBlock>() : nullptr;
    return hint && Near(hint.FontSize(), size) && hint.Foreground().as<sx::Media::SolidColorBrush>().Color().A == alpha;
}
// The language key's name (ENG) becomes a globe the size of the other icon keys.
sx::Controls::TextBlock LanguageName(const sx::Controls::Grid& grid, size_t index) {
    return FindStartElement(Key(grid, index), L"TEMPLATE_PART_KeySecondaryLabel").as<sx::Controls::TextBlock>();
}
bool IsGlobe(const sx::Controls::Grid& grid, size_t index) {
    const auto name = LanguageName(grid, index);
    return name.Text() == L"" && std::wstring_view(name.FontFamily().Source()).substr(0, 18) == L"Segoe Fluent Icons"
        && Near(name.FontSize(), 16.1) && name.Foreground().as<sx::Media::SolidColorBrush>().Color().A == 0xC5;
}
bool IsStock(const sx::Controls::Grid& grid, const std::vector<Stock>& keys) {
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto key = Key(grid, i);
        if (!Near(key.Margin().Left, keys[i].left) || !Near(key.Margin().Top, keys[i].top) || !Near(key.Width(), keys[i].width) || !Near(key.Height(), keys[i].height)
            || !Shown(key)) return false;
        const auto bounds = Bounds(key);
        if (bounds && (!Near(bounds.Margin().Left, 1) || !Near(bounds.Margin().Top, 1) || !Near(Background(key).CornerRadius().TopLeft, 5) || !HintIs(key, 12, 0xC5))) return false;
    }
    return true;
}
// Edges snap to device pixels, so allow one pixel at the island's scale.
struct Frame { double side, p, pixel, above; };
Frame Measure(const sx::Controls::Grid& grid) {
    const double scale = grid.XamlRoot() ? grid.XamlRoot().RasterizationScale() : 1.0;
    const double side = std::round(KeyboardSidePadding * 61 * scale) / scale;
    return {side, (615 - 2 * side) / 10, 1.01 / scale, std::round(KeyboardTopPadding * 61 * scale) / scale};
}
bool IsCompact(const sx::Controls::Grid& grid) {
    const auto [side, p, pixel, above] = Measure(grid);
    const auto q = Key(grid, Q), a = Key(grid, A), z = Key(grid, Z), backspace = Key(grid, Backspace), enter = Key(grid, Enter);
    const auto ctrl = Key(grid, Ctrl), language = Key(grid, Language), symbols = Key(grid, Symbols);
    return Near(q.Margin().Left, 0) && Near(q.Margin().Top, 0) && Near(q.Width(), side + p, 2 * pixel)
        && Near(a.Margin().Left, 0) && Near(Key(grid, A + 1).Margin().Left, side + 1.5 * p, pixel) && Near(z.Margin().Left, side + 1.5 * p, pixel)
        && Near(backspace.Margin().Top, z.Margin().Top) && Near(backspace.Margin().Left + backspace.Width(), 615)
        && Near(enter.Margin().Top, Key(grid, Space).Margin().Top) && Near(enter.Margin().Left + enter.Width(), 615) && Near(enter.Margin().Top + enter.Height(), 246)
        && Near(language.Margin().Left, symbols.Margin().Left + symbols.Width()) && Near(ctrl.Margin().Left, language.Margin().Left + language.Width())
        && Shown(ctrl) && Shown(language) && Shown(Key(grid, Space))
        && !Shown(Key(grid, Apostrophe)) && !Shown(Key(grid, RightShift)) && !Shown(Key(grid, LeftArrow)) && !Shown(Key(grid, RightArrow))
        && Bounds(q).Margin().Left > side && Bounds(q).Margin().Top > above + 2 && Background(q).CornerRadius().TopLeft >= 7
        && Bounds(Key(grid, Space)).Margin().Bottom > Bounds(Key(grid, Space)).Margin().Top + 22
        && HintIs(q, 9.6, 0x8B) && IsGlobe(grid, Language);
}
// The &123 page, with the stock positions from a live screenshot.
std::vector<Stock> Numbers() {
    std::vector<Stock> keys;
    const wchar_t* row1[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};
    const double left1[] = {0, 52, 103, 154, 205, 257, 308, 359, 410, 462}, width1[] = {52, 51, 51, 51, 52, 51, 51, 51, 52, 51};
    for (int i = 0; i < 10; ++i) keys.push_back({row1[i], L"", left1[i], 0, width1[i], 62});
    keys.push_back({L"\uE750", L"", 513, 0, 102, 62});
    const wchar_t* row2[] = {L"!", L"@", L"#", L"$", L"^", L"&", L"_", L"-", L"=", L"+"};
    const double left2[] = {26, 77, 128, 180, 231, 282, 334, 385, 436, 488}, width2[] = {51, 51, 52, 51, 51, 52, 51, 51, 52, 51};
    for (int i = 0; i < 10; ++i) keys.push_back({row2[i], L"", left2[i], 62, width2[i], 61});
    keys.push_back({L"\uE751", L"", 539, 62, 76, 61});
    keys.push_back({L"\uE768", L"", 0, 123, 52, 62});
    const wchar_t* row3[] = {L";", L":", L"(", L")", L"/", L"'", L"\"", L"?"};
    const double left3[] = {52, 103, 154, 205, 257, 308, 359, 410}, width3[] = {51, 51, 51, 52, 51, 51, 51, 52};
    for (int i = 0; i < 8; ++i) keys.push_back({row3[i], L"", left3[i], 123, width3[i], 62});
    keys.push_back({L"Home", L"", 462, 123, 51, 62});
    keys.push_back({L"\uE70E", L"", 513, 123, 51, 62});
    keys.push_back({L"End", L"", 564, 123, 51, 62});
    keys.push_back({L"abc", L"", 0, 185, 52, 61});
    keys.push_back({L"Ctrl", L"", 52, 185, 51, 61});
    keys.push_back({L"Tab", L"", 103, 185, 51, 61});
    keys.push_back({L",", L"", 154, 185, 51, 61});
    keys.push_back({L" ", L"", 205, 185, 205, 61});
    keys.push_back({L".", L"", 410, 185, 52, 61});
    keys.push_back({L"\uE76B", L"", 462, 185, 51, 61});
    keys.push_back({L"\uE70D", L"", 513, 185, 51, 61});
    keys.push_back({L"\uE76C", L"", 564, 185, 51, 61});
    return keys;
}
// Indexes into Numbers(): 1..0, delete, ! @ # $ ^ & _ - = +, return, next, ; : ( ) / ' " ?, Home, up, End, abc, Ctrl, Tab, ",", space, ".", left, down, right.
constexpr size_t NumDelete = 10, NumHash = 13, NumDash = 18, NumEnter = 21, NumNext = 22, NumQuote = 29, NumHome = 31, NumAbc = 34, NumTab = 36, NumComma = 37, NumSpace = 38, NumPeriod = 39;
bool IsNumbers(const sx::Controls::Grid& grid) {
    const auto [side, p, pixel, above] = Measure(grid);
    (void)above;
    const auto dash = Key(grid, NumDash), quote = Key(grid, NumQuote), period = Key(grid, NumPeriod), comma = Key(grid, NumComma);
    const auto next = Key(grid, NumNext), erase = Key(grid, NumDelete), enter = Key(grid, NumEnter), abc = Key(grid, NumAbc);
    return Near(dash.Margin().Left, 0) && Near(dash.Margin().Top, Key(grid, 23).Margin().Top) && Near(quote.Margin().Left, side + 9 * p, pixel)
        && Near(period.Margin().Left, side + 1.5 * p, pixel) && Near(period.Margin().Top, next.Margin().Top) && Near(comma.Margin().Left, side + 2.9 * p, pixel)
        && Near(next.Margin().Left, 0) && Near(erase.Margin().Top, next.Margin().Top) && Near(erase.Margin().Left + erase.Width(), 615)
        && Near(abc.Margin().Left, 0) && Near(enter.Margin().Top, Key(grid, NumSpace).Margin().Top) && Near(enter.Margin().Left + enter.Width(), 615)
        && !Shown(Key(grid, NumHash)) && !Shown(Key(grid, NumHome)) && !Shown(Key(grid, NumTab)) && Shown(Key(grid, 11));
}
winrt::hstring KeyboardParent(const sx::DependencyObject& element) {
    const auto framework = element.try_as<sx::FrameworkElement>();
    return framework && framework.Name() == L"Keyboard" ? winrt::hstring(KeyboardGridType) : winrt::get_class_name(element);
}
}

int main() {
    try {
        Check(IsKeyboardCharacter(L"q") && IsKeyboardCharacter(L"ㅂ") && IsKeyboardCharacter(L"é") && IsKeyboardCharacter(L"?")
            && IsKeyboardCharacter(L"\U0001F600"), "letters in any script, punctuation and one supplementary character are characters");
        Check(!IsKeyboardCharacter(L"") && !IsKeyboardCharacter(L" ") && !IsKeyboardCharacter(L"\uE750") && !IsKeyboardCharacter(L"&123")
            && !IsKeyboardCharacter(L"Ctrl") && !IsKeyboardCharacter(L" "), "glyphs, words and whitespace are not characters");
        Check(IsKeyboardLetter(L"q") && IsKeyboardLetter(L"ㅂ") && IsKeyboardLetter(L"é") && IsKeyboardLetter(L"あ")
            && !IsKeyboardLetter(L"'") && !IsKeyboardLetter(L",") && !IsKeyboardLetter(L"?") && !IsKeyboardLetter(L"\uE750"),
            "Latin, Hangul and kana are letters; punctuation and glyphs are not");

        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto manager = sx::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"JellyflowerKeyboardFixture"; wc.lpfnWndProc = DefWindowProcW; RegisterClassW(&wc);
        const auto window = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"jellyflower keyboard fixture", WS_POPUP, 100, 100, 700, 320, nullptr, nullptr, wc.hInstance, nullptr);
        sx::Hosting::DesktopWindowXamlSource host; auto native = host.as<IDesktopWindowXamlSourceNative>();
        winrt::check_hresult(native->AttachToWindow(window)); HWND child = nullptr; native->get_WindowHandle(&child);
        SetWindowPos(child, nullptr, 0, 0, 700, 320, SWP_SHOWWINDOW);

        sx::Controls::Grid root; root.HorizontalAlignment(sx::HorizontalAlignment::Left); root.VerticalAlignment(sx::VerticalAlignment::Top);
        sx::Controls::Grid keyboard; keyboard.Name(L"Keyboard"); keyboard.Width(615); keyboard.Height(246);
        sx::Controls::Grid grid; grid.Name(L"TEMPLATE_PART_KeyGrid");
        keyboard.Children().Append(grid); root.Children().Append(keyboard);
        keyboard.HorizontalAlignment(sx::HorizontalAlignment::Left); keyboard.VerticalAlignment(sx::VerticalAlignment::Top); keyboard.Margin({5, 40, 0, 0});
        sx::Controls::Grid bar; bar.Name(L"TEMPLATE_PART_CandidateBar"); bar.Width(626); bar.Height(40);
        bar.HorizontalAlignment(sx::HorizontalAlignment::Left); bar.VerticalAlignment(sx::VerticalAlignment::Top);
        sx::Controls::Grid ribbon; ribbon.Name(L"WebUIRoot");
        sx::Controls::Button settings; settings.Name(L"KeyboardSettingsMenuButton"); settings.Width(33.33); settings.HorizontalAlignment(sx::HorizontalAlignment::Left); settings.Margin({3.33, 0, 0, 0});
        sx::Controls::Button close; close.Name(L"SuggestionUIContainerCloseButton"); close.Width(33.33); close.HorizontalAlignment(sx::HorizontalAlignment::Right); close.Margin({0, 0, 3.33, 0});
        ribbon.Children().Append(settings); ribbon.Children().Append(close); bar.Children().Append(ribbon); root.Children().Append(bar);
        host.Content(root);
        const auto stock = Alphabet();
        Fill(grid, stock);
        Layout(root);
        Check(!IsKeyboardKeyGrid(grid) && IsKeyboardKeyGrid(grid, KeyboardParent), "only the key grid inside the keyboard's KeyboardGrid is recognised");

        KeyboardMirror mirror(grid);
        mirror.Refresh(true); Layout(root);
        if (!mirror.Applied() || !IsCompact(grid)) {
            std::printf("applied=%d compact=%d failed=%d arrangements=%u scale=%g\n", mirror.Applied(), mirror.Compact(), mirror.Failed(), mirror.Arrangements(),
                grid.XamlRoot() ? grid.XamlRoot().RasterizationScale() : -1.0);
            for (const auto index : {Q, Backspace, A, Enter, Z, Ctrl, Space, Language}) {
                const auto key = Key(grid, index);
                const auto bounds = Bounds(key);
                std::printf("  key %zu: %g,%g %gx%g shown=%d bounds=%g,%g radius=%g\n", index, key.Margin().Left, key.Margin().Top, key.Width(), key.Height(), Shown(key),
                    bounds.Margin().Left, bounds.Margin().Top, Background(key).CornerRadius().TopLeft);
            }
        }
        Check(mirror.Applied() && mirror.Compact() && IsCompact(grid), "portrait keyboard shows the compact layout with gaps, corners and hidden extras");
        {
            // The suggestion bar's outer icons line up with the outer keys.
            const auto keyLeft = Bounds(Key(grid, Q)).TransformToVisual(root).TransformPoint({0, 0}).X;
            const auto pBounds = Bounds(Key(grid, 9));
            const auto keyRight = pBounds.TransformToVisual(root).TransformPoint({0, 0}).X + pBounds.ActualWidth();
            const auto iconLeft = settings.TransformToVisual(root).TransformPoint({0, 0}).X;
            const auto iconRight = close.TransformToVisual(root).TransformPoint({0, 0}).X + close.ActualWidth();
            if (!Near(iconLeft, keyLeft, 1.5) || !Near(iconRight, keyRight, 1.5))
                std::printf("ribbon margin=%g,%g icons=%g..%g keys=%g..%g\n", ribbon.Margin().Left, ribbon.Margin().Right, iconLeft, iconRight, keyLeft, keyRight);
            Check(Near(iconLeft, keyLeft, 1.5) && Near(iconRight, keyRight, 1.5) && ribbon.Margin().Left > 9, "the suggestion bar's icons line up with the outer keys");
        }
        Check(Key(grid, Q).ActualWidth() > 60 && Bounds(Key(grid, Q)).ActualWidth() < 58, "slots stay wide for touch while the visible key narrows");

        // The keyboard rewrites every key on a page or size change; the new
        // values become the originals and are arranged again without a Refresh.
        for (size_t i = 0; i < stock.size(); ++i) { const auto key = Key(grid, i); key.Margin({stock[i].left, stock[i].top, 0, 0}); key.Width(stock[i].width); key.Height(stock[i].height); key.Visibility(sx::Visibility::Visible); }
        Layout(root);
        Check(IsCompact(grid), "a full rewrite by the keyboard is arranged again from LayoutUpdated");
        Key(grid, Backspace).Margin({513, 0, 0, 0}); Layout(root);
        Check(IsCompact(grid), "a single rewritten key is arranged again");
        Key(grid, Apostrophe).Visibility(sx::Visibility::Visible); Layout(root);
        Check(IsCompact(grid), "a key the keyboard shows again is hidden again");

        // A page this arrangement does not understand (here, five rows) stays stock.
        auto five = Alphabet();
        for (int i = 0; i < 10; ++i) five.push_back({L"1", L"", i * 61.5, -61, 61.5, 61});
        Fill(grid, five); Layout(root);
        Check(!mirror.Applied() && IsStock(grid, five), "unknown pages are left stock");
        Fill(grid, stock); Layout(root);
        Check(mirror.Applied() && IsCompact(grid), "returning to the letters page arranges again");
        const auto numbers = Numbers();
        Fill(grid, numbers); Layout(root);
        Check(mirror.Applied() && mirror.Symbols() && IsNumbers(grid), "the &123 page takes Apple's 123 rows in the same frame");
        Fill(grid, stock); Layout(root);
        Check(mirror.Applied() && !mirror.Symbols() && IsCompact(grid), "and back to letters");
        // A page switch removes and re-adds the very same key elements. Their
        // hints must keep one 80% override, not shrink again each time.
        {
            std::vector<sx::UIElement> same;
            for (const auto& element : grid.Children()) same.push_back(element);
            for (int round = 0; round < 4; ++round) {
                grid.Children().Clear(); Layout(root);
                for (const auto& element : same) grid.Children().Append(element);
                Layout(root);
            }
            Check(IsCompact(grid) && HintIs(Key(grid, Q), 9.6, 0x8B), "re-added keys keep a single hint override");
            mirror.Refresh(false); Layout(root);
            Check(IsStock(grid, stock), "and restore to the keyboard's own hint size and color");
            mirror.Refresh(true); Layout(root);
            Check(IsCompact(grid), "re-enabled after the re-add check");
        }
        {
            // Switching input language rewrites the key's name; the globe stays,
            // and turning the layout off shows the new name.
            LanguageName(grid, Language).Text(L"한"); Layout(root);
            Check(IsCompact(grid), "a language switch keeps the globe");
            mirror.Refresh(false); Layout(root);
            Check(LanguageName(grid, Language).Text() == L"한", "and restores to the language now in use");
            LanguageName(grid, Language).Text(L"ENG");
            mirror.Refresh(true); Layout(root);
            Check(IsCompact(grid), "re-enabled after the language check");
        }
        {
            // Return: an empty field dims it and stops taps; text gives it the
            // accent fill; an unknown field leaves Windows' own look.
            const auto enter = Key(grid, Enter);
            const auto label = FindStartElement(enter, L"TEMPLATE_PART_KeyLabel").as<sx::Controls::TextBlock>();
            const auto fill = Background(enter);
            const auto stockInk = label.Foreground(), stockFill = fill.Background();
            const auto alpha = [](const sx::Media::Brush& brush) { const auto solid = brush ? brush.try_as<sx::Media::SolidColorBrush>() : nullptr; return solid ? solid.Color().A : 0; };
            mirror.Refresh(true, KeyboardField::Empty); Layout(root);
            Check(mirror.EnterStyle() == KeyboardField::Empty && !enter.IsHitTestVisible() && (alpha(label.Foreground()) == 0x5C || alpha(label.Foreground()) == 0x5D),
                "an empty field dims return and stops its taps");
            mirror.Refresh(true, KeyboardField::Text); Layout(root);
            Check(mirror.EnterStyle() == KeyboardField::Text && enter.IsHitTestVisible() && alpha(fill.Background()) == 255 && alpha(label.Foreground()) == 255,
                "text gives return the accent fill");
            mirror.Refresh(true, KeyboardField::Unknown); Layout(root);
            Check(mirror.EnterStyle() == KeyboardField::Unknown && enter.IsHitTestVisible() && label.Foreground() == stockInk && fill.Background() == stockFill,
                "an unknown field restores Windows' return key");
            // The style follows return onto the &123 page and back.
            mirror.Refresh(true, KeyboardField::Text);
            Fill(grid, Numbers()); Layout(root);
            Check(mirror.Symbols() && alpha(Background(Key(grid, NumEnter)).Background()) == 255, "&123's return takes the accent too");
            Fill(grid, stock); Layout(root);
            Check(IsCompact(grid) && alpha(Background(Key(grid, Enter)).Background()) == 255, "and the letters' return again");
            mirror.Refresh(false); Layout(root);
            Check(IsStock(grid, stock) && Key(grid, Enter).IsHitTestVisible() && !Background(Key(grid, Enter)).Background(), "disable restores return");
            mirror.Refresh(true); Layout(root);
            Check(IsCompact(grid), "re-enabled after the return checks");
        }

        // Keys whose templates are not applied yet are retried, not guessed.
        grid.Children().Clear();
        for (const auto& s : stock) grid.Children().Append(MakeKey(s, false));
        Layout(root);
        Check(!mirror.Applied(), "keys without labels yet stay stock");
        grid.Children().Clear();
        for (const auto& s : stock) grid.Children().Append(MakeKey(s));
        Layout(root);
        Check(mirror.Applied() && IsCompact(grid), "labels arriving later trigger the arrangement");

        // Turning it off restores the keyboard's latest values exactly.
        mirror.Refresh(false); Layout(root);
        Check(!mirror.Applied() && IsStock(grid, stock), "disable restores the stock layout, visibility, gaps and corners");
        Check(LanguageName(grid, Language).Text() == L"ENG" && std::wstring_view(LanguageName(grid, Language).FontFamily().Source()).substr(0, 18) != L"Segoe Fluent Icons",
            "disable gives the language key its name back");
        Check(Near(ribbon.Margin().Left, 0) && Near(ribbon.Margin().Right, 0), "disable restores the suggestion bar");
        Key(grid, Backspace).Margin({513, 0, 0, 0}); Layout(root);
        Check(IsStock(grid, stock), "a disabled adapter no longer reacts to layout passes");

        // A keyboard that keeps rewriting our values is left alone, stock.
        mirror.Refresh(true); Layout(root);
        Check(IsCompact(grid), "re-enabled");
        const auto a = Key(grid, A);
        bool fighting = true;
        const auto token = a.RegisterPropertyChangedCallback(sx::FrameworkElement::MarginProperty(), [&](auto&&, auto&&) {
            if (fighting && !Near(a.Margin().Left, 26)) a.Margin({26, 62, 0, 0});
        });
        a.Margin({26, 62, 0, 0});
        for (int i = 0; i < 6; ++i) Layout(root);
        fighting = false;
        a.UnregisterPropertyChangedCallback(sx::FrameworkElement::MarginProperty(), token);
        Check(mirror.Failed() && !mirror.Applied() && IsStock(grid, stock), "a fighting layout gives up and leaves the keyboard stock");
        mirror.Refresh(false); mirror.Refresh(true); Layout(root);
        Check(!mirror.Failed() && IsCompact(grid), "turning it off and on again retries");

        // Destruction restores, like every other adapter.
        mirror.Restore(); Layout(root);
        Check(IsStock(grid, stock), "restore leaves the stock layout");
        { KeyboardMirror scoped(grid); scoped.Refresh(true); Layout(root); Check(IsCompact(grid), "scoped adapter arranges"); }
        Layout(root);
        Check(IsStock(grid, stock), "destructor restores the stock layout");

        host.Content(nullptr); host.Close(); DestroyWindow(window); manager.Close(); Pump();
        std::puts("PASS: compact keyboard arranged with hidden extras, gaps and corners; re-arranged after keyboard rewrites, re-shown keys and late labels; unknown pages and fighting layouts left stock; and every property restored on disable, restore and destruction.");
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::printf("FAIL HRESULT %08lx\n", static_cast<unsigned long>(e.code()));
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
    }
    return 1;
}
