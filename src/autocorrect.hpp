// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The touch keyboard's autocorrect engine: a noisy-channel decoder in the
// style of AOSP LatinIME. Every candidate word costs bits: how unlikely the
// typed letters are if the user meant that word (key distance, missed,
// extra and swapped letters, apostrophes, accents, a letter hit instead of
// the space bar), plus how unlikely the word is after the previous one
// (bigrams with Witten-Bell backoff, mixed with what this user types).
// Pure: no shell, no UI. Dictionaries come from tools/Build-Dictionary.py.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace acrylic::autocorrect {
inline constexpr uint32_t NoWord = 0xFFFFFFFFu;
inline constexpr uint32_t SentenceStart = 0xFFFFFFFEu;
enum class Language { English, French };

inline bool IsApostrophe(wchar_t c) { return c == L'\'' || c == 0x2019 || c == 0x02BC; }
inline bool IsLetter(wchar_t c) {
    if (c < 0x80) return (c | 0x20) >= L'a' && (c | 0x20) <= L'z';
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_ALPHA);
}
inline bool IsUpper(wchar_t c) {
    if (c < 0x80) return c >= L'A' && c <= L'Z';
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_UPPER);
}
// Letters, and the apostrophes and hyphens inside words (don't, l'homme, well-known).
inline bool IsWordCharacter(wchar_t c) { return IsLetter(c) || IsApostrophe(c) || c == L'-'; }
inline wchar_t Lower(wchar_t c) {
    if (c < 0x80) return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c;
    wchar_t out = c;
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, &c, 1, &out, 1, nullptr, nullptr, 0);
    return out;
}
inline wchar_t Upper(wchar_t c) {
    if (c < 0x80) return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - 32) : c;
    wchar_t out = c;
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, &c, 1, &out, 1, nullptr, nullptr, 0);
    return out;
}
// Base letters for the Latin-1 and French letters; others stay as they are.
inline const wchar_t* BaseLetters(wchar_t c) {
    static const wchar_t* const latin1[64] = {
        L"a", L"a", L"a", L"a", L"a", L"a", L"ae", L"c", L"e", L"e", L"e", L"e", L"i", L"i", L"i", L"i",
        L"d", L"n", L"o", L"o", L"o", L"o", L"o", nullptr, L"o", L"u", L"u", L"u", L"u", L"y", L"th", L"ss",
        L"a", L"a", L"a", L"a", L"a", L"a", L"ae", L"c", L"e", L"e", L"e", L"e", L"i", L"i", L"i", L"i",
        L"d", L"n", L"o", L"o", L"o", L"o", L"o", nullptr, L"o", L"u", L"u", L"u", L"u", L"y", L"th", L"y"};
    if (c >= 0xC0 && c <= 0xFF) return latin1[c - 0xC0];
    if (c == 0x152 || c == 0x153) return L"oe";
    if (c == 0x178) return L"y";
    return nullptr;
}
// The search key: lowercase base letters, no apostrophes. "Don't" and
// "dont" share a key; so do "déjà" and "deja", "cœur" and "coeur".
inline std::wstring Fold(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        if (IsApostrophe(c)) continue;
        const wchar_t lower = Lower(c);
        if (const auto* base = BaseLetters(lower)) out += base;
        else out += lower;
    }
    return out;
}
inline bool Accented(wchar_t c) { const auto* base = BaseLetters(Lower(c)); return base && !(base[0] == Lower(c) && !base[1]); }

inline std::wstring FromUtf8(const char* data, size_t size) {
    std::wstring out;
    if (!size) return out;
    const int length = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), nullptr, 0);
    out.resize(static_cast<size_t>(length));
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), out.data(), length);
    return out;
}
inline std::string ToUtf8(std::wstring_view text) {
    std::string out;
    if (text.empty()) return out;
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    out.resize(static_cast<size_t>(length));
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

// Key centers in key widths. Stock Windows touch keyboard rows start 0,
// 0.5 and 1 key in; the space bar sits under the bottom row's middle.
struct Layout {
    std::unordered_map<wchar_t, std::pair<float, float>> keys;
    float spaceLeft = 2.5f, spaceRight = 7.5f, spaceRow = 3;
    static Layout Rows(std::initializer_list<const wchar_t*> rows) {
        Layout layout;
        float y = 0;
        for (const auto* row : rows) {
            float x = y * .5f;
            for (const wchar_t* c = row; *c; ++c, ++x) layout.keys[*c] = {x, y};
            ++y;
        }
        return layout;
    }
    static Layout For(Language language) {
        return language == Language::French ? Rows({L"azertyuiop", L"qsdfghjklm", L"wxcvbn"}) : Rows({L"qwertyuiop", L"asdfghjkl", L"zxcvbnm"});
    }
    // Squared distance between two keys, or a large value for keys not on the layout.
    float Distance2(wchar_t a, wchar_t b) const {
        const auto first = keys.find(a), second = keys.find(b);
        if (first == keys.end() || second == keys.end()) return 100;
        const float dx = first->second.first - second->second.first, dy = first->second.second - second->second.second;
        return dx * dx + dy * dy;
    }
    // A bottom-row letter whose key touches the space bar (c v b n m on QWERTY).
    bool NearSpace(wchar_t c) const {
        const auto key = keys.find(c);
        return key != keys.end() && key->second.second == spaceRow - 1 && key->second.first + .5f > spaceLeft && key->second.first - .5f < spaceRight;
    }
};

// Costs in bits (-log2 probability). Tuned by tests/Autocorrect.cpp's
// --evaluate run on held-out sentences with simulated touch noise.
struct Params {
    float neighbor = 3.5f;            // adjacent key, scaled by squared distance
    float substitution = 10.f;        // any other wrong letter
    float omission = 7.f, doubledOmission = 4.f;   // a letter the user skipped
    float insertion = 7.f, repeatedInsertion = 4.5f, neighborInsertion = 5.5f;
    float transposition = 6.5f;
    float apostrophe = 1.f;           // "dont" for "don't"
    float extraApostrophe = 4.f;
    float accent = .5f;               // "deja" for "déjà"
    float lostAccent = 6.f;           // the user typed an accent the word lacks
    float nameCase = 3.f;             // "will" for "Will"
    float spaceSlip = 5.f;            // c v b n m hit instead of the space bar
    float missingSpace = 6.5f;
    float confusable = 3.f;           // their/there, its/it's, your/you're
    float whitelisted = 1.f;          // AOSP's known slips: "nave" for "have"
    float unknownWord = 12.f;         // that a word nobody knows is meant, not a slip
    float unknownUnigram = 20.f;      // how rare an unseen word is in context
    float revision = 3.f;             // margin before a typed real word is changed
    float validEdit = 6.f;            // the largest slip a typed real word may be read through
    float userWeight = .2f;           // share of the personal model
};

class Dictionary {
public:
    struct Node { uint32_t children = 0, words = 0; uint16_t childCount = 0, wordCount = 0; wchar_t ch = 0; uint8_t best = 255; };
    struct Follower { uint32_t word; uint8_t cost; };
    enum Flags : uint8_t { OffensiveFlag = 1, NameFlag = 2 };

    bool Load(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        size_t at = 0;
        const auto need = [&](size_t count) { return at + count <= size; };
        const auto u32 = [&] { uint32_t value = 0; std::memcpy(&value, bytes + at, 4); at += 4; return value; };
        const auto varint = [&](uint32_t& value) {
            value = 0;
            for (int shift = 0; shift < 35; shift += 7) {
                if (!need(1)) return false;
                const uint8_t byte = bytes[at++];
                value |= static_cast<uint32_t>(byte & 0x7F) << shift;
                if (!(byte & 0x80)) return true;
            }
            return false;
        };
        if (!need(20) || std::memcmp(bytes, "JFD1", 4)) return false;
        at = 4;
        if (u32() != 1) return false;
        const uint32_t count = u32(), whitelist = u32(), lists = u32();
        words_.clear(); pool_.clear(); whitelist_.clear(); followers_.clear();
        words_.reserve(count);
        pool_.reserve(static_cast<size_t>(count) * 16);
        for (uint32_t i = 0; i < count; ++i) {
            if (!need(3)) return false;
            Word word;
            word.cost = bytes[at]; word.flags = bytes[at + 1];
            const uint8_t length = bytes[at + 2];
            at += 3;
            if (!need(length)) return false;
            const auto text = FromUtf8(reinterpret_cast<const char*>(bytes + at), length);
            at += length;
            const auto key = Fold(text);
            if (text.empty() || text.size() > 255 || key.empty() || key.size() > 255) return false;
            word.text = static_cast<uint32_t>(pool_.size()); word.textLength = static_cast<uint8_t>(text.size());
            pool_ += text;
            word.key = static_cast<uint32_t>(pool_.size()); word.keyLength = static_cast<uint8_t>(key.size());
            pool_ += key;
            if (IsUpper(text[0]) && !(text[0] == L'I' && (text.size() == 1 || IsApostrophe(text[1])))) word.flags |= NameFlag;
            words_.push_back(word);
        }
        for (uint32_t i = 0; i < whitelist; ++i) {
            if (!need(1)) return false;
            const uint8_t length = bytes[at++];
            if (!need(length)) return false;
            const auto typed = FromUtf8(reinterpret_cast<const char*>(bytes + at), length);
            at += length;
            uint32_t target = 0;
            if (!varint(target) || target >= count) return false;
            whitelist_[typed] = target;
        }
        listStart_.assign(static_cast<size_t>(count) + 2, 0);
        backoff_.assign(static_cast<size_t>(count) + 1, 0);
        std::vector<std::pair<uint32_t, uint32_t>> ranges(static_cast<size_t>(count) + 1, {0, 0});
        for (uint32_t i = 0; i < lists; ++i) {
            uint32_t first = 0, items = 0;
            if (!varint(first) || first > count || !need(1)) return false;
            const uint8_t backoff = bytes[at++];
            if (!varint(items)) return false;
            const auto begin = static_cast<uint32_t>(followers_.size());
            uint32_t word = 0;
            for (uint32_t j = 0; j < items; ++j) {
                uint32_t delta = 0;
                if (!varint(delta) || !need(1)) return false;
                word += delta;
                if (word >= count) return false;
                followers_.push_back({word, bytes[at++]});
            }
            // Slot 0 is the sentence start; word i is slot i + 1.
            ranges[first] = {begin, static_cast<uint32_t>(followers_.size())};
            backoff_[first] = backoff;
        }
        listRanges_ = std::move(ranges);
        if (!need(5)) return false;
        const uint8_t endDefault = bytes[at++];
        const uint32_t ends = u32();
        end_.assign(count, endDefault);
        for (uint32_t i = 0, word = 0; i < ends; ++i) {
            uint32_t delta = 0;
            if (!varint(delta) || !need(1)) return false;
            word += delta;
            if (word >= count) return false;
            end_[word] = bytes[at++];
        }
        BuildIndex();
        return true;
    }
    size_t Size() const noexcept { return words_.size(); }
    std::wstring_view Text(uint32_t id) const { const auto& w = words_[id]; return {pool_.data() + w.text, w.textLength}; }
    std::wstring_view Key(uint32_t id) const { const auto& w = words_[id]; return {pool_.data() + w.key, w.keyLength}; }
    float Unigram(uint32_t id) const { return words_[id].cost / 8.f; }
    bool Offensive(uint32_t id) const { return (words_[id].flags & OffensiveFlag) != 0; }
    bool IsName(uint32_t id) const { return (words_[id].flags & NameFlag) != 0; }
    uint32_t Find(std::wstring_view text) const {
        const auto found = std::lower_bound(byText_.begin(), byText_.end(), text, [&](uint32_t id, std::wstring_view value) { return Text(id) < value; });
        return found != byText_.end() && Text(*found) == text ? *found : NoWord;
    }
    // Words whose key is exactly `key`.
    std::pair<const uint32_t*, const uint32_t*> WithKey(std::wstring_view key) const {
        const auto node = Walk(key);
        if (node == NoWord) return {nullptr, nullptr};
        const auto& n = nodes_[node];
        return {nodeWords_.data() + n.words, nodeWords_.data() + n.words + n.wordCount};
    }
    uint32_t Walk(std::wstring_view key) const {
        uint32_t node = 0;
        for (const wchar_t c : key) {
            const auto& n = nodes_[node];
            const auto* begin = nodes_.data() + n.children;
            const auto* end = begin + n.childCount;
            const auto* child = std::lower_bound(begin, end, c, [](const Node& x, wchar_t value) { return x.ch < value; });
            if (child == end || child->ch != c) return NoWord;
            node = static_cast<uint32_t>(child - nodes_.data());
        }
        return node;
    }
    uint32_t Whitelist(std::wstring_view typed) const { const auto found = whitelist_.find(std::wstring(typed)); return found == whitelist_.end() ? NoWord : found->second; }
    // -log2 P(second | first); first may be SentenceStart or NoWord (no context).
    float Bigram(uint32_t first, uint32_t second) const {
        if (first == NoWord) return Unigram(second);
        const size_t slot = first == SentenceStart ? 0 : static_cast<size_t>(first) + 1;
        const auto range = listRanges_[slot];
        if (range.first == range.second) return Unigram(second);
        const auto* begin = followers_.data() + range.first;
        const auto* end = followers_.data() + range.second;
        const auto* found = std::lower_bound(begin, end, second, [](const Follower& f, uint32_t value) { return f.word < value; });
        if (found != end && found->word == second) return found->cost / 8.f;
        return backoff_[slot] / 8.f + Unigram(second);
    }
    // -log2 P(the sentence ends | word): "too." is likely, "to." is not.
    float End(uint32_t word) const { return word < end_.size() ? end_[word] / 8.f : 3.f; }
    // -log2 of the share left for followers never seen after `first`.
    float Backoff(uint32_t first) const {
        if (first == NoWord) return 0;
        const size_t slot = first == SentenceStart ? 0 : static_cast<size_t>(first) + 1;
        return listRanges_[slot].first == listRanges_[slot].second ? 0 : backoff_[slot] / 8.f;
    }
    std::pair<const Follower*, const Follower*> Followers(uint32_t first) const {
        if (first == NoWord) return {nullptr, nullptr};
        const auto range = listRanges_[first == SentenceStart ? 0 : static_cast<size_t>(first) + 1];
        return {followers_.data() + range.first, followers_.data() + range.second};
    }
    const std::vector<Node>& Nodes() const noexcept { return nodes_; }
    const std::vector<uint32_t>& NodeWords() const noexcept { return nodeWords_; }

private:
    struct Word { uint32_t text = 0, key = 0; uint8_t textLength = 0, keyLength = 0, cost = 255, flags = 0; };
    std::vector<Word> words_;
    std::wstring pool_;
    std::unordered_map<std::wstring, uint32_t> whitelist_;
    std::vector<Follower> followers_;
    std::vector<std::pair<uint32_t, uint32_t>> listRanges_;
    std::vector<uint32_t> listStart_;
    std::vector<uint8_t> backoff_, end_;
    std::vector<uint32_t> byText_;
    std::vector<Node> nodes_;
    std::vector<uint32_t> nodeWords_;

    void BuildIndex() {
        byText_.resize(words_.size());
        for (uint32_t i = 0; i < words_.size(); ++i) byText_[i] = i;
        std::sort(byText_.begin(), byText_.end(), [&](uint32_t a, uint32_t b) { return Text(a) < Text(b); });
        std::vector<uint32_t> order(byText_);
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { const auto x = Key(a), y = Key(b); return x != y ? x < y : words_[a].cost < words_[b].cost; });
        nodes_.clear(); nodeWords_.clear();
        nodes_.reserve(words_.size() * 3);
        nodeWords_.reserve(words_.size());
        nodes_.emplace_back();
        Build(0, order, 0, order.size(), 0);
    }
    uint8_t Build(uint32_t node, const std::vector<uint32_t>& order, size_t low, size_t high, size_t depth) {
        size_t i = low;
        while (i < high && Key(order[i]).size() == depth) ++i;
        uint8_t best = 255;
        nodes_[node].words = static_cast<uint32_t>(nodeWords_.size());
        nodes_[node].wordCount = static_cast<uint16_t>((std::min)(i - low, size_t{0xFFFF}));
        for (size_t k = low; k < i && k - low < 0xFFFF; ++k) { nodeWords_.push_back(order[k]); best = (std::min)(best, words_[order[k]].cost); }
        std::vector<std::pair<size_t, size_t>> groups;
        for (size_t j = i; j < high;) {
            const wchar_t c = Key(order[j])[depth];
            size_t k = j;
            while (k < high && Key(order[k])[depth] == c) ++k;
            groups.push_back({j, k});
            j = k;
        }
        const auto first = static_cast<uint32_t>(nodes_.size());
        nodes_[node].children = first;
        nodes_[node].childCount = static_cast<uint16_t>(groups.size());
        nodes_.resize(nodes_.size() + groups.size());
        for (size_t g = 0; g < groups.size(); ++g) {
            nodes_[first + g].ch = Key(order[groups[g].first])[depth];
            best = (std::min)(best, Build(first + static_cast<uint32_t>(g), order, groups[g].first, groups[g].second, depth + 1));
        }
        nodes_[node].best = best;
        return best;
    }
};

// What this user types: word and pair counts, rejected corrections, and
// words the dictionary lacks. Counts only, never text in order; kept on
// this machine. Old entries fade: weight halves every 60 days unused.
class UserModel {
public:
    struct Entry { double count = 0; uint32_t day = 0; };
    std::unordered_map<std::wstring, Entry> words, pairs, rejections;
    double total = 0;
    bool changed = false;

    static double Faded(const Entry& entry, uint32_t today) { return entry.count * std::exp2(-static_cast<double>(today > entry.day ? today - entry.day : 0) / 60.0); }
    void Add(std::unordered_map<std::wstring, Entry>& table, const std::wstring& key, double amount, uint32_t today) {
        auto& entry = table[key];
        entry.count = Faded(entry, today) + amount;
        entry.day = today;
        changed = true;
    }
    void Commit(std::wstring_view previous, std::wstring_view word, uint32_t today) {
        Add(words, std::wstring(word), 1, today);
        total += 1;
        if (!previous.empty()) Add(pairs, std::wstring(previous) + L'\t' + std::wstring(word), 1, today);
    }
    double Count(std::wstring_view word, uint32_t today) const { const auto found = words.find(std::wstring(word)); return found == words.end() ? 0 : Faded(found->second, today); }
    // A word the user keeps typing counts as known from its second use.
    bool Known(std::wstring_view word, uint32_t today) const { return Count(word, today) >= 1.99; }
    void Reject(std::wstring_view typedKey, std::wstring_view correction, double amount, uint32_t today) { Add(rejections, std::wstring(typedKey) + L'\t' + std::wstring(correction), amount, today); }
    bool Rejected(std::wstring_view typedKey, std::wstring_view correction, uint32_t today) const {
        const auto found = rejections.find(std::wstring(typedKey) + L'\t' + std::wstring(correction));
        return found != rejections.end() && Faded(found->second, today) >= .99;
    }
    // Personal probability of word after previous (0 when unknown).
    double Probability(std::wstring_view previous, std::wstring_view word, uint32_t today) const {
        if (total < 1) return 0;
        const double unigram = Count(word, today) / (total + 50);
        if (previous.empty()) return unigram;
        const auto pair = pairs.find(std::wstring(previous) + L'\t' + std::wstring(word));
        const double after = Count(previous, today);
        if (pair == pairs.end() || after < 1) return .3 * unigram;
        return .7 * Faded(pair->second, today) / (after + 1) + .3 * unigram;
    }
    std::string Serialize(uint32_t today) const {
        std::string out = "jellyflower keyboard 1\n";
        const auto write = [&](char kind, const std::unordered_map<std::wstring, Entry>& table) {
            std::map<std::wstring, Entry> sorted(table.begin(), table.end());
            for (const auto& [key, entry] : sorted) {
                const double count = Faded(entry, today);
                if (count < .05 || today - entry.day > 400) continue;  // forgotten
                out += kind; out += '\t'; out += ToUtf8(key); out += '\t';
                out += std::to_string(count); out += '\t'; out += std::to_string(entry.day); out += '\n';
            }
        };
        write('w', words); write('p', pairs); write('r', rejections);
        return out;
    }
    bool Parse(std::string_view text) {
        words.clear(); pairs.clear(); rejections.clear(); total = 0;
        if (text.rfind("jellyflower keyboard 1\n", 0) != 0) return false;
        size_t at = text.find('\n') + 1;
        while (at < text.size()) {
            const size_t end = (std::min)(text.find('\n', at), text.size());
            const auto line = text.substr(at, end - at);
            at = end + 1;
            std::vector<std::string_view> fields;
            for (size_t start = 0;;) {
                const size_t tab = line.find('\t', start);
                fields.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
                if (tab == std::string_view::npos) break;
                start = tab + 1;
            }
            // Pairs and rejections carry one more tab-separated part.
            if (fields.size() < 4 || fields[0].size() != 1) continue;
            std::string key(fields[1]);
            for (size_t i = 2; i + 2 < fields.size(); ++i) { key += '\t'; key += fields[i]; }
            Entry entry;
            entry.count = std::strtod(std::string(fields[fields.size() - 2]).c_str(), nullptr);
            entry.day = static_cast<uint32_t>(std::strtoul(std::string(fields.back()).c_str(), nullptr, 10));
            if (!(entry.count > 0)) continue;
            const auto wide = FromUtf8(key.data(), key.size());
            if (fields[0] == "w") { words[wide] = entry; total += entry.count; }
            else if (fields[0] == "p") pairs[wide] = entry;
            else if (fields[0] == "r") rejections[wide] = entry;
        }
        changed = false;
        return true;
    }
};

// One way to read what was typed: the literal itself, a dictionary word,
// or two words where the space was missed or mistyped.
struct Option {
    std::wstring text;                  // what goes in the field, case applied
    uint32_t first = NoWord, last = NoWord;  // dictionary words (last differs for two words)
    std::wstring firstText, lastText;   // the words, for context and learning
    float edit = 0;                     // bits: how unlikely the taps are for this reading
    bool literal = false, known = false;
};

class Engine {
public:
    Params params;
    UserModel user;
    uint32_t today = 0;

    bool Load(const void* data, size_t size, Language language) {
        language_ = language;
        layout_ = Layout::For(language);
        BuildConfusables();
        return dictionary_.Load(data, size);
    }
    const Dictionary& Words() const noexcept { return dictionary_; }
    Language Lang() const noexcept { return language_; }
    void SetLayout(Layout layout) { layout_ = std::move(layout); }
    const Layout& KeyLayout() const noexcept { return layout_; }

    // Known word: in the dictionary or learned. A capital first letter may be
    // the keyboard's own sentence capitalization ("The"); a lowercase one
    // never matches a capitalized word ("i" is not "I", "paris" not "Paris").
    // At a sentence start the capital says nothing, so the commoner form
    // wins ("In" is "in", not the element; "May" is "may"). Mid-sentence a
    // capitalized word stays the capitalized entry ("Will", "May") if any.
    uint32_t Lookup(std::wstring_view text, bool sentenceStart = false) const {
        const uint32_t exact = dictionary_.Find(text);
        if (text.empty() || !IsUpper(text[0])) return exact;
        std::wstring other(text);
        other[0] = Lower(other[0]);
        const uint32_t lower = dictionary_.Find(other);
        if (exact == NoWord) return lower;
        if (lower == NoWord) return exact;
        return sentenceStart && dictionary_.Unigram(lower) < dictionary_.Unigram(exact) ? lower : exact;
    }
    // The form a word is learned under: the dictionary's ("If" -> "if").
    std::wstring Canonical(std::wstring_view text, bool sentenceStart = true) const {
        const uint32_t id = Lookup(text, sentenceStart);
        return id == NoWord ? std::wstring(text) : std::wstring(dictionary_.Text(id));
    }
    bool Known(std::wstring_view text) const {
        if (Lookup(text) != NoWord) return true;
        // A hyphenated compound of known words is known (over-heating, in-growing).
        if (const size_t hyphen = text.find(L'-'); hyphen != std::wstring_view::npos && hyphen && hyphen + 1 < text.size())
            return Known(text.substr(0, hyphen)) && Known(text.substr(hyphen + 1));
        std::wstring lower(text);
        if (!lower.empty()) lower[0] = Lower(lower[0]);
        return user.Known(text, today) || user.Known(lower, today);
    }
    // Whether a typed token may be corrected at all: never digits, mixed
    // case (iPhone), all caps (NASA), or anything with @ / : _ . in it.
    static bool Correctable(std::wstring_view typed) {
        if (typed.empty() || typed.size() > 48) return false;
        size_t upper = 0;
        for (size_t i = 0; i < typed.size(); ++i) {
            const wchar_t c = typed[i];
            if (!IsWordCharacter(c)) return false;
            if (IsUpper(c)) { ++upper; if (i) return false; }
        }
        return !(upper > 1);
    }

    // -log2 P(word | previous), the dictionary mixed with this user.
    float Cost(uint32_t previous, std::wstring_view previousText, uint32_t word, std::wstring_view text) const {
        const double background = word == NoWord ? 0 : std::exp2(-static_cast<double>(dictionary_.Bigram(previous, word)));
        const double personal = user.Probability(previousText, text, today);
        const double weight = personal > 0 ? params.userWeight : 0;
        const double p = (1 - weight) * background + weight * personal;
        return p > 0 ? static_cast<float>(-std::log2(p)) : params.unknownUnigram + dictionary_.Backoff(previous);
    }
    float Cost(uint32_t previous, std::wstring_view previousText, const Option& option) const {
        if (option.literal && !option.known) {
            // An unknown word: as rare as anything unseen after the previous word.
            const double personal = user.Probability(previousText, option.text, today);
            const double background = std::exp2(-static_cast<double>(dictionary_.Backoff(previous) + params.unknownUnigram));
            return static_cast<float>(-std::log2((1 - params.userWeight) * background + params.userWeight * personal));
        }
        float cost = Cost(previous, previousText, option.first, option.firstText);
        if (option.last != option.first || option.lastText != option.firstText) cost += Cost(option.first, option.firstText, option.last, option.lastText);
        return cost;
    }

    // The literal plus the most plausible readings, cheapest edit first.
    std::vector<Option> Options(std::wstring_view typed, bool sentenceStart, size_t limit = 10) const {
        std::vector<Option> out;
        Option literal;
        literal.text = typed;
        literal.literal = true;
        literal.known = Known(typed);
        literal.first = literal.last = Lookup(typed, sentenceStart);
        literal.firstText = literal.lastText = literal.first != NoWord ? std::wstring(dictionary_.Text(literal.first)) : std::wstring(typed);
        literal.edit = literal.known ? 0 : params.unknownWord;
        out.push_back(literal);
        if (!Correctable(typed)) return out;
        const bool capital = IsUpper(typed[0]);
        // A capitalized word mid-sentence is a name or a title: kept. So is a
        // name the dictionary knows, wherever it is typed with its capital.
        if (capital && !sentenceStart) return out;
        if (capital && literal.first != NoWord && dictionary_.IsName(literal.first)) return out;
        const auto key = Fold(typed);
        float budget = key.size() <= 2 ? 7.5f : key.size() <= 4 ? 12.f : key.size() <= 7 ? 17.f : 22.f;
        // A real word changes only through a small slip (a neighbouring key,
        // an apostrophe, a doubled letter, a known mix-up), never a rewrite.
        if (literal.known) budget = params.validEdit;
        // An unknown capitalized first word may be a name: one slip at most.
        else if (capital) budget = (std::min)(budget, 9.f);
        std::vector<std::pair<uint32_t, float>> matches;
        Search(key, budget, matches);
        std::wstring lowerTyped(typed);
        for (auto& c : lowerTyped) c = Lower(c);
        const uint32_t white = dictionary_.Whitelist(lowerTyped);
        if (white != NoWord) matches.push_back({white, 0.f});
        if (literal.known) {
            const auto found = confusables_.find(lowerTyped);
            if (found != confusables_.end())
                for (const auto& other : found->second) {
                    const uint32_t id = dictionary_.Find(other);
                    if (id != NoWord) matches.push_back({id, -1.f});
                }
        }
        for (const auto& [id, distance] : matches) {
            if (dictionary_.Offensive(id) && !user.Known(dictionary_.Text(id), today)) continue;
            const auto text = dictionary_.Text(id);
            // One-letter words only from one letter ("i" -> "I"), never "Uh" -> "U".
            if (dictionary_.Key(id).size() == 1 && key.size() > 1 && id != white) continue;
            if (text == typed) continue;
            Option option;
            option.first = option.last = id;
            option.firstText = option.lastText = std::wstring(text);
            option.text = Cased(text, typed);
            if (option.text == typed) continue;
            option.edit = distance < 0 ? params.confusable : distance + Adjust(typed, text);
            if (id == white) option.edit = (std::min)(option.edit, params.whitelisted);
            if (literal.known && option.edit > params.validEdit) continue;
            if (user.Rejected(key, text, today)) continue;
            out.push_back(std::move(option));
        }
        Learned(key, typed, budget, out);
        if (!literal.known) Splits(typed, out);
        // Duplicates keep their cheapest reading.
        std::sort(out.begin() + 1, out.end(), [&](const Option& a, const Option& b) { return Rank(a) < Rank(b); });
        std::vector<Option> unique;
        for (auto& option : out) {
            if (std::any_of(unique.begin(), unique.end(), [&](const Option& o) { return o.text == option.text; })) continue;
            unique.push_back(std::move(option));
            if (unique.size() >= limit) break;
        }
        return unique;
    }

    // Best reading of one word given only the word before it.
    Option Correct(std::wstring_view typed, uint32_t previous, std::wstring_view previousText, bool sentenceStart) const {
        auto options = Options(typed, sentenceStart);
        size_t best = 0;
        float bestTotal = options[0].edit + Cost(previous, previousText, options[0]) - (options[0].known ? params.revision : 0);
        for (size_t i = 1; i < options.size(); ++i) {
            const float total = options[i].edit + Cost(previous, previousText, options[i]);
            if (total < bestTotal) { bestTotal = total; best = i; }
        }
        return options[best];
    }

    // Completions of a partial word and next-word predictions, best first.
    std::vector<std::wstring> Complete(std::wstring_view typed, uint32_t previous, std::wstring_view previousText, bool sentenceStart, size_t limit) const {
        std::vector<std::pair<float, std::wstring>> scored;
        const auto add = [&](uint32_t id, std::wstring_view text, float cost) {
            if (id != NoWord && dictionary_.Offensive(id) && !user.Known(text, today)) return;
            auto shown = typed.empty() ? std::wstring(text) : Cased(text, typed);
            if (typed.empty() && sentenceStart && !shown.empty()) shown[0] = Upper(shown[0]);
            for (auto& item : scored) if (item.second == shown) { item.first = (std::min)(item.first, cost); return; }
            scored.push_back({cost, std::move(shown)});
        };
        if (typed.empty()) {
            const auto [begin, end] = dictionary_.Followers(sentenceStart ? SentenceStart : previous);
            std::vector<std::pair<uint8_t, uint32_t>> best;
            for (const auto* f = begin; f != end; ++f) best.push_back({f->cost, f->word});
            std::sort(best.begin(), best.end());
            for (size_t i = 0; i < best.size() && i < 24; ++i) add(best[i].second, dictionary_.Text(best[i].second), Cost(sentenceStart ? SentenceStart : previous, previousText, best[i].second, dictionary_.Text(best[i].second)));
            for (const auto& [pair, entry] : user.pairs) {
                const auto tab = pair.find(L'\t');
                if (std::wstring_view(pair).substr(0, tab) != previousText || UserModel::Faded(entry, today) < 1.99) continue;
                const auto word = std::wstring_view(pair).substr(tab + 1);
                add(dictionary_.Find(word), word, Cost(previous, previousText, dictionary_.Find(word), word));
            }
        } else {
            const auto key = Fold(typed);
            const uint32_t node = dictionary_.Walk(key);
            if (node != NoWord) {
                // Best-first over the subtree by its cheapest word.
                using Item = std::pair<uint8_t, uint32_t>;
                std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
                queue.push({dictionary_.Nodes()[node].best, node});
                std::vector<uint32_t> found;
                while (!queue.empty() && found.size() < 16) {
                    const auto [cost, index] = queue.top();
                    queue.pop();
                    const auto& n = dictionary_.Nodes()[index];
                    for (uint16_t i = 0; i < n.wordCount; ++i) found.push_back(dictionary_.NodeWords()[n.words + i]);
                    for (uint16_t i = 0; i < n.childCount; ++i) queue.push({dictionary_.Nodes()[n.children + i].best, n.children + i});
                }
                for (const uint32_t id : found) {
                    const auto text = dictionary_.Text(id);
                    // Typed accents must be kept.
                    if (Adjust(typed, text) >= params.lostAccent) continue;
                    add(id, text, Cost(previous, previousText, id, text));
                }
                const auto [begin, end] = dictionary_.Followers(sentenceStart ? SentenceStart : previous);
                for (const auto* f = begin; f != end; ++f)
                    if (dictionary_.Key(f->word).substr(0, key.size()) == key) add(f->word, dictionary_.Text(f->word), Cost(previous, previousText, f->word, dictionary_.Text(f->word)));
            }
            for (const auto& [word, entry] : user.words)
                if (UserModel::Faded(entry, today) >= 1.99 && Fold(word).substr(0, key.size()) == key) add(dictionary_.Find(word), word, Cost(previous, previousText, dictionary_.Find(word), word));
        }
        std::sort(scored.begin(), scored.end());
        std::vector<std::wstring> out;
        for (auto& item : scored) { if (out.size() >= limit) break; out.push_back(std::move(item.second)); }
        return out;
    }

private:
    Dictionary dictionary_;
    Layout layout_ = Layout::For(Language::English);
    Language language_ = Language::English;
    std::unordered_map<std::wstring, std::vector<std::wstring>> confusables_;

    float Rank(const Option& option) const {
        return option.edit + (option.first == NoWord ? params.unknownUnigram : dictionary_.Unigram(option.first)) * .5f;
    }
    // Words people mix up while spelling correctly: corrected from context alone.
    void BuildConfusables() {
        confusables_.clear();
        static const wchar_t* const english[] = {L"its|it's", L"your|you're", L"there|their|they're", L"then|than", L"to|too",
            L"were|we're|where", L"whose|who's", L"lose|loose", L"of|off", L"weather|whether", L"accept|except", L"quite|quiet", L"were|we're", L"well|we'll", L"ill|I'll", L"id|I'd", L"hell|he'll", L"shell|she'll", L"wont|won't", L"cant|can't", L"lets|let's"};
        static const wchar_t* const french[] = {L"a|à", L"ou|où", L"ce|se", L"ces|ses", L"son|sont", L"on|ont", L"et|est", L"la|là", L"sa|ça", L"du|dû", L"sur|sûr", L"mur|mûr"};
        const auto& sets = language_ == Language::French ? std::vector<const wchar_t*>(std::begin(french), std::end(french)) : std::vector<const wchar_t*>(std::begin(english), std::end(english));
        for (const auto* set : sets) {
            std::vector<std::wstring> words;
            std::wstring current;
            for (const wchar_t* c = set;; ++c) {
                if (!*c || *c == L'|') { words.push_back(current); current.clear(); if (!*c) break; }
                else current += *c;
            }
            for (const auto& word : words) {
                std::wstring lower(word);
                for (auto& c : lower) c = Lower(c);
                for (const auto& other : words) if (other != word) confusables_[lower].push_back(other);
            }
        }
    }
    // The dictionary word in the typed word's case: "Teh" -> "The".
    static std::wstring Cased(std::wstring_view word, std::wstring_view typed) {
        std::wstring out(word);
        if (!typed.empty() && !out.empty() && IsUpper(typed[0]) && !IsUpper(out[0])) out[0] = Upper(out[0]);
        return out;
    }
    // Costs the folded search can't see: apostrophes, accents, name case.
    float Adjust(std::wstring_view typed, std::wstring_view word) const {
        float cost = 0;
        const auto apostrophes = [](std::wstring_view s) { return std::count_if(s.begin(), s.end(), IsApostrophe); };
        const auto wordApostrophes = apostrophes(word), typedApostrophes = apostrophes(typed);
        if (wordApostrophes > typedApostrophes) cost += params.apostrophe * static_cast<float>(wordApostrophes - typedApostrophes);
        if (typedApostrophes > wordApostrophes) cost += params.extraApostrophe;
        // Accents are counted per letter: "célébré" is not "célèbre".
        const auto count = [](std::wstring_view s, wchar_t c) { return std::count_if(s.begin(), s.end(), [&](wchar_t x) { return Lower(x) == c; }); };
        for (size_t i = 0; i < word.size(); ++i) {
            const wchar_t c = Lower(word[i]);
            if (!Accented(c) || word.substr(0, i).find(word[i]) != std::wstring_view::npos) continue;
            const auto have = count(typed, c), want = count(word, c);
            if (want > have) cost += params.accent * static_cast<float>(want - have);
        }
        for (size_t i = 0; i < typed.size(); ++i) {
            const wchar_t c = Lower(typed[i]);
            if (!Accented(c) || typed.substr(0, i).find(typed[i]) != std::wstring_view::npos) continue;
            const auto have = count(word, c), typedCount = count(typed, c);
            if (typedCount > have) cost += params.lostAccent * static_cast<float>(typedCount - have);
        }
        if (!word.empty() && IsUpper(word[0]) && !typed.empty() && !IsUpper(typed[0]) && !(word[0] == L'I' && (word.size() == 1 || IsApostrophe(word[1])))) cost += params.nameCase;
        return cost;
    }
    float Substitute(wchar_t typed, wchar_t meant) const {
        if (typed == meant) return 0;
        const float d2 = layout_.Distance2(typed, meant);
        return (std::min)(params.substitution, params.neighbor * (std::max)(1.f, d2));
    }
    // Weighted Damerau-Levenshtein over the trie: each row holds the cost of
    // matching the typed prefix against the trie path so far.
    void Search(const std::wstring& typed, float budget, std::vector<std::pair<uint32_t, float>>& out) const {
        const size_t n = typed.size();
        std::vector<float> rows((n + 1) * 66, 0);
        for (size_t j = 0; j <= n; ++j) {
            float insert = 0;
            for (size_t k = 0; k < j; ++k) insert += InsertCost(typed, k);
            rows[j] = insert;
        }
        std::wstring path;
        Visit(0, 0, typed, budget, rows, path, out);
    }
    float InsertCost(const std::wstring& typed, size_t k) const {
        const wchar_t c = typed[k];
        if (k && typed[k - 1] == c) return params.repeatedInsertion;
        if ((k && layout_.Distance2(c, typed[k - 1]) <= 1.3f) || (k + 1 < typed.size() && layout_.Distance2(c, typed[k + 1]) <= 1.3f)) return params.neighborInsertion;
        return params.insertion;
    }
    void Visit(uint32_t node, size_t depth, const std::wstring& typed, float budget, std::vector<float>& rows, std::wstring& path, std::vector<std::pair<uint32_t, float>>& out) const {
        const size_t n = typed.size();
        const auto& nodes = dictionary_.Nodes();
        const auto& current = nodes[node];
        if (current.wordCount && rows[depth * (n + 1) + n] <= budget)
            for (uint16_t i = 0; i < current.wordCount; ++i) out.push_back({dictionary_.NodeWords()[current.words + i], rows[depth * (n + 1) + n]});
        if (depth >= 64) return;
        for (uint16_t i = 0; i < current.childCount; ++i) {
            const uint32_t childIndex = current.children + i;
            const wchar_t c = nodes[childIndex].ch;
            const float* prev = &rows[depth * (n + 1)];
            float* row = &rows[(depth + 1) * (n + 1)];
            const wchar_t before = depth ? path[depth - 1] : 0;
            const float omit = c == before ? params.doubledOmission : c == L'-' ? 1.f : params.omission;
            row[0] = prev[0] + omit;
            float low = row[0];
            for (size_t j = 1; j <= n; ++j) {
                float value = prev[j - 1] + Substitute(typed[j - 1], c);
                value = (std::min)(value, prev[j] + omit);
                value = (std::min)(value, row[j - 1] + InsertCost(typed, j - 1));
                if (depth >= 1 && j >= 2 && typed[j - 1] == before && typed[j - 2] == c && typed[j - 1] != c)
                    value = (std::min)(value, rows[(depth - 1) * (n + 1) + j - 2] + params.transposition);
                row[j] = value;
                low = (std::min)(low, value);
            }
            if (low > budget) continue;
            path.resize(depth);
            path += c;
            Visit(childIndex, depth + 1, typed, budget, rows, path, out);
        }
    }
    // Learned words are few: compare them directly.
    void Learned(const std::wstring& key, std::wstring_view typed, float budget, std::vector<Option>& out) const {
        for (const auto& [word, entry] : user.words) {
            if (UserModel::Faded(entry, today) < 2.99 || dictionary_.Find(word) != NoWord || word == typed) continue;
            const auto other = Fold(word);
            const float distance = Distance(key, other);
            if (distance > budget) continue;
            Option option;
            option.text = Cased(word, typed);
            option.firstText = option.lastText = word;
            option.edit = distance + Adjust(typed, word);
            option.known = true;
            out.push_back(std::move(option));
        }
    }
    float Distance(const std::wstring& typed, const std::wstring& word) const {
        const size_t n = typed.size(), m = word.size();
        std::vector<float> d((n + 1) * (m + 1), 0);
        const auto at = [&](size_t i, size_t j) -> float& { return d[i * (m + 1) + j]; };
        for (size_t i = 1; i <= n; ++i) at(i, 0) = at(i - 1, 0) + InsertCost(typed, i - 1);
        for (size_t j = 1; j <= m; ++j) at(0, j) = at(0, j - 1) + params.omission;
        for (size_t i = 1; i <= n; ++i)
            for (size_t j = 1; j <= m; ++j) {
                float value = at(i - 1, j - 1) + Substitute(typed[i - 1], word[j - 1]);
                value = (std::min)(value, at(i, j - 1) + (j > 1 && word[j - 1] == word[j - 2] ? params.doubledOmission : params.omission));
                value = (std::min)(value, at(i - 1, j) + InsertCost(typed, i - 1));
                if (i > 1 && j > 1 && typed[i - 1] == word[j - 2] && typed[i - 2] == word[j - 1]) value = (std::min)(value, at(i - 2, j - 2) + params.transposition);
                at(i, j) = value;
            }
        return at(n, m);
    }
    // The cheapest known word with exactly this key (apostrophes free).
    uint32_t Exact(std::wstring_view part) const {
        const auto key = Fold(part);
        const auto [begin, end] = dictionary_.WithKey(key);
        uint32_t best = NoWord;
        for (const auto* id = begin; id && id != end; ++id)
            if (!dictionary_.Offensive(*id) && !dictionary_.IsName(*id) && (best == NoWord || dictionary_.Unigram(*id) < dictionary_.Unigram(best))) best = *id;
        return best;
    }
    // Two words: a missing space ("tothe") or a letter hit for the space bar ("tocthe").
    void Splits(std::wstring_view typed, std::vector<Option>& out) const {
        if (typed.size() < 3) return;
        const auto add = [&](std::wstring_view left, std::wstring_view right, float cost) {
            if (left.empty() || right.empty()) return;
            const uint32_t a = Exact(left), b = Exact(right);
            if (a == NoWord || b == NoWord) return;
            // One-letter words only where they are words on their own.
            if ((left.size() == 1 && dictionary_.Unigram(a) > 9) || (right.size() == 1 && dictionary_.Unigram(b) > 9)) return;
            Option option;
            option.first = a; option.last = b;
            option.firstText = std::wstring(dictionary_.Text(a));
            option.lastText = std::wstring(dictionary_.Text(b));
            option.text = Cased(option.firstText, left) + L' ' + option.lastText;
            option.edit = cost + Adjust(left, option.firstText) + Adjust(right, option.lastText);
            if (user.Rejected(Fold(typed), option.text, today)) return;
            out.push_back(std::move(option));
        };
        for (size_t i = 1; i < typed.size(); ++i) {
            add(typed.substr(0, i), typed.substr(i), params.missingSpace);
            if (i + 1 < typed.size() && layout_.NearSpace(Lower(typed[i]))) add(typed.substr(0, i), typed.substr(i + 1), params.spaceSlip);
        }
    }
};
}
