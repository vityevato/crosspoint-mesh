# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 10

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 10
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 35

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 35 adds a header offset and a `uint32_t` entry per page for the
visible-text offset LUT. The other section LUTs remain unchanged.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- per-page visible-text offset LUT (zero-based Unicode codepoints in `<body>`)
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs retained for navigation and legacy sync fallback
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 35
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 96

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;
    u32 visibleTextLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }

    if (visibleTextLutOffset != 0) {
	u32 visibleTextOffset[pageCount] @ visibleTextLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## MeshCore storage

MeshCore data is stored under `/.crosspoint/meshcore/`. The directory
structure is companion-scoped: each paired companion device has its own
subdirectory named after the BLE address with colons stripped
(e.g. `c2:0e:d3:71:13:d9` → `c20ed37113d9`).

```
/.crosspoint/meshcore/
├── companion.json            # Root-level: last-used BLE address + type
├── <ble-addr-hex>/           # Per-companion data directory
│   ├── contacts.bin          # Saved contacts
│   ├── unread.bin            # Unread message counters
│   ├── pin.bin               # BLE pairing PIN (4 bytes LE)
│   ├── conv/                 # Conversation storage
│   │   ├── ch_<N>/           # Channel message threads (N = 0–39)
│   │   │   ├── meta.bin
│   │   │   └── msgs/
│   │   │       ├── 1         # MeshCoreMessage (268 bytes), filename = id
│   │   │       ├── 2
│   │   │       └── ...
│   │   └── dm_<hexprefix>/   # Direct message threads (12-char hex)
│   │       ├── meta.bin
│   │       └── msgs/
│   │           └── ...
```

### `companion.json`

Plain text, format: `<BLE_address>:<address_type>`

```
c2:0e:d3:71:13:d9:0
```

The last colon separates the 17-char BLE address from the address type byte
(0 = public, 1 = random). Legacy format (address only, no colon) is also
accepted and treated as type 0.

### `contacts.bin` — version 3

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (`MESHCORE_CONTACT_FILE_VERSION = 3`) |
| 1 | 2 | Count (`uint16_t`, little-endian) |
| 3+ | 80× | `MeshCoreContact` records |

Older versions (1, 2) are rejected (no migration — the format predates
production; contacts are re-fetched from the node on the next full sync).

**`MeshCoreContact`** (80 bytes, natural alignment):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 32 | `publicKey` — Ed25519 public key |
| 32 | 33 | `name` — UTF-8 display name (protocol: max 32 + NUL) |
| 65 | 1 | `type` — `MeshNodeType` enum |
| 66 | 1 | `flags` — bit 0 = favourite |
| 68 | 4 | `lastSeen` — Unix timestamp (`uint32_t`) |
| 72 | 1 | `pathLength` — LoRa hop count |
| 73 | 1 | `snr` — signal-to-noise ratio (`int8_t`) |
| 74 | 1 | `isSaved` — bool |
| 76 | 2 | `unreadCount` — `uint16_t` |

### `unread.bin` — version 2

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (hardcoded `2`) |
| 1 | 2 | Channel count (`uint16_t`) |
| 3 | 2× | Per-channel unread counts (`uint16_t`) |
| N+1 | 2 | Contact count (`uint16_t`) |
| N+3 | 8× | Per-contact: 6-byte pubkey prefix + `uint16_t` unread |

### `meta.bin` — version 3

Per-conversation metadata file. Located at `conv/ch_<N>/meta.bin` and
`conv/dm_<hexprefix>/meta.bin`.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (`META_FILE_VERSION = 3`) |
| 1 | 2 | `count` — number of messages (`uint16_t`) |
| 3 | 4 | `startId` — id of the oldest message (`uint32_t`) |
| 7 | 4 | `endId` — id of the newest message (`uint32_t`) |
| 11 | 4 | `positionId` — id of last viewed message (scroll restore) |
| 15 | 4 | `totalPx` — total pixel height of all messages (`uint32_t`) |
| 19 | 4 | `positionPx` — pixel scroll offset (scroll restore) |
| 23 | 4 | `fontId` — body font id the heights were computed with (`int32_t`) |
| 27 | 4 | `metaFontId` — meta-line font id the heights were computed with (`int32_t`) |

Total: 31 bytes.

Version 2 files (27 bytes, no `metaFontId`) are still read — the field
defaults to 0, which the thread activity detects as a mismatch and uses
to trigger a one-time height rebuild (meta lines are measured with the
system font's line height since v3). Version 1 files are not readable
and are treated as missing.

### `msgs/` — per-file message storage

Each message is stored as a separate file named by its numeric id
(e.g. `msgs/1`, `msgs/2`, ...). The id is a monotonically increasing
`uint32_t`, starting at 1 for the first message of a conversation.
Ids never reset — even after old messages are trimmed at
`MAX_MSGS_PER_THREAD` (200), new messages continue incrementing
from the last `endId`.

Each file contains exactly one `MeshCoreMessage` struct (268 bytes):

**`MeshCoreMessage`** (268 bytes):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `direction` — `MsgDirection` (`RECEIVED=0`/`SENT=1`) |
| 1 | 1 | `type` — `MsgType` (`CHANNEL=0`/`DIRECT=1`) |
| 2 | 6 | `pubkeyPrefix` — first 6 bytes of sender pubkey |
| 8 | 64 | `senderName` — sender display name |
| 72 | 1 | `channelIdx` — channel index (for CHANNEL type) |
| 73 | 4 | `timestamp` — Unix timestamp (`uint32_t`) |
| 77 | 1 | `snr` — signal-to-noise ratio (`int8_t`) |
| 78 | 1 | `pathLength` — LoRa hop count |
| 79 | 1 | `deliveryStatus` — `DeliveryStatus` enum |
| 80 | 4 | `globalId` — monotonic message id (`uint32_t`) |
| 84 | 184 | `text` — message text (`MAX_MSG_TEXT_LEN`) |

### Cache invalidation

Version mismatch on any binary file causes a silent read failure (treated as
empty). The caller is responsible for handling the resulting empty state.
There is no automatic deletion of stale companion directories.

## `user_words.bin`

Learned-word store for T4 predictive input, at
`/t4dicts/user_words.bin`. Written by `T4EntryActivity` on exit
when it changed, and only for `InputType::Text` fields — password, URL, and
digit entry never reads or writes it.

The store serves both purposes of personalization: words missing from the
shipped `.trie` (names, jargon typed in Multi-tap) become predictable, and
words the user types often are ranked ahead of the static frequency order
baked into the `.trie`.

### Version 1

**Header** (12 bytes):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x54345557` (`T4UW`) |
| 4 | 2 | Version (`T4UserLexicon::kVersion = 1`) |
| 6 | 2 | Entry count (`uint16_t`) |
| 8 | 4 | Reserved (0) |

**Entry** (3 + `wordLen` bytes, repeated `entryCount` times):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `lang` — `T4Language` value (never `DIGIT`) |
| 1 | 1 | `score` — usage count, 1…200 |
| 2 | 1 | `wordLen` — word length in bytes, 1…27 |
| 3 | `wordLen` | `word` — lowercase UTF-8, no terminator |

Maximum file size is 12 + 128 × 30 = 3852 bytes (`kMaxEntries` entries).

Notes:

- Button sequences are not stored. They are recomputed at load time from
  the letter groups of the entry's language, so the file survives layout
  changes; entries whose letters no longer map are dropped.
- Scores are aged by halving every entry when any score reaches 200, which
  keeps long-lived favourites from freezing the ranking.
- When the store is full, the entry with the lowest score is evicted.
- A truncated or corrupt file is treated as empty — no version migration
  and no automatic deletion.

