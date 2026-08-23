#include "T4UserLexicon.h"

#include <cstring>

namespace t4 {

namespace {

/// UTF-8 byte length of the character starting at @p c (1–4).
uint8_t utf8CharLen(char c) {
  const unsigned char c0 = static_cast<unsigned char>(c);
  if ((c0 & 0xE0) == 0xC0) return 2;
  if ((c0 & 0xF0) == 0xE0) return 3;
  if ((c0 & 0xF8) == 0xF0) return 4;
  return 1;
}

/// True when the character belongs to the letter groups of any input
/// language. Used to trim punctuation off a token before learning it.
bool isLetterAnyLanguage(const char* p, uint8_t byteLen) {
  if (buttonForLetter(T4Language::EN, p, byteLen) != 0) return true;
  if (hasActiveAdditionalLayout() && buttonForLetter(T4Language::ADDITIONAL, p, byteLen) != 0) return true;
  return false;
}

/// True when @p token appears in @p haystack delimited by spaces.
bool containsWord(const char* haystack, const char* token, size_t tokenLen) {
  if (!haystack || !haystack[0] || tokenLen == 0) return false;
  const size_t hayLen = strlen(haystack);
  if (tokenLen > hayLen) return false;
  for (size_t i = 0; i + tokenLen <= hayLen; i++) {
    if (memcmp(haystack + i, token, tokenLen) != 0) continue;
    const bool leftOk = (i == 0) || haystack[i - 1] == ' ';
    const bool rightOk = (i + tokenLen == hayLen) || haystack[i + tokenLen] == ' ';
    if (leftOk && rightOk) return true;
  }
  return false;
}

bool isSeparator(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

}  // namespace

// ── Sequence packing ─────────────────────────────────────────────────────

bool T4UserLexicon::packWord(T4Language lang, const char* word, size_t len, uint32_t& outSeq, uint8_t& outSeqLen) {
  if (!word || len == 0 || lang == T4Language::DIGIT) return false;

  uint32_t seq = 0;
  uint8_t chars = 0;
  size_t i = 0;
  while (i < len) {
    const uint8_t byteLen = utf8CharLen(word[i]);
    if (i + byteLen > len) return false;  // truncated UTF-8 sequence
    const uint8_t btn = buttonForLetter(lang, word + i, byteLen);
    if (btn == 0) return false;  // not a letter of this language
    if (chars >= kMaxWordChars) return false;
    seq = (seq << 2) | static_cast<uint32_t>(btn - 1);
    chars++;
    i += byteLen;
  }
  if (chars < kMinWordChars) return false;

  outSeq = seq;
  outSeqLen = chars;
  return true;
}

bool T4UserLexicon::resolveLanguage(T4Language preferredLang, const char* word, uint8_t len, T4Language& outLang,
                                    uint32_t& outSeq, uint8_t& outSeqLen) {
  const T4Language order[] = {preferredLang, T4Language::EN, T4Language::ADDITIONAL};
  for (T4Language lang : order) {
    if (lang == T4Language::DIGIT) continue;
    if (lang == T4Language::ADDITIONAL && !hasActiveAdditionalLayout()) continue;
    if (packWord(lang, word, len, outSeq, outSeqLen)) {
      outLang = lang;
      return true;
    }
  }
  return false;
}

// ── Normalization ────────────────────────────────────────────────────────

bool T4UserLexicon::normalizeWord(const char* word, size_t len, char* out, uint8_t& outLen) {
  if (!word || len == 0 || len > kMaxWordLen) return false;

  size_t in = 0;
  size_t written = 0;
  while (in < len) {
    const uint8_t byteLen = utf8CharLen(word[in]);
    if (in + byteLen > len) return false;  // truncated UTF-8 sequence
    char lower[4];
    const uint8_t lowerLen = lowerLetterUtf8(word + in, byteLen, lower);
    if (lowerLen == 0 || written + lowerLen > kMaxWordLen) return false;
    memcpy(out + written, lower, lowerLen);
    written += lowerLen;
    in += byteLen;
  }
  out[written] = '\0';
  outLen = static_cast<uint8_t>(written);
  return written > 0;
}

// ── Learning ─────────────────────────────────────────────────────────────

bool T4UserLexicon::learnWord(T4Language preferredLang, const char* word, size_t len) {
  char normalized[kMaxWordBytes];
  uint8_t normalizedLen = 0;
  if (!normalizeWord(word, len, normalized, normalizedLen)) return false;

  T4Language lang = T4Language::EN;
  uint32_t seq = 0;
  uint8_t seqLen = 0;
  if (!resolveLanguage(preferredLang, normalized, normalizedLen, lang, seq, seqLen)) return false;

  const int existing = findEntry(static_cast<uint8_t>(lang), normalized, normalizedLen);
  if (existing >= 0) {
    bump(static_cast<uint16_t>(existing));
    return true;
  }
  return insertEntry(lang, seq, seqLen, normalized, normalizedLen);
}

uint16_t T4UserLexicon::learnText(T4Language preferredLang, const char* text, const char* skipText) {
  if (!text) return 0;

  const size_t len = strlen(text);
  uint16_t learned = 0;
  size_t pos = 0;
  while (pos < len && learned < kMaxLearnPerText) {
    while (pos < len && isSeparator(text[pos])) pos++;
    size_t start = pos;
    while (pos < len && !isSeparator(text[pos])) pos++;
    size_t end = pos;

    // Trim leading non-letters (quotes, brackets, …).
    while (start < end) {
      const uint8_t byteLen = utf8CharLen(text[start]);
      if (start + byteLen > end || isLetterAnyLanguage(text + start, byteLen)) break;
      start += byteLen;
    }
    // Trim trailing non-letters (the punctuation appended on confirm).
    size_t lastLetterEnd = start;
    for (size_t scan = start; scan < end;) {
      const uint8_t byteLen = utf8CharLen(text[scan]);
      if (scan + byteLen > end) break;
      if (isLetterAnyLanguage(text + scan, byteLen)) lastLetterEnd = scan + byteLen;
      scan += byteLen;
    }
    end = lastLetterEnd;
    if (end <= start) continue;

    // Words the field started with were not typed by the user.
    if (containsWord(skipText, text + start, end - start)) continue;

    if (learnWord(preferredLang, text + start, end - start)) learned++;
  }
  return learned;
}

// ── Entry management ─────────────────────────────────────────────────────

int T4UserLexicon::findEntry(uint8_t lang, const char* word, uint8_t wordLen) const {
  for (uint16_t i = 0; i < _count; i++) {
    const Entry& e = _entries[i];
    if (e.lang != lang || e.wordLen != wordLen) continue;
    if (memcmp(e.word, word, wordLen) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool T4UserLexicon::insertEntry(T4Language lang, uint32_t seq, uint8_t seqLen, const char* word, uint8_t wordLen) {
  const uint16_t slot = (_count < kMaxEntries) ? _count++ : victimIndex();
  Entry& e = _entries[slot];
  e.seq = seq;
  e.seqLen = seqLen;
  e.score = 1;
  e.lang = static_cast<uint8_t>(lang);
  e.wordLen = wordLen;
  memcpy(e.word, word, wordLen);
  e.word[wordLen] = '\0';
  _dirty = true;
  return true;
}

void T4UserLexicon::bump(uint16_t index) {
  if (index >= _count) return;
  Entry& e = _entries[index];
  if (e.score < kScoreMax) e.score++;
  if (e.score >= kScoreMax) ageAll();
  _dirty = true;
}

void T4UserLexicon::ageAll() {
  for (uint16_t i = 0; i < _count; i++) {
    _entries[i].score = static_cast<uint8_t>(_entries[i].score / 2);
    if (_entries[i].score == 0) _entries[i].score = 1;
  }
}

uint16_t T4UserLexicon::victimIndex() const {
  uint16_t victim = 0;
  for (uint16_t i = 1; i < _count; i++) {
    if (_entries[i].score < _entries[victim].score) victim = i;
  }
  return victim;
}

void T4UserLexicon::clear() {
  _count = 0;
  _dirty = false;
}

// ── Lookup ───────────────────────────────────────────────────────────────

uint8_t T4UserLexicon::findMatches(T4Language lang, const uint8_t* seq, uint8_t seqLen, uint16_t* outEntries,
                                   uint8_t cap) const {
  if (!seq || !outEntries || cap == 0) return 0;
  if (seqLen == 0 || seqLen > kMaxWordChars || lang == T4Language::DIGIT) return 0;

  uint32_t packed = 0;
  for (uint8_t i = 0; i < seqLen; i++) {
    if (seq[i] < 1 || seq[i] > 4) return 0;
    packed = (packed << 2) | static_cast<uint32_t>(seq[i] - 1);
  }

  uint8_t found = 0;
  for (uint16_t i = 0; i < _count; i++) {
    const Entry& e = _entries[i];
    if (e.lang != static_cast<uint8_t>(lang) || e.seqLen != seqLen || e.seq != packed) continue;

    // Insertion sort by score, highest first; drop the weakest on overflow.
    uint8_t pos = (found < cap) ? found : cap;
    while (pos > 0 && _entries[outEntries[pos - 1]].score < e.score) pos--;
    if (pos >= cap) continue;
    const uint8_t last = (found < cap) ? found : static_cast<uint8_t>(cap - 1);
    for (uint8_t k = last; k > pos; k--) outEntries[k] = outEntries[k - 1];
    outEntries[pos] = i;
    if (found < cap) found++;
  }
  return found;
}

const char* T4UserLexicon::getWord(uint16_t entry) const {
  if (entry >= _count) return nullptr;
  return _entries[entry].word;
}

uint8_t T4UserLexicon::getScore(uint16_t entry) const {
  if (entry >= _count) return 0;
  return _entries[entry].score;
}

// ── Serialization ────────────────────────────────────────────────────────

bool T4UserLexicon::loadFromBuffer(const uint8_t* data, size_t len) {
  clear();
  if (!data || len < kHeaderSize) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t entryCount = 0;
  memcpy(&magic, data, sizeof(magic));
  memcpy(&version, data + 4, sizeof(version));
  memcpy(&entryCount, data + 6, sizeof(entryCount));
  if (magic != kMagic || version != kVersion) return false;

  size_t pos = kHeaderSize;
  for (uint16_t i = 0; i < entryCount && _count < kMaxEntries; i++) {
    if (pos + kEntryHeaderSize > len) break;
    const uint8_t lang = data[pos];
    const uint8_t score = data[pos + 1];
    const uint8_t wordLen = data[pos + 2];
    pos += kEntryHeaderSize;
    if (wordLen == 0 || wordLen > kMaxWordLen || pos + wordLen > len) break;

    const char* word = reinterpret_cast<const char*>(data + pos);
    pos += wordLen;

    if (lang >= kLanguageCount || lang == static_cast<uint8_t>(T4Language::DIGIT) || score == 0) continue;

    // Re-derive the sequence so the store survives letter-group changes;
    // entries that no longer map to their language are dropped.
    uint32_t seq = 0;
    uint8_t seqLen = 0;
    if (!packWord(static_cast<T4Language>(lang), word, wordLen, seq, seqLen)) continue;

    Entry& e = _entries[_count++];
    e.seq = seq;
    e.seqLen = seqLen;
    e.score = (score > kScoreMax) ? kScoreMax : score;
    e.lang = lang;
    e.wordLen = wordLen;
    memcpy(e.word, word, wordLen);
    e.word[wordLen] = '\0';
  }

  _dirty = false;
  return true;
}

size_t T4UserLexicon::serialize(uint8_t* out, size_t cap) {
  if (!out) return 0;

  size_t needed = kHeaderSize;
  for (uint16_t i = 0; i < _count; i++) needed += kEntryHeaderSize + _entries[i].wordLen;
  if (cap < needed) return 0;

  const uint32_t magic = kMagic;
  const uint16_t version = kVersion;
  const uint16_t entryCount = _count;
  const uint32_t reserved = 0;
  memcpy(out, &magic, sizeof(magic));
  memcpy(out + 4, &version, sizeof(version));
  memcpy(out + 6, &entryCount, sizeof(entryCount));
  memcpy(out + 8, &reserved, sizeof(reserved));

  size_t pos = kHeaderSize;
  for (uint16_t i = 0; i < _count; i++) {
    const Entry& e = _entries[i];
    out[pos] = e.lang;
    out[pos + 1] = e.score;
    out[pos + 2] = e.wordLen;
    pos += kEntryHeaderSize;
    memcpy(out + pos, e.word, e.wordLen);
    pos += e.wordLen;
  }

  _dirty = false;
  return pos;
}

}  // namespace t4
