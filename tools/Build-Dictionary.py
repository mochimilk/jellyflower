# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a touch keyboard dictionary (assets/dictionaries/<lang>.jfdict).

Sources (see THIRD_PARTY_NOTICES.md for licenses and attribution):
  --aosp       AOSP LatinIME <lang>_wordlist.combined.gz (Apache-2.0): the
               curated vocabulary, log frequencies, offensive flags and
               whitelisted typo corrections ("nave" -> "have").
  --heliboard  HeliBoard main_<lang>__experimental.combined (CC BY 4.0 from
               Leipzig corpora): more vocabulary and a few news bigrams.
  --tatoeba    Tatoeba <lang>_sentences.tsv.bz2 (CC BY 2.0 FR): everyday
               sentences for unigram and bigram counts.

The build is deterministic. Sentences whose Tatoeba id is a multiple of 50
are left out of the counts and, with --holdout, written to a file for
evaluating the autocorrect engine on text it has not seen.

File format (little endian, all strings UTF-8):
  "JFD1", u32 version=1, u32 words, u32 whitelist entries, u32 bigram lists
  words, most frequent first: u8 cost, u8 flags, u8 length, bytes
      cost = round(-8 * log2 P(word)); flags 1 = possibly offensive
  whitelist: u8 length, bytes (the typed form), varint target word
  bigram lists: varint first word + 1 (0 = sentence start), u8 backoff cost,
      varint count, then per follower: varint index delta, u8 cost
      cost = round(-8 * log2 P(follower | first)), Witten-Bell smoothed;
      unlisted followers get backoff * P(follower)
  sentence ends: u8 default cost, u32 count, then per word: varint index
      delta, u8 cost = round(-8 * log2 P(the sentence ends | word))
"""
import argparse
import bz2
import collections
import gzip
import math
import re
import struct
import unicodedata

APOSTROPHES = str.maketrans({"’": "'", "ʼ": "'", "‘": "'", "`": "'"})
TOKEN = re.compile(r"[^\W\d_]+(?:['-][^\W\d_]+)*'?", re.UNICODE)
SENTENCE_END = re.compile(r"[.!?…]+")
# Capitalized words that are not names: English first-person forms.
NOT_NAMES = {"I", "I'm", "I'd", "I'll", "I've"}


def valid_word(word):
    return 0 < len(word.encode("utf-8")) < 64 and all(c.isalpha() or c in "'-" for c in word) and word[0].isalpha()


def is_name(word):
    return word[0].isupper() and word not in NOT_NAMES


def read_combined(lines):
    """Yields (word, attributes, [(follower, rank)], [shortcut targets])."""
    current = None
    for line in lines:
        stripped = line.strip()
        if line.startswith(" word="):
            if current:
                yield current
            fields = dict(part.split("=", 1) for part in stripped.split(",") if "=" in part)
            current = (fields.get("word", ""), fields, [], [])
        elif current and line.startswith("  bigram="):
            fields = dict(part.split("=", 1) for part in stripped.split(",") if "=" in part)
            current[2].append((fields["bigram"], int(fields.get("f", "0"))))
        elif current and line.startswith("  shortcut="):
            fields = dict(part.split("=", 1) for part in stripped.split(",") if "=" in part)
            if fields.get("f") == "whitelist":
                current[3].append(fields["shortcut"])
    if current:
        yield current


def tokenize(text):
    """Splits a sentence into runs of word tokens; punctuation inside a
    sentence breaks the bigram chain, sentence ends start a new sentence."""
    text = unicodedata.normalize("NFC", text.translate(APOSTROPHES))
    for sentence in SENTENCE_END.split(text):
        runs, run, last = [], [], 0
        for match in TOKEN.finditer(sentence):
            between = sentence[last:match.start()]
            if run and between.strip():
                runs.append(run)
                run = []
            run.append(match.group().rstrip("'"))
            last = match.end()
        if run:
            runs.append(run)
        if runs:
            yield runs


def edits1(word):
    """Every string one deletion, swap, substitution or insertion away."""
    letters = set(word) | set("abcdefghijklmnopqrstuvwxyz'")
    splits = [(word[:i], word[i:]) for i in range(len(word) + 1)]
    out = {a + b[1:] for a, b in splits if b}
    out |= {a + b[1] + b[0] + b[2:] for a, b in splits if len(b) > 1}
    out |= {a + c + b[1:] for a, b in splits if b for c in letters}
    out |= {a + c + b for a, b in splits for c in letters}
    out.discard(word)
    return out


def looks_like_typo(word, weight, known):
    """A word only a noisy source has, one edit from a far more common one
    ("recieve" next to "receive"): a typo, not vocabulary."""
    lower = word.lower()
    return any(known.get(other, 0) >= weight for other in edits1(lower))


def varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def cost(probability):
    return max(0, min(255, round(-8 * math.log2(max(probability, 2.0 ** -31.9)))))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--aosp", required=True)
    parser.add_argument("--heliboard", required=True)
    parser.add_argument("--tatoeba", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--holdout")
    parser.add_argument("--max-words", type=int, default=200000)
    parser.add_argument("--followers", type=int, default=48)
    args = parser.parse_args()

    # Vocabulary: AOSP's curated list first, then HeliBoard's by frequency.
    news = {}          # word -> 0..255 log frequency
    offensive = set()
    whitelist = {}     # typed -> target
    heli_bigrams = []  # (first, follower, rank)
    with gzip.open(args.aosp, "rt", encoding="utf-8") as source:
        for word, fields, _, shortcuts in read_combined(source):
            word = unicodedata.normalize("NFC", word)
            if fields.get("not_a_word") == "true" or "whitelist" in fields or shortcuts:
                for target in shortcuts + ([fields["whitelist"]] if "whitelist" in fields else []):
                    whitelist[word] = unicodedata.normalize("NFC", target)
                if fields.get("not_a_word") == "true":
                    continue
            if not valid_word(word):
                continue
            news[word] = max(news.get(word, 0), int(fields.get("f", "0")))
            if fields.get("possibly_offensive") == "true":
                offensive.add(word)
    aosp_words = set(news)
    extra = []
    with open(args.heliboard, encoding="utf-8") as source:
        for word, fields, bigrams, _ in read_combined(source):
            word = unicodedata.normalize("NFC", word)
            if not valid_word(word):
                continue
            if fields.get("possibly_offensive") == "true":
                offensive.add(word)
            frequency = int(fields.get("f", "0"))
            if word not in aosp_words:
                extra.append((frequency, word))
            heli_bigrams.extend((word, unicodedata.normalize("NFC", follower), rank) for follower, rank in bigrams)
    extra.sort(key=lambda item: (-item[0], item[1]))
    curated = {}
    for word, frequency in news.items():
        curated[word.lower()] = max(curated.get(word.lower(), 0), frequency)
    room = max(0, args.max_words - len(news))
    typos = 0
    for frequency, word in extra:
        if room <= 0:
            break
        if looks_like_typo(word, frequency + 50, curated):
            typos += 1
            continue
        news[word] = frequency
        room -= 1
    print(f"HeliBoard: {typos} likely typos left out")

    # Everyday counts from Tatoeba, case-normalized at sentence start.
    unigrams = collections.Counter()
    bigrams = collections.Counter()
    starts = collections.Counter()
    ends = collections.Counter()
    sentences = []
    held = []
    with bz2.open(args.tatoeba, "rt", encoding="utf-8") as source:
        for line in source:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            if int(parts[0]) % 50 == 0:
                held.append(parts[2])
                continue
            sentences.append(parts[2])
    lower = collections.Counter()
    for text in sentences:
        for runs in tokenize(text):
            for i, run in enumerate(runs):
                for j, token in enumerate(run):
                    if i or j:
                        lower[token] += 1

    def normalize(token, initial):
        if not initial:
            return token
        folded = token[0].lower() + token[1:]
        if folded != token and lower[folded] >= lower[token]:
            return folded
        return token

    for text in sentences:
        for runs in tokenize(text):
            final = None
            for i, run in enumerate(runs):
                previous = None
                for j, raw in enumerate(run):
                    token = normalize(raw, i == 0 and j == 0)
                    if token not in news:
                        if not valid_word(token) or len(token) < 2 or is_name(token):
                            previous = None
                            continue
                    unigrams[token] += 1
                    if i == 0 and j == 0:
                        starts[token] += 1
                    elif previous:
                        bigrams[(previous, token)] += 1
                    previous = token
                final = previous if run else None
            if final:
                ends[final] += 1
    # Everyday words missing from both lists, seen often and not typos.
    seen = {}
    for token, count in unigrams.items():
        seen[token.lower()] = max(seen.get(token.lower(), 0), count)
    for word, frequency in news.items():
        if frequency >= 100:
            seen[word.lower()] = max(seen.get(word.lower(), 0), 10 ** 9)
    added = typos = 0
    for token, count in sorted(unigrams.items()):
        if token in news or count < 10 or not valid_word(token):
            continue
        if looks_like_typo(token, count * 10, seen):
            typos += 1
            continue
        news[token] = 0
        added += 1
    print(f"Tatoeba: {added} words added, {typos} likely typos left out")

    # Unigram probability: news frequency mapped onto Tatoeba's scale by a
    # least-squares fit on common words, mixed half and half with Tatoeba.
    total = sum(unigrams.values())
    pairs = [(news[w], math.log2(c / total)) for w, c in unigrams.items() if c >= 20 and w in news and news[w] > 0 and not is_name(w)]
    n = len(pairs)
    mean_f = sum(f for f, _ in pairs) / n
    mean_l = sum(l for _, l in pairs) / n
    slope = sum((f - mean_f) * (l - mean_l) for f, l in pairs) / sum((f - mean_f) ** 2 for f, _ in pairs)
    intercept = mean_l - slope * mean_f
    print(f"news frequency fit: log2 p = {slope:.4f} * f + {intercept:.2f} over {n} words")
    floor = 2.0 ** (intercept)  # f = 0
    probability = {}
    for word, frequency in news.items():
        from_news = 2.0 ** (slope * frequency + intercept) if frequency > 0 else floor / 4
        from_tatoeba = unigrams[word] / total
        # Tatoeba over-represents a few names (Tom, Mary): names use news only.
        probability[word] = from_news if is_name(word) else 0.5 * from_news + 0.5 * from_tatoeba
    norm = sum(probability.values())
    order = sorted(probability, key=lambda w: (-probability[w], w))
    index = {w: i for i, w in enumerate(order)}
    unigram = {w: probability[w] / norm for w in order}

    # Bigrams: Witten-Bell over Tatoeba, plus HeliBoard's news bigrams for
    # pairs Tatoeba never saw. Names are left to the unigram backoff.
    followers = collections.defaultdict(dict)
    for (first, second), count in bigrams.items():
        followers[first][second] = count
    followers[None] = dict(starts)
    lists = []
    for first in [None] + order:
        seen = followers.get(first, {})
        count = sum(seen.values())
        types = len(seen)
        if not count:
            continue
        backoff = types / (count + types)
        # Common words keep more followers: "j'ai faim" is far down j'ai's list.
        limit = min(512, args.followers + count // 300)
        kept = sorted(((c, w) for w, c in seen.items() if c >= 2 and w in index and not is_name(w)), reverse=True)[:limit]
        entries = {w: (c + types * unigram[w]) / (count + types) for c, w in kept}
        lists.append((first, backoff, entries))
    by_first = {first: entries for first, _, entries in lists}
    added = 0
    for first, second, rank in heli_bigrams:
        if first in index and second in index and not is_name(second):
            entries = by_first.get(first)
            if entries is None:
                entries = {}
                by_first[first] = entries
                lists.append((first, 0.5, entries))
            if second not in entries:
                entries[second] = min(0.05, 8 * unigram[second]) / rank
                added += 1

    with open(args.out, "wb") as out:
        out.write(b"JFD1")
        out.write(struct.pack("<IIII", 1, len(order), sum(1 for t in whitelist if whitelist[t] in index), len(lists)))
        for word in order:
            data = word.encode("utf-8")
            out.write(struct.pack("<BBB", cost(unigram[word]), 1 if word in offensive else 0, len(data)))
            out.write(data)
        for typed in sorted(whitelist):
            target = whitelist[typed]
            if target in index:
                data = typed.encode("utf-8")
                out.write(struct.pack("<B", len(data)) + data + varint(index[target]))
        lists.sort(key=lambda item: -1 if item[0] is None else index[item[0]])
        written = 0
        for first, backoff, entries in lists:
            out.write(varint(0 if first is None else index[first] + 1))
            out.write(struct.pack("<B", cost(backoff)))
            items = sorted((index[w], p) for w, p in entries.items())
            out.write(varint(len(items)))
            last = 0
            for target, p in items:
                out.write(varint(target - last) + struct.pack("<B", cost(p)))
                last = target
                written += 1
        # How likely each word is to end a sentence ("too." but not "to.").
        sentences_total = sum(ends.values())
        default = sentences_total / total
        end_costs = []
        for word in order:
            if unigrams[word] >= 3:
                end_costs.append((index[word], cost((ends[word] + 2 * default) / (unigrams[word] + 2))))
        out.write(struct.pack("<BI", cost(default), len(end_costs)))
        last = 0
        for target, value in end_costs:
            out.write(varint(target - last) + struct.pack("<B", value))
            last = target
    print(f"{args.out}: {len(order)} words, {len(whitelist)} whitelist, {len(lists)} lists, {written} bigrams ({added} from HeliBoard), {total} tokens")
    if args.holdout:
        with open(args.holdout, "w", encoding="utf-8") as out:
            out.write("\n".join(held) + "\n")


if __name__ == "__main__":
    main()
