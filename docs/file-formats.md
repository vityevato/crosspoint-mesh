# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 7

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 7
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

### Version 25

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 25 includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs used by KOReader sync page refinement
- optional per-word Focus Reading split metadata
- per-page footnote entries

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 25
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
    String words[wordCount];
    s16 wordXPos[wordCount];
    WordStyle wordStyle[wordCount];

    u8 hasFocus;
    if (hasFocus != 0) {
        u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
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
│   │   ├── ch_<N>/           # Channel message threads (N = 0–7)
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

### `contacts.bin` — version 1

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (`MESHCORE_CONTACT_FILE_VERSION = 1`) |
| 1 | 1 | Count (`uint8_t`) |
| 2+ | 106× | `MeshCoreContact` records |

**`MeshCoreContact`** (106 bytes):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 32 | `publicKey` — Ed25519 public key |
| 32 | 64 | `name` — UTF-8 display name |
| 96 | 1 | `type` — `MeshNodeType` enum |
| 97 | 4 | `lastSeen` — Unix timestamp (`uint32_t`) |
| 101 | 1 | `pathLength` — LoRa hop count |
| 102 | 1 | `snr` — signal-to-noise ratio (`int8_t`) |
| 103 | 1 | `isSaved` — bool |
| 104 | 2 | `unreadCount` — `uint16_t` |

### `unread.bin` — version 1

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (hardcoded `1`) |
| 1 | 1 | Channel count (`uint8_t`) |
| 2 | 2× | Per-channel unread counts (`uint16_t`) |
| N+1 | 1 | Contact count (`uint8_t`) |
| N+2 | 8× | Per-contact: 6-byte pubkey prefix + `uint16_t` unread |

### `meta.bin` — version 1

Per-conversation metadata file. Located at `conv/ch_<N>/meta.bin` and
`conv/dm_<hexprefix>/meta.bin`.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Version (`META_FILE_VERSION = 1`) |
| 1 | 2 | `count` — number of messages (`uint16_t`) |
| 3 | 4 | `startId` — id of the oldest message (`uint32_t`) |
| 7 | 4 | `endId` — id of the newest message (`uint32_t`) |
| 11 | 4 | `positionId` — id of last viewed message (scroll restore) |

Total: 15 bytes.

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
`/.crosspoint/dicts/user_words.bin`. Written by `T4EntryActivity` on exit
when it changed, and only for `InputType::Text` fields — password and URL
entry never reads or writes it.

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

