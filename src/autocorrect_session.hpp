// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// One text field's typing session: turns the touch keyboard's keystrokes
// into iOS-style autocorrect. It keeps a model of the text before the caret,
// decides corrections when a word is committed, and says which keystrokes
// it takes over and what to send instead. Pure: the caller hooks the
// keystrokes and injects the edits.
//
// iOS rules kept: space, punctuation and return apply the highlighted
// correction; the bar shows the typed word in quotes, the correction in the
// middle and an alternative; a tapped suggestion brings a space that a
// following punctuation mark replaces; after a correction the bar offers
// ↶ and the original until the next word, and backspacing into the word
// offers it again. Deliberately stronger than iOS: when the next word
// arrives, the previous one is reconsidered with both neighbours ("if like"
// becomes "I'd like"); deleting a correction and retyping the same letters
// keeps them and counts as a rejection.
#include "autocorrect.hpp"

namespace acrylic::autocorrect {
// Backspaces, then text: what to inject in place of a taken keystroke.
struct Edit { size_t erase = 0; std::wstring insert; };
enum class SlotKind { None, Literal, Correction, Suggestion, Revert };
struct Slot { std::wstring text; SlotKind kind = SlotKind::None; };
struct Bar { Slot slots[3]; size_t primary = 1; bool correcting = false; };

class Session {
public:
    explicit Session(Engine& engine) : engine_(engine) {}

    // A new field or caret position. `before` is the text before the caret
    // when known (UI Automation); `known` says whether it starts the field.
    void Reset(std::wstring_view before = {}, bool known = false) {
        Flush();
        text_ = before.size() > Window ? std::wstring(before.substr(before.size() - Window)) : std::wstring(before);
        reliable_ = known && before.size() <= Window;
        last_ = {};
        correction_ = {};
        deleted_ = {};
        autoSpace_ = std::wstring::npos;
    }
    // Learn whatever is still pending (focus lost, keyboard closed).
    void Flush() {
        if (last_.active) Learn(last_);
        last_ = {};
    }
    const std::wstring& Text() const noexcept { return text_; }

    // A character the keyboard is delivering. Returns true when the session
    // takes it: the caller drops the keystroke and injects `edit` instead.
    bool Type(wchar_t c, Edit& edit) {
        edit = {};
        const bool word = IsLetter(c) || ((IsApostrophe(c) || c == L'-') && CurrentStart() < text_.size());
        if (word) {
            Append(std::wstring(1, c));
            autoSpace_ = std::wstring::npos;
            return false;
        }
        // A tapped suggestion's space gives way to punctuation: "word." not "word .".
        if (autoSpace_ != std::wstring::npos && autoSpace_ + 1 == text_.size() && std::wcschr(L".,!?;:", c)) {
            text_.pop_back();
            edit.erase = 1;
            edit.insert = std::wstring(1, c);
            Append(edit.insert);
            autoSpace_ = std::wstring::npos;
            return true;
        }
        autoSpace_ = std::wstring::npos;
        const wchar_t typed = c == L'\r' ? L'\n' : c;
        // Digits and symbols (@ / _ # ...) don't end a word: they glue it
        // into a token (an address, a file name) that is never corrected.
        const bool taken = Trigger(typed) && Commit(typed, edit);
        if (!taken) Append(std::wstring(1, typed));
        return taken;
    }
    // Keys that end a word and apply its correction, as on iOS.
    static bool Trigger(wchar_t c) { return c && std::wcschr(L" \t\n.,!?;:)]}\"»”", c); }
    void Backspace() {
        autoSpace_ = std::wstring::npos;
        if (text_.empty()) { reliable_ = false; return; }
        text_.pop_back();
        // Editing back into the last word: it's the user's now.
        if (last_.active && text_.size() < last_.start + last_.shown.size()) {
            if (correction_.active && correction_.start == last_.start && text_.size() < correction_.start + correction_.replaced.size())
                deleted_ = correction_;  // a correction being deleted: retyping it is a rejection
            last_.active = false;
        }
        if (correction_.active && text_.size() < correction_.start) correction_.active = false;
    }

    // The QuickType bar for the current state.
    Bar Suggestions() const {
        Bar bar;
        const size_t start = CurrentStart();
        const std::wstring current = text_.substr(start);
        const auto context = Context(start);
        const bool revert = RevertOffered();
        if (current.empty()) {
            const auto next = engine_.Complete(L"", context.id, context.text, context.sentenceStart, 3);
            size_t order[3] = {1, 0, 2};
            if (revert) {
                bar.slots[0] = {correction_.original, SlotKind::Revert};
                if (next.size() > 0) bar.slots[1] = {next[0], SlotKind::Suggestion};
                if (next.size() > 1) bar.slots[2] = {next[1], SlotKind::Suggestion};
            } else
                for (size_t i = 0; i < next.size() && i < 3; ++i) bar.slots[order[i]] = {next[i], SlotKind::Suggestion};
            return bar;
        }
        const auto choice = engine_.Correct(current, context.id, context.text, context.sentenceStart);
        const bool pending = !choice.literal && !Retyped(current);
        const auto more = engine_.Complete(current, context.id, context.text, context.sentenceStart, 4);
        bar.slots[0] = revert ? Slot{correction_.original, SlotKind::Revert} : Slot{current, SlotKind::Literal};
        size_t next = 1;
        if (pending) { bar.slots[1] = {choice.text, SlotKind::Correction}; bar.correcting = true; next = 2; }
        for (const auto& item : more) {
            if (next > 2) break;
            if (item == current || (pending && item == choice.text)) continue;
            bar.slots[next++] = {item, SlotKind::Suggestion};
        }
        return bar;
    }
    // A tap on a bar slot.
    bool Pick(const Bar& bar, size_t index, Edit& edit) {
        edit = {};
        if (index >= 3 || bar.slots[index].kind == SlotKind::None) return false;
        const auto& slot = bar.slots[index];
        const size_t start = CurrentStart();
        const std::wstring current = text_.substr(start);
        if (slot.kind == SlotKind::Revert) {
            if (!RevertOffered()) return false;
            // ↶: put back exactly what was typed and remember the refusal.
            const std::wstring tail = text_.substr(correction_.start + correction_.replaced.size());
            edit = Replace(correction_.start, correction_.original + tail);
            Refuse(correction_.original, correction_.replaced, 1);
            Teach(correction_.original, 2);
            if (last_.active && last_.start == correction_.start) { last_.shown = correction_.original; last_.locked = true; last_.learned = true; }
            correction_.active = false;
            return true;
        }
        if (slot.kind == SlotKind::Literal) {
            // The quoted word: keep it, don't correct it, count the refusal.
            const auto context = Context(start);
            const auto choice = engine_.Correct(current, context.id, context.text, context.sentenceStart);
            if (!choice.literal) Refuse(current, choice.text, .5);
            Teach(current, 1);
            Remember(start, current, current, {}, 0, true, context);
            last_.learned = true;
            edit.insert = L" ";
            Append(edit.insert);
            autoSpace_ = text_.size() - 1;
            return true;
        }
        // A correction, completion or prediction: replace the partial word.
        const auto context = Context(start);
        edit = Replace(start, slot.text + L" ");
        Remember(start, current, slot.text, {}, 0, true, context);
        autoSpace_ = text_.size() - 1;
        return true;
    }

private:
    static constexpr size_t Window = 512;
    struct Context_ { uint32_t id = NoWord; std::wstring text; bool sentenceStart = false; };
    // The last committed word (or two-word split): its span, its readings,
    // and the context it was decided in, kept until the next word arrives.
    struct Committed {
        bool active = false, locked = false, learned = false;
        size_t start = 0;
        std::wstring typed, shown;
        std::vector<Option> options;
        size_t chosen = 0;
        Context_ context;
    };
    struct Correction { bool active = false; size_t start = 0; std::wstring original, replaced; };

    Engine& engine_;
    std::wstring text_;
    bool reliable_ = false;
    Committed last_;
    Correction correction_, deleted_;
    size_t autoSpace_ = std::wstring::npos;

    void Append(const std::wstring& value) {
        text_ += value;
        if (text_.size() > Window * 2) {
            const size_t drop = text_.size() - Window;
            text_.erase(0, drop);
            reliable_ = false;
            const auto shift = [&](size_t& position, bool& active) { if (position < drop) active = false; else position -= drop; };
            shift(last_.start, last_.active);
            shift(correction_.start, correction_.active);
            shift(deleted_.start, deleted_.active);
            if (autoSpace_ != std::wstring::npos) autoSpace_ = autoSpace_ >= drop ? autoSpace_ - drop : std::wstring::npos;
        }
    }
    size_t CurrentStart() const {
        size_t start = text_.size();
        while (start && IsWordCharacter(text_[start - 1])) --start;
        // Leading apostrophes and hyphens are punctuation, not the word.
        while (start < text_.size() && !IsLetter(text_[start])) ++start;
        return start;
    }
    // The word before position `start` (separated by one space), and
    // whether `start` begins a sentence.
    bool SentenceAt(size_t start) const {
        size_t at = start;
        while (at && (text_[at - 1] == L' ' || text_[at - 1] == L'\t')) --at;
        return at ? std::wcschr(L".!?\n", text_[at - 1]) != nullptr : reliable_;
    }
    Context_ Context(size_t start) const {
        Context_ context;
        if (SentenceAt(start)) { context.sentenceStart = true; context.id = SentenceStart; return context; }
        size_t at = start;
        while (at && (text_[at - 1] == L' ' || text_[at - 1] == L'\t')) --at;
        if (!at || start - at != 1 || !IsWordCharacter(text_[at - 1])) return context;
        size_t begin = at;
        while (begin && IsWordCharacter(text_[begin - 1])) --begin;
        context.text = text_.substr(begin, at - begin);
        context.id = engine_.Lookup(context.text, SentenceAt(begin));
        if (context.id != NoWord) context.text = std::wstring(engine_.Words().Text(context.id));
        return context;
    }
    static std::vector<std::wstring> Words(const std::wstring& text) {
        std::vector<std::wstring> out;
        std::wstring current;
        for (const wchar_t c : text) {
            if (IsWordCharacter(c)) current += c;
            else if (!current.empty()) { out.push_back(current); current.clear(); }
        }
        if (!current.empty()) out.push_back(current);
        return out;
    }
    bool RevertOffered() const {
        if (!correction_.active) return false;
        const size_t end = correction_.start + correction_.replaced.size();
        if (text_.compare(correction_.start, correction_.replaced.size(), correction_.replaced)) return false;
        // Right after the correction (its trigger typed), or backspaced to its end.
        return text_.size() == end || (text_.size() == end + 1 && !IsWordCharacter(text_.back()));
    }
    // The user deleted this correction and typed the same letters again.
    bool Retyped(const std::wstring& current) const { return deleted_.active && Fold(current) == Fold(deleted_.original); }
    // Refusing a correction: each changed word, or the whole split.
    void Refuse(const std::wstring& original, const std::wstring& replaced, double amount) {
        const auto before = Words(original), after = Words(replaced);
        if (before.size() == after.size()) {
            for (size_t i = 0; i < before.size(); ++i)
                if (Fold(before[i]) != Fold(after[i]) || before[i] != after[i]) engine_.user.Reject(Fold(before[i]), engine_.Canonical(after[i]), amount, engine_.today);
        } else engine_.user.Reject(Fold(original), replaced, amount, engine_.today);
    }
    void Teach(const std::wstring& text, double amount) {
        for (const auto& word : Words(text)) {
            engine_.user.Add(engine_.user.words, engine_.Canonical(word), amount, engine_.today);
            engine_.user.total += amount;
        }
    }
    Edit Replace(size_t start, const std::wstring& replacement) {
        const std::wstring old = text_.substr(start);
        size_t same = 0;
        while (same < old.size() && same < replacement.size() && old[same] == replacement[same]) ++same;
        Edit edit;
        edit.erase = old.size() - same;
        edit.insert = replacement.substr(same);
        text_.resize(start);
        Append(replacement);
        return edit;
    }
    void Learn(Committed& word) {
        if (word.learned || word.shown.empty()) return;
        word.learned = true;
        std::wstring previous = word.context.text;
        for (const auto& item : Words(word.shown)) { const auto canonical = engine_.Canonical(item); engine_.user.Commit(previous, canonical, engine_.today); previous = canonical; }
    }
    void Remember(size_t start, const std::wstring& typed, const std::wstring& shown, std::vector<Option> options, size_t chosen, bool locked, const Context_& context) {
        if (last_.active) Learn(last_);
        last_ = {};
        last_.active = true;
        last_.locked = locked;
        last_.start = start;
        last_.typed = typed;
        last_.shown = shown;
        last_.options = std::move(options);
        last_.chosen = chosen;
        last_.context = context;
    }
    float Total(const Context_& context, const Option& option) const { return option.edit + engine_.Cost(context.id, context.text, option); }

    // The word before the trigger is done: decide it, and reconsider the
    // word before it now that its right-hand neighbour is known.
    bool Commit(wchar_t trigger, Edit& edit) {
        const size_t start = CurrentStart();
        if (start == text_.size()) return false;
        const std::wstring typed = text_.substr(start);
        const auto context = Context(start);
        auto options = engine_.Options(typed, context.sentenceStart);
        // Glued to a digit or symbol (user@mail, example.com, v2beta): an
        // address or a name, kept as typed. Opening brackets and quotes are fine.
        if (start && !std::wcschr(L" \t\n([{\"'«“‘", text_[start - 1])) options.resize(1);
        if (Retyped(typed)) {
            // Deleted and retyped: keep it this time and remember the refusal.
            Refuse(typed, deleted_.replaced, .5);
            deleted_.active = false;
            options.resize(1);
        }
        const auto& params = engine_.params;
        const bool ending = std::wcschr(L".!?", trigger) != nullptr;
        const float keep = options[0].known ? params.revision : 0;  // a real word stays unless clearly wrong
        // Can the previous word still change? Only right behind this one.
        const bool revisable = last_.active && !last_.locked && last_.options.size() > 1 && start == last_.start + last_.shown.size() + 1
            && text_[start - 1] == L' ' && !text_.compare(last_.start, last_.shown.size(), last_.shown);
        size_t bestPrevious = last_.chosen, bestCurrent = 0;
        float best = 0;
        bool first = true;
        const size_t previousCount = revisable ? last_.options.size() : 1;
        for (size_t p = 0; p < previousCount; ++p) {
            const Option* previous = revisable ? &last_.options[p] : nullptr;
            Context_ next = context;
            float base = 0;
            if (previous) {
                base = Total(last_.context, *previous) + (p == last_.chosen ? 0 : params.revision);
                next.id = previous->last;
                next.text = previous->lastText;
            }
            for (size_t c = 0; c < options.size(); ++c) {
                float total = base + Total(next, options[c]) + (c == 0 ? -keep : 0);
                // A full stop, ! or ? ends the sentence: some words can end one, some can't.
                if (ending && options[c].last != NoWord) total += engine_.Words().End(options[c].last);
                if (first || total < best) { best = total; bestPrevious = p; bestCurrent = c; first = false; }
            }
        }
        const Option& chosen = options[bestCurrent];
        const bool revise = revisable && bestPrevious != last_.chosen;
        const std::wstring terminator(1, trigger);
        if (revise) {
            const auto& previous = last_.options[bestPrevious];
            const size_t from = last_.start;
            const std::wstring original = text_.substr(from, text_.size() - from);
            const std::wstring replacement = previous.text + L" " + chosen.text;
            const auto previousContext = last_.context;
            last_.learned = true;  // learned below with its final reading
            edit = Replace(from, replacement + terminator);
            text_.pop_back();
            correction_ = {true, from, original, replacement};
            std::wstring before = previousContext.text;
            for (const auto& item : Words(previous.text)) { const auto canonical = engine_.Canonical(item); engine_.user.Commit(before, canonical, engine_.today); before = canonical; }
            Context_ mine = context;
            mine.id = previous.last; mine.text = previous.lastText;
            Remember(from + previous.text.size() + 1, typed, chosen.text, std::move(options), bestCurrent, false, mine);
            text_ += terminator;
            return true;
        }
        if (bestCurrent == 0) {
            Remember(start, typed, typed, std::move(options), 0, false, context);
            return false;
        }
        const std::wstring replacement = chosen.text;
        edit = Replace(start, replacement + terminator);
        text_.pop_back();
        correction_ = {true, start, typed, replacement};
        Remember(start, typed, replacement, std::move(options), bestCurrent, false, context);
        text_ += terminator;
        return true;
    }
};
}
