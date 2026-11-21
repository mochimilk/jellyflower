// SPDX-License-Identifier: GPL-3.0-or-later
// Pure geometry of the tablet-style touch keyboard layouts. No shell or desktop.
#include "../src/keyboard_layout.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace acrylic;
using Kind = KeyboardKeyKind;
using Role = KeyboardKeyRole;

namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
bool Near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

struct Fixture {
    std::vector<KeyboardKeySlot> keys;
    std::vector<std::wstring> names;
    void Add(const wchar_t* name, Kind kind, double left, double top, double width, double height, Role role = Role::None) {
        KeyboardKeySlot slot;
        slot.kind = kind; slot.role = role;
        const std::wstring text(name);
        if (kind == Kind::Character && text.size() == 1) {
            slot.symbol = text[0];
            slot.letter = (text[0] >= L'a' && text[0] <= L'z');
        }
        slot.left = left; slot.top = top; slot.width = width; slot.height = height;
        keys.push_back(slot);
        names.push_back(name);
    }
    size_t Index(const wchar_t* name) const {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == name) return i;
        throw std::runtime_error("fixture key missing");
    }
};
const double Row1[] = {0, 52, 103, 154, 205, 257, 308, 359, 410, 462}, Width1[] = {52, 51, 51, 51, 52, 51, 51, 51, 52, 51};
const double Row2[] = {26, 77, 128, 180, 231, 282, 334, 385, 436, 488}, Width2[] = {51, 51, 52, 51, 51, 52, 51, 51, 52, 51};
const double Row3[] = {52, 103, 154, 205, 257, 308, 359, 410, 462, 513}, Width3[] = {51, 51, 51, 52, 51, 51, 51, 52, 51, 51};
void Chars(Fixture& f, const wchar_t* const* names, size_t count, const double* lefts, const double* widths, double top, double height) {
    for (size_t i = 0; i < count; ++i) f.Add(names[i], Kind::Character, lefts[i], top, widths[i], height);
}
// Windows 11 25H2 docked portrait keyboard, 615 DIP at 300% (measured live).
Fixture Narrow() {
    Fixture f;
    const wchar_t* row1[] = {L"q", L"w", L"e", L"r", L"t", L"y", L"u", L"i", L"o", L"p"};
    Chars(f, row1, 10, Row1, Width1, 0, 62);
    f.Add(L"backspace", Kind::Function, 513, 0, 102, 62);
    const wchar_t* row2[] = {L"a", L"s", L"d", L"f", L"g", L"h", L"j", L"k", L"l", L"'"};
    Chars(f, row2, 10, Row2, Width2, 62, 61);
    f.Add(L"enter", Kind::Function, 539, 62, 76, 61);
    f.Add(L"shift-left", Kind::Function, 0, 123, 52, 62);
    const wchar_t* row3[] = {L"z", L"x", L"c", L"v", L"b", L"n", L"m", L",", L".", L"?"};
    Chars(f, row3, 10, Row3, Width3, 123, 62);
    f.Add(L"shift-right", Kind::Function, 564, 123, 51, 62);
    f.Add(L"&123", Kind::Function, 0, 185, 52, 61);
    f.Add(L"ctrl", Kind::Function, 52, 185, 51, 61, Role::Control);
    f.Add(L"mic", Kind::Function, 103, 185, 51, 61, Role::Microphone);
    f.Add(L"space", Kind::Space, 154, 185, 308, 61);
    f.Add(L"left", Kind::Function, 462, 185, 51, 61);
    f.Add(L"right", Kind::Function, 513, 185, 51, 61);
    f.Add(L"language", Kind::Function, 564, 185, 51, 61, Role::Language);
    return f;
}
// The same keyboard's &123 page (positions from a live screenshot).
Fixture Symbols() {
    Fixture f;
    const wchar_t* row1[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};
    Chars(f, row1, 10, Row1, Width1, 0, 62);
    f.Add(L"backspace", Kind::Function, 513, 0, 102, 62);
    const wchar_t* row2[] = {L"!", L"@", L"#", L"$", L"^", L"&", L"_", L"-", L"=", L"+"};
    Chars(f, row2, 10, Row2, Width2, 62, 61);
    f.Add(L"enter", Kind::Function, 539, 62, 76, 61);
    f.Add(L"more", Kind::Function, 0, 123, 52, 62);
    const wchar_t* row3[] = {L";", L":", L"(", L")", L"/", L"'", L"\"", L"?"};
    Chars(f, row3, 8, Row3, Width3, 123, 62);
    f.Add(L"home", Kind::Function, 462, 123, 51, 62);
    f.Add(L"up", Kind::Function, 513, 123, 51, 62);
    f.Add(L"end", Kind::Function, 564, 123, 51, 62);
    f.Add(L"abc", Kind::Function, 0, 185, 52, 61);
    f.Add(L"ctrl", Kind::Function, 52, 185, 51, 61, Role::Control);
    f.Add(L"tab", Kind::Function, 103, 185, 51, 61);
    f.Add(L",", Kind::Character, 154, 185, 51, 61);
    f.Add(L"space", Kind::Space, 205, 185, 205, 61);
    f.Add(L".", Kind::Character, 410, 185, 52, 61);
    f.Add(L"left", Kind::Function, 462, 185, 51, 61);
    f.Add(L"down", Kind::Function, 513, 185, 51, 61);
    f.Add(L"right", Kind::Function, 564, 185, 51, 61);
    return f;
}
// A landscape-style page: everything twice as wide, with Tab before q.
Fixture Wide() {
    auto f = Narrow();
    for (auto& key : f.keys) { key.left *= 2; key.width *= 2; }
    for (size_t i = 0; i < 11; ++i) { f.keys[i].left = 104 + f.keys[i].left * (1126.0 / 1230.0); f.keys[i].width *= 1126.0 / 1230.0; }
    f.Add(L"tab", Kind::Function, 0, 0, 104, 62);
    return f;
}

// A key's slot (hit-tested) and its visible shape (slot minus insets).
struct Box {
    double left, right, top, bottom; bool visible;
    double insetLeft, insetRight, insetTop, insetBottom;
    double shownLeft() const { return left + insetLeft; }
    double shownRight() const { return right - insetRight; }
    double shownTop() const { return top + insetTop; }
    double shownBottom() const { return bottom - insetBottom; }
};
Box At(const KeyboardArrangement& a, const Fixture& f, const wchar_t* name) {
    const auto& r = a.keys[f.Index(name)];
    return {r.left, r.left + r.width, r.top, r.top + r.height, r.visible, r.insetLeft, r.insetRight, r.insetTop, r.insetBottom};
}
// Visible slots never overlap and tile each row edge to edge.
void CheckRows(const KeyboardArrangement& a, double width, double height, const char* text) {
    std::vector<std::pair<double, std::pair<double, double>>> rows;
    for (size_t i = 0; i < a.keys.size(); ++i) {
        const auto& k = a.keys[i];
        if (!k.visible) continue;
        Check(k.width > 0 && k.left >= -1e-9 && k.left + k.width <= width + 1e-9 && k.top >= -1e-9 && k.top + k.height <= height + 1e-9, text);
        Check(k.insetLeft + k.insetRight < k.width && k.insetTop + k.insetBottom < k.height, text);
        for (size_t j = i + 1; j < a.keys.size(); ++j) {
            const auto& o = a.keys[j];
            if (!o.visible || !Near(o.top, k.top, .5)) continue;
            Check(o.left >= k.left + k.width - 1e-9 || k.left >= o.left + o.width - 1e-9, text);
        }
        auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return Near(row.first, k.top, .5); });
        if (it == rows.end()) rows.push_back({k.top, {k.left, k.left + k.width}});
        else { it->second.first = (std::min)(it->second.first, k.left); it->second.second = (std::max)(it->second.second, k.left + k.width); }
    }
    for (const auto& row : rows) Check(Near(row.second.first, 0, 1e-9) && Near(row.second.second, width, 1e-9), text);
}
}

int main() {
    try {
        const double width = 615, height = 246, scale = 3, pixel = 1 / scale;
        const double side = std::round(KeyboardSidePadding * 61 * scale) / scale, p = (width - 2 * side) / 10;
        const double above = std::round(KeyboardTopPadding * 61 * scale) / scale, under = std::round(KeyboardBottomPadding * 61 * scale) / scale;
        const double gap = std::round(KeyboardGap * p / 2 * scale) / scale;

        // Compact letters: Apple's minimal layout, for the 615-DIP portrait keyboard.
        const auto narrow = Narrow();
        const auto compact = ArrangeKeyboard(narrow.keys, width, scale);
        Check(compact.keys.size() == narrow.keys.size() && compact.compact && !compact.symbols, "portrait keyboard gets the compact letters layout");
        CheckRows(compact, width, height, "compact slots tile each row edge to edge without overlap");
        for (const auto& k : compact.keys) {
            if (!k.visible) continue;
            Check(Near(k.left * scale, std::round(k.left * scale), 1e-6) && Near((k.left + k.width) * scale, std::round((k.left + k.width) * scale), 1e-6)
                && Near(k.top * scale, std::round(k.top * scale), 1e-6) && Near((k.top + k.height) * scale, std::round((k.top + k.height) * scale), 1e-6),
                "key edges land on device pixels");
        }
        const auto q = At(compact, narrow, L"q"), pKey = At(compact, narrow, L"p"), w = At(compact, narrow, L"w");
        Check(Near(q.left, 0, 1e-9) && Near(q.shownLeft(), side + gap, pixel) && Near(pKey.right, width, 1e-9) && Near(pKey.shownRight(), width - side - gap, pixel),
            "row 1's outer keys reach the edges while their visible keys sit inside the side padding");
        const wchar_t* top[] = {L"w", L"e", L"r", L"t", L"y", L"u", L"i", L"o"};
        for (int i = 0; i < 8; ++i) {
            const auto key = At(compact, narrow, top[i]);
            Check(Near(key.left, side + (i + 1) * p, pixel) && Near(key.right - key.left, p, 2 * pixel), "ten letters span row 1 at an even pitch");
        }
        const auto a = At(compact, narrow, L"a"), l = At(compact, narrow, L"l"), s = At(compact, narrow, L"s");
        Check(Near(a.left, 0, 1e-9) && Near(a.shownLeft(), side + p / 2 + gap, pixel) && Near(s.left, side + 1.5 * p, pixel) && Near(l.shownRight(), width - side - p / 2 - gap, pixel),
            "row 2 sits half a key in, and its indent still types a");
        Check(!At(compact, narrow, L"'").visible, "punctuation leaves the compact letter rows");
        const auto shift = At(compact, narrow, L"shift-left"), z = At(compact, narrow, L"z"), m = At(compact, narrow, L"m");
        const auto erase = At(compact, narrow, L"backspace");
        Check(Near(shift.left, 0, 1e-9) && Near(shift.right, side + 1.5 * p, pixel) && Near(z.left, s.left, pixel) && Near(m.left, At(compact, narrow, L"k").left, pixel),
            "shift takes 1.5 keys; z..m sit under s..k");
        Check(Near(erase.top, z.top, 1e-9) && Near(erase.left, m.right, 1e-9) && Near(erase.right, width, 1e-9), "delete moves to row 3's right end");
        Check(Near(shift.shownLeft(), width - erase.shownRight(), pixel) && Near(shift.shownRight(), width - erase.shownLeft(), pixel), "shift and delete are mirrored");
        Check(shift.insetRight > 3 * gap && Near(shift.insetRight, erase.insetLeft, pixel), "shift and delete keep a wider inner gap");
        for (const auto* name : {L",", L".", L"?", L"shift-right", L"left", L"right"}) Check(!At(compact, narrow, name).visible, "keys without a compact place are hidden");
        const auto symbols = At(compact, narrow, L"&123"), language = At(compact, narrow, L"language"), ctrl = At(compact, narrow, L"ctrl");
        const auto space = At(compact, narrow, L"space"), mic = At(compact, narrow, L"mic"), enter = At(compact, narrow, L"enter");
        Check(Near(symbols.left, 0, 1e-9) && Near(language.left, symbols.right, 1e-9) && Near(ctrl.left, language.right, 1e-9) && Near(space.left, ctrl.right, 1e-9)
            && Near(mic.left, space.right, 1e-9) && Near(enter.left, mic.right, 1e-9) && Near(enter.right, width, 1e-9), "bottom row reads &123, language, Ctrl, space, mic, return");
        Check(Near(enter.top, space.top, 1e-9) && Near(enter.bottom, height, 1e-9), "return moves to the bottom row");
        Check(Near(language.right - language.left, .933 * p, 2 * pixel) && Near(enter.shownRight() - enter.shownLeft(), 1.4 * p - 2 * gap, 2 * pixel),
            "bottom keys follow the measured proportions");
        const auto g = At(compact, narrow, L"g");
        Check(Near(g.insetLeft, gap, 1e-9) && Near(g.insetLeft, g.insetRight, 1e-9) && gap < 4, "letters share an even gap under 7 DIP");
        // Vertical: padding above the first row and under the bottom row.
        const double vertical = g.insetTop;
        Check(Near(g.insetTop, g.insetBottom, 1e-9) && vertical >= 4 && vertical <= 5.5, "rows share an even gap");
        Check(Near(q.top, 0, 1e-9) && Near(q.insetTop, vertical + above, pixel) && above >= 5, "the first row's slot reaches the top while its key sits below the bar's padding");
        Check(Near(space.bottom, height, 1e-9) && Near(space.insetBottom, vertical + under, pixel) && under >= 27, "the bottom row lifts about 27 DIP above the edge");
        Check(Near(q.bottom, a.top, 1e-9) && Near(a.bottom, z.top, 1e-9) && Near(z.bottom, space.top, 1e-9), "rows still tile top to bottom");
        Check(Near(compact.edge, side + gap, pixel) && compact.radius >= 10 && compact.radius <= 12, "outer edge and corners are reported");
        size_t shown = 0;
        for (const auto& k : compact.keys) shown += k.visible ? 1 : 0;
        Check(shown == narrow.keys.size() - 7, "seven keys are hidden");
        std::printf("Compact: side=%.2f pitch=%.2f gap=%.2f top=%.2f bottom=%.2f radius=%.0f edge=%.2f\n", side, p, 2 * gap, q.insetTop, space.insetBottom, compact.radius, compact.edge);

        // Compact &123: Apple's 123 page in the same frame.
        const auto sym = Symbols();
        const auto page = ArrangeKeyboard(sym.keys, width, scale);
        Check(!page.keys.empty() && page.compact && page.symbols, "the &123 page gets the compact symbols layout");
        CheckRows(page, width, height, "symbols tile each row edge to edge without overlap");
        const wchar_t* second[] = {L"-", L"/", L":", L";", L"(", L")", L"$", L"&", L"@", L"\""};
        for (int i = 1; i < 9; ++i) {
            const auto key = At(page, sym, second[i]);
            Check(key.visible && Near(key.left, side + i * p, pixel) && Near(key.right - key.left, p, 2 * pixel) && Near(key.top, a.top, 1e-9),
                "row 2 reads - / : ; ( ) $ & @ \" at the letters' pitch");
        }
        Check(Near(At(page, sym, L"-").left, 0, 1e-9) && Near(At(page, sym, L"\"").right, width, 1e-9), "row 2's outer symbols reach the edges");
        const auto more = At(page, sym, L"more"), pageErase = At(page, sym, L"backspace");
        Check(Near(more.left, 0, 1e-9) && Near(more.right, shift.right, 1e-9) && Near(more.top, shift.top, 1e-9) && Near(more.insetRight, shift.insetRight, 1e-9),
            "the next-symbols key sits exactly where shift does");
        Check(Near(pageErase.left, erase.left, 1e-9) && Near(pageErase.top, erase.top, 1e-9) && Near(pageErase.right, width, 1e-9), "delete sits exactly where it does on the letters");
        const wchar_t* third[] = {L".", L",", L"?", L"!", L"'"};
        for (int i = 0; i < 5; ++i) {
            const auto key = At(page, sym, third[i]);
            Check(key.visible && Near(key.left, side + 1.5 * p + i * 1.4 * p, pixel) && Near(key.right - key.left, 1.4 * p, 2 * pixel) && Near(key.top, shift.top, 1e-9),
                "row 3 reads . , ? ! ' as five wide keys");
        }
        const auto abc = At(page, sym, L"abc"), pageCtrl = At(page, sym, L"ctrl"), pageSpace = At(page, sym, L"space"), pageEnter = At(page, sym, L"enter");
        Check(Near(abc.left, 0, 1e-9) && Near(abc.right, symbols.right, 1e-9) && Near(pageCtrl.left, abc.right, 1e-9) && Near(pageSpace.left, pageCtrl.right, 1e-9)
            && Near(pageEnter.left, enter.left, 1e-9) && Near(pageEnter.right, width, 1e-9) && Near(pageSpace.right, pageEnter.left, 1e-9),
            "bottom row reads abc, Ctrl, space, return; abc and return sit where &123 and return do");
        for (const auto* name : {L"#", L"^", L"_", L"=", L"+", L"home", L"up", L"end", L"tab", L"left", L"down", L"right"})
            Check(!At(page, sym, name).visible, "navigation and #+= symbols are hidden");
        auto missing = Symbols();
        missing.keys[missing.Index(L"@")].symbol = L'%';
        Check(ArrangeKeyboard(missing.keys, width, scale).keys.empty(), "a symbols page without Apple's characters stays stock");

        // Input order does not matter, and results follow the input order.
        auto shuffled = narrow;
        for (size_t i = 0; i + 1 < shuffled.keys.size(); i += 2) { std::swap(shuffled.keys[i], shuffled.keys[i + 1]); std::swap(shuffled.names[i], shuffled.names[i + 1]); }
        const auto again = ArrangeKeyboard(shuffled.keys, width, scale);
        Check(again.keys.size() == compact.keys.size(), "shuffled keys are recognised");
        for (size_t i = 0; i < again.keys.size(); ++i) {
            const auto& expected = compact.keys[narrow.Index(shuffled.names[i].c_str())];
            Check(Near(again.keys[i].left, expected.left, 1e-9) && again.keys[i].visible == expected.visible, "results follow the input order");
        }
        // A row 2 of ten letters (AZERTY's q..m) is not indented.
        auto azerty = Narrow();
        azerty.keys[azerty.Index(L"'")].letter = true;
        const auto french = ArrangeKeyboard(azerty.keys, width, scale);
        Check(!french.keys.empty() && Near(At(french, azerty, L"a").shownLeft(), side + gap, pixel) && At(french, azerty, L"'").visible, "ten-letter second rows are not indented");
        // Without a language key, the space bar grows.
        auto single = Narrow();
        single.keys[single.Index(L"language")].role = Role::None;
        const auto one = ArrangeKeyboard(single.keys, width, scale);
        Check(!one.keys.empty() && !At(one, single, L"language").visible && At(one, single, L"space").right - At(one, single, L"space").left > space.right - space.left,
            "a missing language key widens space");

        // Full: iPad Pro proportions, for wide keyboards.
        const double wideWidth = 1230, wideSide = std::round(KeyboardSidePadding * 61 * 2) / 2, u = (wideWidth - 2 * wideSide) / 12.5;
        const auto wide = Wide();
        const auto full = ArrangeKeyboard(wide.keys, wideWidth, 2);
        Check(!full.keys.empty() && !full.compact, "wide keyboards get the full layout");
        CheckRows(full, wideWidth, height, "full slots tile each row edge to edge without overlap");
        const auto tab = At(full, wide, L"tab"), fq = At(full, wide, L"q"), backspace = At(full, wide, L"backspace");
        Check(Near(tab.left, 0, 1e-9) && Near(tab.right, wideSide + 1.25 * u, .5) && Near(fq.left, wideSide + 1.25 * u, .5) && Near(backspace.right - backspace.left, wideSide + 1.25 * u, 1),
            "row 1: tab and delete are 1.25 units each");
        const auto fullCtrl = At(full, wide, L"ctrl"), fullA = At(full, wide, L"a"), fullEnter = At(full, wide, L"enter");
        Check(Near(fullCtrl.top, fullA.top, 1e-9) && Near(fullCtrl.left, 0, 1e-9) && Near(fullCtrl.right, wideSide + 1.55 * u, .5), "Ctrl takes the caps slot, 1.55 units");
        Check(Near(fullEnter.right - fullEnter.left, wideSide + 1.95 * u, 1) && !At(full, wide, L"'").visible, "return is 1.95 units after nine letters");
        const auto fullShift = At(full, wide, L"shift-left"), fullZ = At(full, wide, L"z"), fullRight = At(full, wide, L"shift-right");
        Check(Near(fullShift.right, wideSide + 2 * u, .5) && Near(fullRight.right - fullRight.left, wideSide + 1.5 * u, 1) && At(full, wide, L",").visible && At(full, wide, L".").visible
            && !At(full, wide, L"?").visible, "row 3: shifts are 2.0 and 1.5 units around z..m , .");
        Check(Near(fullA.left - fq.left, .3 * u, .5) && Near(fullZ.left - fq.left, .75 * u, .5), "letters stagger like a hardware keyboard");
        const auto fullLanguage = At(full, wide, L"language"), fullSymbols = At(full, wide, L"&123"), fullMic = At(full, wide, L"mic");
        const auto fullSpace = At(full, wide, L"space"), arrowLeft = At(full, wide, L"left"), arrowRight = At(full, wide, L"right");
        Check(Near(fullLanguage.left, 0, 1e-9) && Near(fullSymbols.left, fullLanguage.right, 1e-9) && Near(fullMic.left, fullSymbols.right, 1e-9)
            && Near(fullSpace.left, fullMic.right, 1e-9) && Near(arrowLeft.left, fullSpace.right, 1e-9) && Near(arrowRight.right, wideWidth, 1e-9),
            "bottom row reads language, &123, mic, space, left, right");
        Check(Near(fullSpace.bottom, height, 1e-9) && fullSpace.insetBottom > fullSpace.insetTop + 20, "the full layout keeps the bottom padding too");
        Check(ArrangeKeyboard(Symbols().keys, wideWidth, 2).keys.empty(), "a wide &123 page stays stock");
        std::printf("Full: unit=%.2f a-q=%.2f z-q=%.2f space=%.2f-%.2f\n", u, fullA.left - fq.left, fullZ.left - fq.left, fullSpace.left, fullSpace.right);
        // Without Tab, Ctrl takes row 1 instead.
        auto plain = Narrow();
        for (auto& key : plain.keys) { key.left *= 1.5; key.width *= 1.5; }
        const auto plainFull = ArrangeKeyboard(plain.keys, 922.5, 2);
        Check(!plainFull.keys.empty() && !plainFull.compact && Near(At(plainFull, plain, L"ctrl").top, 0, 1e-9), "Ctrl takes the tab slot when there is no Tab");

        // Pages this does not understand stay stock.
        auto five = Narrow();
        for (int i = 0; i < 10; ++i) five.Add(L"digit", Kind::Character, i * 61.5, -61, 61.5, 61);
        Check(ArrangeKeyboard(five.keys, width, scale).keys.empty(), "traditional (five-row) layout is left alone");
        auto split = Narrow();
        for (auto& key : split.keys) if (key.left >= 257 && key.top < 120) key.left += 120;
        Check(ArrangeKeyboard(split.keys, width + 120, scale).keys.empty(), "split keyboard is left alone");
        auto middle = Narrow();
        middle.keys[middle.Index(L"t")].kind = Kind::Function;
        Check(ArrangeKeyboard(middle.keys, width, scale).keys.empty(), "a function key between characters is left alone");
        auto noSpace = Narrow();
        noSpace.keys[noSpace.Index(L"space")].kind = Kind::Function;
        noSpace.keys[noSpace.Index(L"space")].width = 100;
        Check(ArrangeKeyboard(noSpace.keys, width, scale).keys.empty(), "a bottom row without a space bar is left alone");
        auto worded = Narrow();
        worded.keys[worded.Index(L"space")].kind = Kind::Function;  // e.g. a language name on the space bar
        Check(!ArrangeKeyboard(worded.keys, width, scale).keys.empty(), "the widest bottom key is the space bar even with a word on it");
        auto few = Narrow();
        few.keys.resize(12);
        Check(ArrangeKeyboard(few.keys, width, scale).keys.empty(), "too few keys is not a keyboard page");
        auto hidden = Narrow();
        hidden.keys[0].width = 0;
        Check(ArrangeKeyboard(hidden.keys, width, scale).keys.empty(), "a zero-width key rejects the page");
        Check(ArrangeKeyboard(narrow.keys, 0, scale).keys.empty() && ArrangeKeyboard(narrow.keys, width, 0).keys.empty(), "invalid width or scale is rejected");
        std::puts("KeyboardLayout passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "KeyboardLayout failed: %s\n", error.what());
        return 1;
    }
}
