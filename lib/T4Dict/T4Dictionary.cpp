#include "T4Dictionary.h"

#include <Logging.h>
#include <Memory.h>

#include <cstring>

T4Dictionary::T4Dictionary() { LOG_DBG("T4", "Constructor"); }

T4Dictionary::~T4Dictionary() {
  LOG_DBG("T4", "Destructor");
  close();
}

T4Dictionary::T4Dictionary(T4Dictionary&& other) noexcept
    : _file(std::move(other._file)),
      _currentNode(other._currentNode),
      _candidateBuf(std::move(other._candidateBuf)),
      _candidateBufSize(other._candidateBufSize),
      _candidateCount(other._candidateCount),
      _loaded(other._loaded) {
  memcpy(_langCode, other._langCode, sizeof(_langCode));
  other._loaded = false;
  other._candidateCount = 0;
  other._candidateBufSize = 0;
  other._langCode[0] = '\0';
  LOG_DBG("T4", "Move constructor");
}

T4Dictionary& T4Dictionary::operator=(T4Dictionary&& other) noexcept {
  if (this != &other) {
    close();
    _file = std::move(other._file);
    _currentNode = other._currentNode;
    _candidateBuf = std::move(other._candidateBuf);
    _candidateBufSize = other._candidateBufSize;
    _candidateCount = other._candidateCount;
    _loaded = other._loaded;
    memcpy(_langCode, other._langCode, sizeof(_langCode));
    other._loaded = false;
    other._candidateCount = 0;
    other._candidateBufSize = 0;
    other._langCode[0] = '\0';
    LOG_DBG("T4", "Move assignment");
  }
  return *this;
}

bool T4Dictionary::loadFromSD(const char* path) {
  LOG_DBG("T4", "loadFromSD: %s", path);
  close();  // ensure clean state

  if (!Storage.openFileForRead("T4", path, _file)) {
    LOG_ERR("T4", "Failed to open dictionary: %s", path);
    return false;
  }

  // Read header
  uint8_t headerBuf[t4::T4_TRIE_HEADER_SIZE];
  if (_file.read(headerBuf, sizeof(headerBuf)) != static_cast<int>(sizeof(headerBuf))) {
    LOG_ERR("T4", "Failed to read header from %s", path);
    close();
    return false;
  }

  if (!t4::validateTrieHeader(headerBuf, sizeof(headerBuf))) {
    t4::T4TrieHeader hdr;
    memcpy(&hdr, headerBuf, sizeof(hdr));
    LOG_ERR("T4", "Invalid dictionary: magic=0x%08X version=%u", hdr.magic, hdr.version);
    close();
    return false;
  }

  t4::T4TrieHeader hdr;
  memcpy(&hdr, headerBuf, sizeof(hdr));
  _langCode[0] = static_cast<char>((hdr.lang_code >> 8) & 0xFF);
  _langCode[1] = static_cast<char>(hdr.lang_code & 0xFF);
  _langCode[2] = '\0';

  // Read root node (at offset HEADER_SIZE = 16)
  if (!readNodeAtOffset(t4::T4_TRIE_HEADER_SIZE, _currentNode)) {
    LOG_ERR("T4", "Failed to read root node");
    close();
    return false;
  }

  _loaded = true;
  LOG_INF("T4", "Loaded dictionary: %s (%s, %u words, %u nodes)", path, _langCode, hdr.word_count, hdr.node_count);
  return true;
}

bool T4Dictionary::pressButton(uint8_t btn) {
  LOG_DBG("T4", "pressButton: btn=%u (loaded=%d)", btn, _loaded);
  if (!_loaded || btn < 1 || btn > 4) {
    LOG_DBG("T4", "pressButton: rejected (loaded=%d btn=%u)", _loaded, btn);
    return false;
  }

  // Clear previous candidates — new node means new candidates
  clearCandidates();

  uint8_t idx = btn - 1;
  uint32_t childOff = _currentNode.child_offset[idx];
  if (childOff == t4::T4_TRIE_NULL_OFFSET) {
    LOG_DBG("T4", "pressButton: btn=%u dead end (no child)", btn);
    return false;
  }

  bool ok = readNodeAtOffset(childOff, _currentNode);
  LOG_DBG("T4", "pressButton: btn=%u -> node@0x%08X word_count=%u (%s)", btn, childOff, _currentNode.word_count,
          ok ? "ok" : "fail");
  return ok;
}

uint16_t T4Dictionary::getCandidateCount() const {
  LOG_DBG("T4", "getCandidateCount: %u", _candidateCount);
  return _candidateCount;
}

bool T4Dictionary::loadCandidates() {
  LOG_DBG("T4", "loadCandidates: word_count=%u str_offset=0x%08X", _currentNode.word_count, _currentNode.str_offset);
  if (!_loaded || _currentNode.word_count == 0) {
    _candidateCount = 0;
    return _currentNode.word_count == 0;  // success if no words (intermediate node)
  }

  // Pass 1: measure the exact String Pool span for this node's words. Sizing
  // the allocation to the real need (typically well under 1 KB) is what keeps
  // candidate lookup alive on the BLE-fragmented MeshCore heap, where the old
  // fixed 4096-byte request failed outright. _bufferBudget caps the span when
  // the activity has to run inside a smaller heap window.
  if (!_file.seekSet(_currentNode.str_offset)) {
    LOG_ERR("T4", "Seek to offset %u failed", _currentNode.str_offset);
    _candidateCount = 0;
    return false;
  }
  bool readError = false;
  auto readByte = [this, &readError]() -> int {
    const int value = _file.read();
    if (value < 0) readError = true;
    return value;
  };
  uint16_t measuredWords = 0;
  const size_t needed = t4::measureCandidateSpan(readByte, _currentNode.word_count, _bufferBudget, measuredWords);
  if (readError) {
    LOG_ERR("T4", "Read error while measuring candidates");
    _candidateCount = 0;
    return false;
  }
  if (needed == 0) {
    LOG_DBG("T4", "loadCandidates: no complete word within %u bytes", static_cast<unsigned>(_bufferBudget));
    _candidateCount = 0;
    return true;
  }

  // Grow (never shrink) the buffer to the measured span. On OOM retry with
  // half the size so a truncated candidate list still beats no list at all;
  // an existing smaller buffer is reused as-is in that case.
  if (_candidateBufSize < needed) {
    size_t want = needed;
    std::unique_ptr<char[]> grown;
    while (want > 0) {
      grown = makeUniqueNoThrow<char[]>(want);
      if (grown) break;
      want = (want > kMinCandidateBytes) ? want / 2 : 0;
    }
    if (grown) {
      _candidateBuf = std::move(grown);
      _candidateBufSize = want;
    } else if (!_candidateBuf) {
      LOG_ERR("T4", "OOM: candidate buffer (need %u bytes)", static_cast<unsigned>(needed));
      _candidateCount = 0;
      return false;
    }
  }

  // Pass 2: read the words into the buffer.
  if (!_file.seekSet(_currentNode.str_offset)) {
    LOG_ERR("T4", "Seek to offset %u failed", _currentNode.str_offset);
    _candidateCount = 0;
    return false;
  }
  readError = false;
  _candidateCount = t4::readCandidateSpan(readByte, _currentNode.word_count, _candidateBuf.get(), _candidateBufSize);
  if (readError) {
    LOG_ERR("T4", "Read error at word %u", _candidateCount);
    _candidateCount = 0;
    return false;
  }
  LOG_DBG("T4", "loadCandidates: read %u/%u words (measured %u words, need %u bytes, capacity %u)", _candidateCount,
          _currentNode.word_count, measuredWords, static_cast<unsigned>(needed),
          static_cast<unsigned>(_candidateBufSize));

  if (_candidateCount < _currentNode.word_count) {
    // The node has more words than the budget/heap could hold. Words keep
    // their frequency order, so the best candidates survive; surface the
    // truncation instead of degrading silently (93/598 was invisible in DBG).
    LOG_INF("T4", "Candidate list truncated: %u/%u words (span %u bytes, buffer %u bytes)", _candidateCount,
            _currentNode.word_count, static_cast<unsigned>(needed), static_cast<unsigned>(_candidateBufSize));
  }

#if LOG_LEVEL >= 2
  // Log candidate words for debugging (compiled out in release)
  if (_candidateCount > 0 && _candidateBuf) {
    char summary[256];
    size_t pos = 0;
    const char* p = _candidateBuf.get();
    uint16_t shown = 0;
    while (shown < _candidateCount && pos < sizeof(summary) - 4) {
      size_t len = strlen(p);
      if (pos > 0) {
        summary[pos++] = ',';
        summary[pos++] = ' ';
      }
      size_t cp = (pos + len < sizeof(summary) - 1) ? len : (sizeof(summary) - pos - 1);
      memcpy(summary + pos, p, cp);
      pos += cp;
      p += len + 1;
      ++shown;
    }
    summary[pos] = '\0';
    LOG_DBG("T4", "candidates[%u]: %s", _candidateCount, summary);
  }
#endif

  return true;
}

const char* T4Dictionary::getCandidate(uint16_t index) const {
  if (!_candidateBuf || index >= _candidateCount) return nullptr;

  // Walk null-terminated words
  const char* p = _candidateBuf.get();
  for (uint16_t i = 0; i < index; ++i) {
    while (*p != '\0') ++p;
    ++p;  // skip null
  }
  return p;
}

void T4Dictionary::reset() {
  LOG_DBG("T4", "reset (loaded=%d)", _loaded);
  if (!_loaded) return;
  clearCandidates();
  readNodeAtOffset(t4::T4_TRIE_HEADER_SIZE, _currentNode);
}

void T4Dictionary::close() {
  LOG_DBG("T4", "close (loaded=%d)", _loaded);
  if (_file.isOpen()) {
    _file.close();
  }
  freeCandidates();
  _loaded = false;
  _langCode[0] = '\0';
}

void T4Dictionary::setBufferBudget(size_t maxBytes) {
  if (maxBytes < kMinCandidateBytes) maxBytes = kMinCandidateBytes;
  if (maxBytes > kMaxCandidateBytes) maxBytes = kMaxCandidateBytes;
  _bufferBudget = maxBytes;
}

bool T4Dictionary::isLoaded() const { return _loaded; }

const char* T4Dictionary::getLangCode() const { return _langCode; }

// ── Private helpers ─────────────────────────────────────────────────────

bool T4Dictionary::readNodeAtOffset(uint32_t offset, t4::T4TrieNode& out) {
  LOG_DBG("T4", "readNodeAtOffset: 0x%08X", offset);
  if (!_file.seekSet(offset)) {
    LOG_ERR("T4", "Seek to offset %u failed", offset);
    return false;
  }
  uint8_t buf[t4::T4_TRIE_NODE_SIZE];
  if (_file.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) {
    LOG_ERR("T4", "Read node at offset %u failed", offset);
    return false;
  }
  memcpy(&out, buf, sizeof(out));
  return true;
}

void T4Dictionary::clearCandidates() {
  LOG_DBG("T4", "clearCandidates (was %u words, capacity %u)", _candidateCount, _candidateBufSize);
  _candidateCount = 0;
}

void T4Dictionary::freeCandidates() {
  LOG_DBG("T4", "freeCandidates (was %u words, capacity %u)", _candidateCount, _candidateBufSize);
  _candidateBuf.reset();
  _candidateBufSize = 0;
  _candidateCount = 0;
}
