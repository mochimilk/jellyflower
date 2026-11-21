// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Tablet-style arrangements of the Windows 11 touch keyboard's letter and
// &123 pages. Pure geometry: the XAML adapter (keyboard_view.hpp) reads each
// key's stock rectangle, kind, role and character, and applies what is
// returned here.
//
// Proportions follow Apple's iPadOS 26 docked keyboards, measured from
// screenshots in key pitches (p):
// - Compact (keyboards narrower than KeyboardCompactWidth), letters: ten
//   letters across the full width; the next row's letters sit half a pitch in;
//   shift and delete take 1.5p each around z..m, with a wider inner gap; the
//   bottom row is three small keys, space, microphone and a wider return.
// - Compact, &123: Apple's 123 page. Digits across; - / : ; ( ) $ & @ ";
//   the next-symbols key, five wide keys . , ? ! ' and delete; the same bottom
//   row as the letters. Keys either page has no place for are hidden; on the
//   &123 page that is the navigation keys and # ^ _ = + (Apple's #+= page).
// - Full (wider keyboards), letters only: rows of 12.5 units. Tab and delete
//   are 1.25 each; caps 1.55 and return 1.95; shifts 2.0 and 1.5, so the letters
//   stagger like a hardware keyboard while both edges stay flush. Ctrl takes an
//   empty left end; the bottom row puts the language key, &123 and microphone
//   left of space and the arrows right of it.
// Gaps between keys and the padding under the bottom row are visual insets
// inside each key's slot: slots still tile the keyboard, so touches anywhere
// reach the nearest key, as with the stock layout.
namespace acrylic {
enum class KeyboardKeyKind { Character, Function, Space };
enum class KeyboardKeyRole { None, Control, Language, Microphone };
struct KeyboardKeySlot {
    KeyboardKeyKind kind = KeyboardKeyKind::Function;
    KeyboardKeyRole role = KeyboardKeyRole::None;
    bool letter = false;   // a character key that is a letter, not punctuation
    wchar_t symbol = 0;    // a character key's single UTF-16 character, if any
    double left = 0, top = 0, width = 0, height = 0;
};
struct KeyboardKeyRect {
    double left = 0, top = 0, width = 0, height = 0;
    bool visible = true;
    double insetLeft = 0, insetTop = 0, insetRight = 0, insetBottom = 0;  // visual gap inside the slot
};
struct KeyboardArrangement {
    std::vector<KeyboardKeyRect> keys;  // one per input key; empty leaves the page stock
    bool compact = false, symbols = false;
    double radius = 0;
    double edge = 0;  // from the key grid's side to the outer keys' visible edge
    size_t enter = static_cast<size_t>(-1);  // index of the return key, if found
};

inline constexpr double KeyboardCompactWidth = 800;   // DIP
inline constexpr double KeyboardGap = .11;            // between keys, of a pitch
inline constexpr double KeyboardRowGap = .17;         // between rows, of a row
// Padding around the keys, of a row's height: the sides, above the first
// row (under the suggestion bar) and under the bottom row.
inline constexpr double KeyboardSidePadding = .13, KeyboardTopPadding = .1, KeyboardBottomPadding = .45;
namespace keyboard_detail {
inline constexpr double CompactSide = 1.5, CompactSideVisual = 1.15;  // shift/delete slot and visible width, pitches
inline constexpr double CompactSmallKey = .933, CompactReturn = 1.4;
inline constexpr wchar_t SymbolsSecondRow[] = L"-/:;()$&@\"", SymbolsThirdRow[] = L".,?!'";
inline constexpr double FullExtra = 2.5, FullTab = 1.25, FullCaps = 1.55, FullShift = 2.0;
inline constexpr double FullMinimumSide = 1.25, FullLeftKey = 1.05, FullRightKey = 1.5;

struct Row { std::vector<size_t> lead, chars, trail; };
struct Parsed {
    std::vector<std::vector<size_t>> rows;
    std::vector<Row> letters;  // rows 1-3
    size_t space = 0;
    double stockUnit = 0;
};
inline bool Parse(const std::vector<KeyboardKeySlot>& keys, Parsed& parsed) {
    if (keys.size() < 20 || keys.size() > 80) return false;
    for (const auto& key : keys)
        if (!(key.width > 0) || !(key.height > 0) || !std::isfinite(key.left) || !std::isfinite(key.top)) return false;
    std::vector<size_t> order(keys.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return keys[a].top < keys[b].top; });
    double rowTop = 0;
    for (const auto index : order) {
        if (parsed.rows.empty() || keys[index].top - rowTop > 2) { parsed.rows.emplace_back(); rowTop = keys[index].top; }
        parsed.rows.back().push_back(index);
    }
    if (parsed.rows.size() != 4) return false;
    for (auto& row : parsed.rows) std::stable_sort(row.begin(), row.end(), [&](size_t a, size_t b) { return keys[a].left < keys[b].left; });
    parsed.letters.assign(3, {});
    std::vector<double> widths;
    for (size_t r = 0; r < 3; ++r) {
        auto& row = parsed.letters[r];
        for (const auto index : parsed.rows[r]) {
            const auto kind = keys[index].kind;
            if (kind == KeyboardKeyKind::Space) return false;
            if (kind == KeyboardKeyKind::Character) {
                if (!row.trail.empty()) return false;  // a function key between characters
                row.chars.push_back(index);
                widths.push_back(keys[index].width);
            } else (row.chars.empty() ? row.lead : row.trail).push_back(index);
        }
        if (row.chars.size() < 7 || row.chars.size() > 12) return false;
    }
    std::sort(widths.begin(), widths.end());
    parsed.stockUnit = widths[widths.size() / 2];
    for (const auto& row : parsed.rows)  // a wide gap inside a row: the split keyboard
        for (size_t i = 1; i < row.size(); ++i)
            if (keys[row[i]].left - (keys[row[i - 1]].left + keys[row[i - 1]].width) > parsed.stockUnit / 2) return false;
    // The space bar is the bottom row's widest key, even with a word on it.
    parsed.space = keys.size();
    for (const auto index : parsed.rows[3])
        if (keys[index].kind == KeyboardKeyKind::Space || keys[index].width >= 2.5 * parsed.stockUnit)
            if (parsed.space == keys.size() || keys[index].width > keys[parsed.space].width) parsed.space = index;
    return parsed.space != keys.size();
}
inline size_t Find(const std::vector<KeyboardKeySlot>& keys, const std::vector<size_t>& row, KeyboardKeyRole role) {
    for (const auto index : row) if (keys[index].role == role && keys[index].kind != KeyboardKeyKind::Character) return index;
    return keys.size();
}
}

// Returns an empty arrangement for pages this does not understand (split,
// traditional, number pad, handwriting, emoji): those stay stock.
inline KeyboardArrangement ArrangeKeyboard(const std::vector<KeyboardKeySlot>& keys, double fullWidth, double scale = 1) {
    using namespace keyboard_detail;
    KeyboardArrangement result;
    Parsed parsed;
    if (!(fullWidth > 0) || !(scale > 0) || !Parse(keys, parsed)) return result;
    // Keys are laid out inside the side padding; the outer keys' slots are
    // stretched back to the edges at the end, so touches there still land.
    const double rowSize = keys[parsed.space].height;
    const double side = std::round(KeyboardSidePadding * rowSize * scale) / scale;
    const double width = fullWidth - 2 * side;
    if (!(width > 0)) return result;
    const auto& bottom = parsed.rows[3];
    const size_t none = keys.size();
    std::vector<KeyboardKeyRect> rects(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) rects[i] = {keys[i].left, keys[i].top, keys[i].width, keys[i].height};
    std::vector<bool> placed(keys.size(), false);
    double pitch = 0;
    const auto place = [&](size_t index, double left, double right, double top, double height) {
        rects[index].left = left; rects[index].width = right - left;
        rects[index].top = top; rects[index].height = height;
        rects[index].insetLeft = rects[index].insetRight = KeyboardGap * pitch / 2;
        placed[index] = true;
    };
    const auto rowTop = [&](size_t r) { return keys[parsed.letters[r].chars.front()].top; };
    const auto rowHeight = [&](size_t r) { return keys[parsed.letters[r].chars.front()].height; };
    const double bottomTop = keys[parsed.space].top, bottomHeight = keys[parsed.space].height;
    const auto split = [&](const std::vector<size_t>& chars, std::vector<size_t>& letters, std::vector<size_t>& marks) {
        for (const auto index : chars) (keys[index].letter ? letters : marks).push_back(index);
    };
    const size_t n1 = parsed.letters[0].chars.size();
    result.compact = fullWidth < KeyboardCompactWidth;
    if (result.compact) {
        pitch = width / static_cast<double>(n1);
        // Row 1: letters or digits alone, edge to edge. Delete moves to row 3
        // and return to the bottom row, on both pages.
        const auto& row1 = parsed.letters[0];
        for (size_t i = 0; i < n1; ++i) place(row1.chars[i], i * pitch, (i + 1) * pitch, rowTop(0), rowHeight(0));
        if (row1.trail.empty() || parsed.letters[1].trail.empty() || parsed.letters[2].lead.empty()) return {};
        const size_t erase = row1.trail.front(), enter = parsed.letters[1].trail.front(), shift = parsed.letters[2].lead.front();
        result.enter = enter;
        std::vector<size_t> letters2, marks2;
        split(parsed.letters[1].chars, letters2, marks2);
        result.symbols = letters2.empty();
        double wing = CompactSide * pitch;
        if (!result.symbols) {
            // Letters: row 2 centred (half a pitch in on QWERTY), row 3's
            // letters centred between shift and delete.
            if (letters2.size() > n1 || letters2.size() + 3 < n1) return {};
            const double offset2 = (width - letters2.size() * pitch) / 2;
            for (size_t i = 0; i < letters2.size(); ++i) place(letters2[i], offset2 + i * pitch, offset2 + (i + 1) * pitch, rowTop(1), rowHeight(1));
            std::vector<size_t> letters3, marks3;
            split(parsed.letters[2].chars, letters3, marks3);
            if (letters3.empty()) return {};
            wing = (width - letters3.size() * pitch) / 2;
            if (wing < pitch) return {};
            for (size_t i = 0; i < letters3.size(); ++i) place(letters3[i], wing + i * pitch, wing + (i + 1) * pitch, rowTop(2), rowHeight(2));
        } else {
            // &123: Apple's rows, picked by character from anywhere on the page.
            const auto find = [&](wchar_t symbol) {
                for (size_t i = 0; i < keys.size(); ++i) if (keys[i].kind == KeyboardKeyKind::Character && keys[i].symbol == symbol) return i;
                return none;
            };
            std::vector<size_t> second, third;
            for (const auto* symbol = SymbolsSecondRow; *symbol; ++symbol) { const auto index = find(*symbol); if (index == none) return {}; second.push_back(index); }
            for (const auto* symbol = SymbolsThirdRow; *symbol; ++symbol) { const auto index = find(*symbol); if (index == none) return {}; third.push_back(index); }
            const double offset2 = (width - second.size() * pitch) / 2;
            for (size_t i = 0; i < second.size(); ++i) place(second[i], offset2 + i * pitch, offset2 + (i + 1) * pitch, rowTop(1), rowHeight(1));
            const double each = (width - 2 * wing) / static_cast<double>(third.size());
            for (size_t i = 0; i < third.size(); ++i) place(third[i], wing + i * each, wing + (i + 1) * each, rowTop(2), rowHeight(2));
        }
        // Shift (or the next-symbols key) and delete, with a wider inner gap.
        place(shift, 0, wing, rowTop(2), rowHeight(2));
        place(erase, width - wing, width, rowTop(2), rowHeight(2));
        const double outer = KeyboardGap * pitch / 2, inner = (std::max)(outer, wing - CompactSideVisual * pitch - outer);
        rects[shift].insetRight = inner; rects[erase].insetLeft = inner;
        // Bottom: &123 (abc), language, Ctrl | space | microphone, return.
        std::vector<size_t> left;
        if (keys[bottom.front()].left < keys[parsed.space].left && keys[bottom.front()].role == KeyboardKeyRole::None) left.push_back(bottom.front());
        for (const auto role : {KeyboardKeyRole::Language, KeyboardKeyRole::Control})
            if (const auto index = Find(keys, bottom, role); index != none) left.push_back(index);
        const size_t microphone = Find(keys, bottom, KeyboardKeyRole::Microphone);
        double at = 0;
        for (const auto index : left) { place(index, at, at + CompactSmallKey * pitch, bottomTop, bottomHeight); at += CompactSmallKey * pitch; }
        double end = width - CompactReturn * pitch;
        place(enter, end, width, bottomTop, bottomHeight);
        if (microphone != none) { place(microphone, end - CompactSmallKey * pitch, end, bottomTop, bottomHeight); end -= CompactSmallKey * pitch; }
        if (end - at < 3 * pitch) return {};
        place(parsed.space, at, end, bottomTop, bottomHeight);
    } else {
        pitch = width / (static_cast<double>(n1) + FullExtra);
        // Ctrl fills the first letter row with nothing on its left.
        size_t spare = Find(keys, bottom, KeyboardKeyRole::Control);
        auto lead1 = parsed.letters[0].lead, lead2 = parsed.letters[1].lead;
        for (auto* lead : {&lead1, &lead2})
            if (lead->empty() && spare != none) { lead->push_back(spare); spare = none; }
        const auto share = [&](const std::vector<size_t>& group, double from, double to, double top, double height) {
            double total = 0;
            for (const auto index : group) total += keys[index].width;
            double at = from;
            for (const auto index : group) {
                const double next = at + (to - from) * keys[index].width / total;
                place(index, at, next, top, height);
                at = next;
            }
        };
        // Each row: its left end, the characters, and the right end taking the rest.
        const auto row = [&](size_t r, const std::vector<size_t>& lead, const std::vector<size_t>& chars, const std::vector<size_t>& trail, double start) -> bool {
            double rest = width - start - chars.size() * pitch;
            if (rest < FullMinimumSide * pitch) { start = width - chars.size() * pitch - FullMinimumSide * pitch; rest = FullMinimumSide * pitch; }
            if (start < pitch) return false;
            share(lead, 0, start, rowTop(r), rowHeight(r));
            for (size_t i = 0; i < chars.size(); ++i) place(chars[i], start + i * pitch, start + (i + 1) * pitch, rowTop(r), rowHeight(r));
            share(trail, width - rest, width, rowTop(r), rowHeight(r));
            return true;
        };
        if (!row(0, lead1, parsed.letters[0].chars, parsed.letters[0].trail, FullTab * pitch)) return {};
        std::vector<size_t> letters2, marks2;
        split(parsed.letters[1].chars, letters2, marks2);
        if (letters2.empty() || !row(1, lead2, letters2, parsed.letters[1].trail, FullCaps * pitch)) return {};
        if (!parsed.letters[1].trail.empty()) result.enter = parsed.letters[1].trail.front();
        // Row 3 keeps its letters plus the first marks (",", ".") up to one less than row 1.
        std::vector<size_t> chars3;
        size_t letterCount = 0;
        for (const auto index : parsed.letters[2].chars) letterCount += keys[index].letter ? 1 : 0;
        size_t marks = n1 - 1 > letterCount ? n1 - 1 - letterCount : 0;
        for (const auto index : parsed.letters[2].chars)
            if (keys[index].letter || (marks && marks--)) chars3.push_back(index);
        if (!row(2, parsed.letters[2].lead, chars3, parsed.letters[2].trail, FullShift * pitch)) return {};
        // Bottom: language, &123, microphone (and Ctrl if unused) | space | the rest.
        std::vector<size_t> left, right;
        if (const auto language = Find(keys, bottom, KeyboardKeyRole::Language); language != none) left.push_back(language);
        for (const auto index : bottom)
            if (index != parsed.space && !placed[index] && keys[index].role == KeyboardKeyRole::None && keys[index].left < keys[parsed.space].left) left.push_back(index);
        if (const auto microphone = Find(keys, bottom, KeyboardKeyRole::Microphone); microphone != none && !placed[microphone]) left.push_back(microphone);
        if (spare != none) left.push_back(spare);
        for (const auto index : bottom)
            if (index != parsed.space && !placed[index] && std::find(left.begin(), left.end(), index) == left.end()) right.push_back(index);
        double at = 0;
        for (const auto index : left) { place(index, at, at + FullLeftKey * pitch, bottomTop, bottomHeight); at += FullLeftKey * pitch; }
        double end = width;
        for (auto it = right.rbegin(); it != right.rend(); ++it) { place(*it, end - FullRightKey * pitch, end, bottomTop, bottomHeight); end -= FullRightKey * pitch; }
        if (end - at < 3 * pitch) return {};
        place(parsed.space, at, end, bottomTop, bottomHeight);
    }

    // Padding: the rows shrink to leave room above the first row (under the
    // suggestion bar) and under the bottom one (clear of rounded screen
    // corners), and every key moves in by the side padding. The outer keys'
    // slots then stretch back to the keyboard's edges, so touches anywhere
    // still land on keys; only their visible shapes move in.
    const double pixel = 1 / scale;
    const auto snap = [&](double value) { return std::round(value * scale) / scale; };
    const double first = rowTop(0), edge = bottomTop + bottomHeight;
    const double above = snap(KeyboardTopPadding * rowSize), under = snap(KeyboardBottomPadding * rowSize);
    const double factor = (edge - first - above - under) / (edge - first);
    if (!(factor > .5)) return {};
    const double vertical = (std::max)(pixel, snap(KeyboardRowGap * rowSize * factor / 2));
    for (size_t i = 0; i < rects.size(); ++i) {
        auto& rect = rects[i];
        rect.visible = placed[i];
        if (!rect.visible) continue;
        const double l = snap(rect.left + side), r = snap(rect.left + rect.width + side);
        rect.left = l;
        rect.width = r - l;
        rect.insetLeft = (std::max)(pixel, snap(rect.insetLeft));
        rect.insetRight = (std::max)(pixel, snap(rect.insetRight));
        const bool top = std::abs(rect.top - first) < .5, last = std::abs(rect.top - bottomTop) < .5;
        const double upper = snap(first + above + (rect.top - first) * factor), lower = snap(first + above + (rect.top + rect.height - first) * factor);
        rect.top = top ? first : upper;
        rect.height = (last ? edge : lower) - rect.top;
        rect.insetTop = vertical + (top ? upper - first : 0);
        rect.insetBottom = vertical + (last ? edge - lower : 0);
    }
    // Each row's outermost keys reach the keyboard's sides again.
    std::vector<std::pair<double, std::pair<double, double>>> rows;  // top -> (min left, max right)
    for (const auto& rect : rects) {
        if (!rect.visible) continue;
        auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return std::abs(row.first - rect.top) < .5; });
        if (it == rows.end()) rows.push_back({rect.top, {rect.left, rect.left + rect.width}});
        else { it->second.first = (std::min)(it->second.first, rect.left); it->second.second = (std::max)(it->second.second, rect.left + rect.width); }
    }
    for (auto& rect : rects) {
        if (!rect.visible) continue;
        const auto& bounds = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return std::abs(row.first - rect.top) < .5; })->second;
        if (std::abs(rect.left - bounds.first) < 1e-6) { rect.insetLeft += rect.left; rect.width += rect.left; rect.left = 0; }
        if (std::abs(rect.left + rect.width - bounds.second) < 1e-6) { rect.insetRight += fullWidth - (rect.left + rect.width); rect.width = fullWidth - rect.left; }
    }
    // Corners: 24% of a key's visible height on the compact layout (about 11 DIP,
    // like iPadOS 26's rounder keys), a little tighter on the full one.
    result.radius = std::round((rowSize * factor - 2 * vertical) * (result.compact ? .24 : .2));
    result.edge = side + (std::max)(pixel, snap(KeyboardGap * pitch / 2));
    result.keys = std::move(rects);
    return result;
}
}
