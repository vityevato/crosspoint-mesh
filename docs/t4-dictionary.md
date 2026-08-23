# T4 Predictive Text Dictionaries

This document covers the technical side of the **T4 predictive text input**:
how the button-driven keyboard works, the `.trie` dictionary binary format,
how to build a dictionary, and where to place files on the SD card. For
end-user usage, see the [User Guide](./../USER_GUIDE.md) section on text
entry.

## Overview

Text fields on button-only devices (Xteink X3/X4) use the T4 keyboard
(`src/activities/util/T4EntryActivity.h`), a T9-style input method driven by
the physical buttons. Touch devices use an on-screen keyboard instead; the
selection is made in `textentry::makeEntryActivity` based on
`mappedInput.hasTouch()`.

The keyboard has two modes:

- **Predict (T9)** — press each letter group once per word; the keyboard
  suggests words from a dictionary. Available only when a dictionary file
  exists on the SD card for the active language.
- **Multi-tap** — press a group repeatedly to cycle through its letters.
  Always available, even without a dictionary.

The core engine lives in `lib/T4Dict/` (`T4InputEngine`, `T4Dictionary`,
`T4Layout`, `T4UserLexicon`). The dictionary itself is **streamed from the
SD card on demand** — the file stays open and nodes are read by offset, so
the large shipped dictionaries are never loaded into RAM in full.

## SD card layout

Dictionaries and the learned-word store live under `/t4dicts/` on the SD
card root:

```text
/t4dicts/
├── en.trie            # English dictionary (shipped)
├── ru.trie            # Russian dictionary (shipped)
└── user_words.bin     # learned words (auto-created, see below)
```

A dictionary file name must be `<language-code>.trie`, e.g. `en.trie`,
`ru.trie`. `T4Layout::getDictionaryPath()` resolves a language to
`/t4dicts/<code>.trie`. If the file is missing, that language is Multi-tap
only.

## Letter groupings

Four front buttons cover the letters; the side buttons handle backspace and
punctuation. The groupings are defined in `lib/T4Dict/T4Layout.cpp` and
mirrored in `scripts/build_t4_dict.py`.

| Button | English | Russian |
| --- | --- | --- |
| Back (1) | `abcdef'` | `абвгдеёж-` |
| Confirm (2) | `ghijkl-` | `зийклмно` |
| Left (3) | `mnopqrs` | `прстуфхц` |
| Right (4) | `tuvwxyz` | `чшщъыьэюя` |

The keyboard cycles three language slots: **English** (fixed),
**Additional** (configurable, default Russian), and **Digits** (fixed).
The Additional slot is chosen via **Settings → System → Additional Keyboard
Layout** and can be disabled entirely.

## `.trie` binary format

All multi-byte integers are little-endian. The format is defined in
`lib/T4Dict/T4TrieFormat.h` and written by `scripts/build_t4_dict.py`.

**Header** (16 bytes):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `magic` = `0x54347269` |
| 4 | 2 | `version` = 1 |
| 6 | 2 | `lang_code` — ISO 639-1, `(c0 << 8) \| c1` |
| 8 | 4 | `word_count` |
| 12 | 4 | `node_count` |

**Node pool** (`node_count` × 22 bytes). One node per unique button-sequence
prefix; node 0 is the root:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `child_offset[0]` — button 1 |
| 4 | 4 | `child_offset[1]` — button 2 |
| 8 | 4 | `child_offset[2]` — button 3 |
| 12 | 4 | `child_offset[3]` — button 4 |
| 16 | 2 | `word_count` — words ending at this node |
| 18 | 4 | `str_offset` — offset into the string pool |

Each `child_offset` is an absolute file offset to the child node, or
`0xFFFFFFFF` when there is no child for that button.

**String pool** — null-terminated UTF-8 words, in descending frequency order
(index 0 = most frequent). `str_offset` points at the first word of the
node's candidate list; the following `word_count` words are contiguous.

## Building a dictionary

Compile a word-frequency list into a `.trie` with:

```bash
python3 scripts/build_t4_dict.py --lang en --input words.txt --output en.trie
```

- `--lang` — one of `en`, `ru` (see `GROUPINGS` in the script).
- `--input` — a word-frequency text file.
- `--output` — the `.trie` to write; copy it to `/t4dicts/` on the SD card.

**Input format** — one `word frequency` pair per line, whitespace-separated,
frequency a positive integer:

```text
the 231358511
of 131519427
and 129976379
```

Words are lowercased; duplicate words keep the highest frequency. Lines with
missing or invalid frequencies are skipped with a warning. For English, the
script also injects apostrophe contractions (`dont` → `don't`, `im` → `i'm`,
etc.) when the non-apostrophe form is present in the source list, which
matches OpenSubtitles-derived corpora that tokenize contractions away.

Source word lists are available from the
[FrequencyWords](https://github.com/hermitdave/FrequencyWords) repository
(the same source the shipped dictionaries are built from).

## Adding a new language

1. Add an entry to `kAdditionalLayouts` in `lib/T4Dict/T4Layout.cpp`: the
   `code`, `i18nCode`, `dictPath`, four letter-group strings and their
   lengths, and a `SentenceConfig` (auto-capitalisation flag and
   sentence-ending characters).
2. Extend the `kCaseRanges` / `kCaseExceptions` tables in the same file so
   the language's uppercase/lowercase mapping is covered.
3. Add the matching groupings to `GROUPINGS` in
   `scripts/build_t4_dict.py` and allow the code in `--lang`.
4. Build the `.trie` and ship it to `/t4dicts/<code>.trie` on the SD card.

## Learned words (`user_words.bin`)

Words the user types in Multi-tap are learned into `/t4dicts/user_words.bin`
and ranked alongside the static dictionary. See
[file-formats.md](./file-formats.md) for the on-disk format.
