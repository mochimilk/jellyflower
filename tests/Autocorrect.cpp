// SPDX-License-Identifier: GPL-3.0-or-later
// The touch keyboard's autocorrect engine and typing session. No shell or
// desktop: keystrokes go into a simulated text field.
//   Autocorrect.exe [dictionary folder]
//   Autocorrect.exe --evaluate <sentences.txt> [en|fr] [count] [name=value...] [dictionary folder]
// --evaluate types held-out sentences with simulated touch noise (Gaussian
// taps around key centers, plus a few skipped, doubled and swapped letters)
// and reports word errors before and after correction.
#include "../src/autocorrect_session.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace acrylic::autocorrect;

namespace {
void Check(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
std::string Narrow(const std::wstring& text) { return ToUtf8(text); }

std::vector<char> ReadFile(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + Narrow(path));
    return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}
Engine Load(const std::wstring& folder, Language language) {
    Engine engine;
    const auto data = ReadFile(folder + (language == Language::French ? L"\\fr.jfdict" : L"\\en.jfdict"));
    Check(engine.Load(data.data(), data.size(), language), "dictionary loads");
    engine.today = 20000;
    return engine;
}

// The app's text: keystrokes the session lets through, plus its edits.
struct Field {
    std::wstring text;
    Session& session;
    void Key(wchar_t c) {
        Edit edit;
        if (session.Type(c, edit)) Apply(edit);
        else text += c;
    }
    void Apply(const Edit& edit) {
        Check(edit.erase <= text.size(), "edit erases more than the field holds");
        text.resize(text.size() - edit.erase);
        text += edit.insert;
    }
    void Type(std::wstring_view keys) { for (const wchar_t c : keys) Key(c); }
    void Backspace() { if (!text.empty()) text.pop_back(); session.Backspace(); }
    void Pick(size_t slot) {
        Edit edit;
        Check(session.Pick(session.Suggestions(), slot, edit), "slot can be picked");
        Apply(edit);
    }
};

void Expect(Engine& engine, std::wstring_view typed, std::wstring_view expected) {
    Session session(engine);
    session.Reset({}, true);
    Field field{{}, session};
    field.Type(typed);
    if (field.text != expected) {
        std::printf("typed    '%s'\nexpected '%s'\ngot      '%s'\n", Narrow(std::wstring(typed)).c_str(), Narrow(std::wstring(expected)).c_str(), Narrow(field.text).c_str());
        throw std::runtime_error("autocorrect result");
    }
    Check(session.Text() == field.text, "session tracks the field");
}

void Behaviour(Engine& en) {
    // The user's own example: a real word fixed by its neighbour, and a letter hit for the space bar.
    Expect(en, L"if like to go tocthe party ", L"I'd like to go to the party ");
    Expect(en, L"If like to go tocthe party.", L"I'd like to go to the party.");
    // Plain typos, apostrophes and the lone i.
    Expect(en, L"teh ", L"the ");
    Expect(en, L"dont do that ", L"don't do that ");
    Expect(en, L"i think so ", L"I think so ");
    Expect(en, L"im here ", L"I'm here ");
    Expect(en, L"recieve ", L"receive ");
    Expect(en, L"helo world ", L"hello world ");
    Expect(en, L"Thsi is good ", L"This is good ");
    Expect(en, L"tothe store ", L"to the store ");
    // Context: real words that are wrong next to their neighbours.
    Expect(en, L"its a nice day ", L"it's a nice day ");
    Expect(en, L"your welcome ", L"you're welcome ");
    // Left alone: names mid-sentence, mixed case, capitals, digits, real words in context.
    Expect(en, L"I met Kocienda today ", L"I met Kocienda today ");
    Expect(en, L"my iPhone ", L"my iPhone ");
    Expect(en, L"NASA said ", L"NASA said ");
    Expect(en, L"abc123 ", L"abc123 ");
    Expect(en, L"it is on its own ", L"it is on its own ");
    Expect(en, L"I will go ", L"I will go ");

    // Punctuation replaces a tapped suggestion's space.
    {
        Session session(en);
        session.Reset({}, true);
        Field field{{}, session};
        field.Type(L"I said helo");
        const auto bar = session.Suggestions();
        Check(bar.slots[0].kind == SlotKind::Literal && bar.slots[0].text == L"helo", "bar shows the typed word on the left");
        Check(bar.correcting && bar.slots[1].kind == SlotKind::Correction && bar.slots[1].text == L"hello", "bar highlights the correction in the middle");
        field.Pick(1);
        Check(field.text == L"I said hello ", "a picked suggestion brings a space");
        field.Key(L'.');
        Check(field.text == L"I said hello.", "punctuation replaces the automatic space");
    }
    // ↶ puts back the typed word, and the correction is not made again.
    {
        Session session(en);
        session.Reset({}, true);
        Field field{{}, session};
        field.Type(L"we went to Zanzibarr ");
        field.Type(L"teh ");
        Check(field.text == L"we went to Zanzibarr the ", "corrected");
        const auto bar = session.Suggestions();
        Check(bar.slots[0].kind == SlotKind::Revert && bar.slots[0].text == L"teh", "bar offers the original after a correction");
        field.Pick(0);
        Check(field.text == L"we went to Zanzibarr teh ", "revert restores the typed word");
        field.Type(L"and teh ");
        Check(field.text == L"we went to Zanzibarr teh and teh ", "a reverted correction is not made again");
    }
    // Backspacing into a correction offers the original; deleting it and retyping keeps it.
    {
        Session session(en);
        session.Reset({}, true);
        Field field{{}, session};
        field.Type(L"say helo ");
        Check(field.text == L"say hello ", "corrected");
        field.Backspace();
        Check(session.Suggestions().slots[0].kind == SlotKind::Revert, "backspacing into the word offers the original");
        for (int i = 0; i < 5; ++i) field.Backspace();
        field.Type(L"helo ");
        Check(field.text == L"say helo ", "deleting a correction and retyping keeps the letters");
    }
    // Tapping the quoted word twice teaches it.
    {
        for (int round = 0; round < 2; ++round) {
            Session session(en);
            session.Reset({}, true);
            Field field{{}, session};
            field.Type(L"the floof");
            Check(session.Suggestions().slots[0].text == L"floof", "literal slot");
            field.Pick(0);
            Check(field.text == L"the floof ", "the literal stays");
            session.Flush();
        }
        Expect(en, L"a floof ", L"a floof ");
    }
    // Next-word predictions after a space, best in the middle.
    {
        Session session(en);
        session.Reset({}, true);
        Field field{{}, session};
        field.Type(L"I'd like ");
        const auto bar = session.Suggestions();
        Check(bar.slots[1].text == L"to", "predicts 'to' after 'I'd like'");
    }
    // The personal model survives a round trip.
    {
        const auto saved = en.user.Serialize(en.today);
        UserModel copy;
        Check(copy.Parse(saved) && copy.Known(L"floof", en.today), "user model round trip");
    }
}

void French(Engine& fr) {
    Expect(fr, L"cest deja fait ", L"c'est déjà fait ");
    Expect(fr, L"jai faim ", L"j'ai faim ");
    Expect(fr, L"il a mangé ", L"il a mangé ");
    Expect(fr, L"je vais a la maison ", L"je vais à la maison ");
}

// Simulated touch typing for --evaluate.
struct Typist {
    const Layout& layout;
    std::mt19937 random{1234};
    double sigma = .25, omit = .01, insert = .005, swap = .005;
    wchar_t Tap(wchar_t c) {
        const bool upper = IsUpper(c);
        const wchar_t lower = Lower(c);
        const auto key = layout.keys.find(lower);
        if (key == layout.keys.end() && c != L' ') return c;
        std::normal_distribution<float> noise(0, static_cast<float>(sigma));
        float x = c == L' ' ? (layout.spaceLeft + layout.spaceRight) / 2 + std::uniform_real_distribution<float>(-1.5f, 1.5f)(random) : key->second.first;
        float y = c == L' ' ? layout.spaceRow : key->second.second;
        x += noise(random); y += noise(random);
        if (y > layout.spaceRow - .5f && x > layout.spaceLeft - .5f && x < layout.spaceRight + .5f) return L' ';
        wchar_t best = lower;
        float distance = 1e9f;
        for (const auto& [k, position] : layout.keys) {
            const float dx = position.first - x, dy = position.second - y, d = dx * dx + dy * dy;
            if (d < distance) { distance = d; best = k; }
        }
        if (c == L' ' && distance > .5f) return L' ';
        return upper ? Upper(best) : best;
    }
    std::wstring Type(const std::wstring& text) {
        std::wstring out;
        std::uniform_real_distribution<double> chance(0, 1);
        for (size_t i = 0; i < text.size(); ++i) {
            const wchar_t c = text[i];
            if (IsLetter(c)) {
                const double roll = chance(random);
                if (roll < omit) continue;
                if (roll < omit + swap && i + 1 < text.size() && IsLetter(text[i + 1])) { out += Tap(text[i + 1]); out += Tap(c); ++i; continue; }
                if (roll < omit + swap + insert) out += Tap(c);
            }
            out += c == L' ' || IsLetter(c) ? Tap(c) : c;
        }
        return out;
    }
};
std::vector<std::wstring> Split(const std::wstring& text) {
    std::vector<std::wstring> out;
    std::wstring current;
    for (const wchar_t c : text) {
        if (c == L' ') { if (!current.empty()) out.push_back(current); current.clear(); }
        else current += c;
    }
    if (!current.empty()) out.push_back(current);
    return out;
}
size_t WordErrors(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
    std::vector<size_t> row(b.size() + 1), next(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        next[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) next[j] = (std::min)({row[j] + 1, next[j - 1] + 1, row[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(row, next);
    }
    return row[b.size()];
}
// name=value overrides for tuning, e.g. revision=3 validEdit=5.
void Override(Params& params, const std::wstring& setting) {
    const auto equals = setting.find(L'=');
    if (equals == std::wstring::npos) throw std::runtime_error("expected name=value");
    const auto name = setting.substr(0, equals);
    const float value = std::stof(setting.substr(equals + 1));
    std::pair<const wchar_t*, float*> fields[] = {{L"neighbor", &params.neighbor}, {L"substitution", &params.substitution},
        {L"omission", &params.omission}, {L"insertion", &params.insertion}, {L"transposition", &params.transposition},
        {L"apostrophe", &params.apostrophe}, {L"accent", &params.accent}, {L"spaceSlip", &params.spaceSlip},
        {L"missingSpace", &params.missingSpace}, {L"confusable", &params.confusable}, {L"unknownWord", &params.unknownWord},
        {L"unknownUnigram", &params.unknownUnigram}, {L"revision", &params.revision}, {L"validEdit", &params.validEdit}};
    for (auto& [field, target] : fields) if (name == field) { *target = value; return; }
    throw std::runtime_error("unknown parameter " + Narrow(name));
}
int Evaluate(const std::wstring& path, Language language, size_t count, const std::wstring& folder, const std::vector<std::wstring>& overrides) {
    auto engine = Load(folder, language);
    for (const auto& setting : overrides) Override(engine.params, setting);
    const auto data = ReadFile(path);
    const auto all = FromUtf8(data.data(), data.size());
    std::vector<std::wstring> sentences;
    std::wstringstream lines(all);
    for (std::wstring line; std::getline(lines, line) && sentences.size() < count;) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        for (auto& c : line) if (c == 0x2019) c = L'\'';
        if (line.size() > 3) sentences.push_back(line);
    }
    Typist typist{engine.KeyLayout()};
    size_t words = 0, before = 0, after = 0, clean = 0, broken = 0, keystrokes = 0;
    double seconds = 0;
    for (const auto& sentence : sentences) {
        const auto typed = typist.Type(sentence + L" ");
        Session session(engine);
        session.Reset({}, true);
        Field field{{}, session};
        const auto start = std::chrono::steady_clock::now();
        field.Type(typed);
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        keystrokes += typed.size();
        const auto reference = Split(sentence), noisy = Split(typed), output = Split(field.text);
        words += reference.size();
        before += WordErrors(noisy, reference);
        after += WordErrors(output, reference);
        // Words typed exactly right that came out different.
        if (noisy.size() == reference.size() && output.size() == reference.size())
            for (size_t i = 0; i < reference.size(); ++i) {
                if (noisy[i] != reference[i]) continue;
                ++clean;
                if (output[i] != reference[i]) {
                    ++broken;
                    if (broken <= 12) std::printf("  broke %s -> %s: typed [%s] got [%s]\n", Narrow(reference[i]).c_str(), Narrow(output[i]).c_str(), Narrow(typed).c_str(), Narrow(field.text).c_str());
                }
            }
    }
    std::printf("%zu sentences, %zu words\n", sentences.size(), words);
    std::printf("word errors: %.2f%% typed -> %.2f%% after autocorrect\n", 100.0 * before / words, 100.0 * after / words);
    std::printf("correct words broken: %zu of %zu (%.2f%%)\n", broken, clean, 100.0 * broken / (std::max)(clean, size_t{1}));
    std::printf("%.3f ms per keystroke\n", 1000 * seconds / (std::max)(keystrokes, size_t{1}));
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring folder = JELLYFLOWER_DICTIONARIES;
        if (argc >= 3 && std::wstring(argv[1]) == L"--evaluate") {
            const Language language = argc >= 4 && std::wstring(argv[3]) == L"fr" ? Language::French : Language::English;
            const size_t count = argc >= 5 ? static_cast<size_t>(_wtoi(argv[4])) : 2000;
            std::vector<std::wstring> overrides;
            for (int i = 5; i < argc; ++i) {
                if (std::wstring(argv[i]).find(L'=') != std::wstring::npos) overrides.push_back(argv[i]);
                else folder = argv[i];
            }
            return Evaluate(argv[2], language, count, folder, overrides);
        }
        if (argc >= 4 && std::wstring(argv[1]) == L"--type") {
            // --type <en|fr> <text>: what the session makes of exactly these keys.
            auto engine = Load(folder, std::wstring(argv[2]) == L"fr" ? Language::French : Language::English);
            Session session(engine);
            session.Reset({}, true);
            Field field{{}, session};
            field.Type(argv[3]);
            std::printf("'%s'\n", Narrow(field.text).c_str());
            return 0;
        }
        if (argc >= 4 && std::wstring(argv[1]) == L"--explain") {
            // --explain <en|fr> <word> [previous word]: every reading and its cost.
            auto engine = Load(folder, std::wstring(argv[2]) == L"fr" ? Language::French : Language::English);
            const std::wstring previous = argc >= 5 ? argv[4] : L"";
            const uint32_t id = previous.empty() ? SentenceStart : engine.Lookup(previous);
            std::printf("known: %d, previous id %u\n", engine.Known(argv[3]) ? 1 : 0, id);
            for (const auto& option : engine.Options(argv[3], previous.empty(), 20))
                std::printf("  %-20s edit %6.2f  context %6.2f  total %6.2f%s\n", Narrow(option.text).c_str(), option.edit,
                    engine.Cost(id, previous, option), option.edit + engine.Cost(id, previous, option), option.literal ? "  (literal)" : "");
            return 0;
        }
        if (argc >= 2) folder = argv[1];
        const auto start = std::chrono::steady_clock::now();
        auto en = Load(folder, Language::English);
        std::printf("English dictionary: %zu words in %.0f ms\n", en.Words().Size(), 1000 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        Behaviour(en);
        auto fr = Load(folder, Language::French);
        French(fr);
        std::puts("Autocorrect passed");
        return 0;
    } catch (const std::exception& error) {
        std::printf("Autocorrect failed: %s\n", error.what());
        return 1;
    }
}
