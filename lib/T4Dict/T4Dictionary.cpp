#include "T4Dictionary.h"

#include <Logging.h>
#include <Memory.h>

T4Dictionary::T4Dictionary() = default;

T4Dictionary::~T4Dictionary() {
  close();
}

T4Dictionary::T4Dictionary(T4Dictionary&& other) noexcept
    : _file(std::move(other._file))
    , _currentNode(other._currentNode)
    , _candidateBuf(std::move(other._candidateBuf))
    , _candidateBufSize(other._candidateBufSize)
    , _candidateCount(other._candidateCount)
    , _loaded(other._loaded) {
  memcpy(_langCode, other._langCode, sizeof(_langCode));
  other._loaded = false;
  other._candidateCount = 0;
  other._candidateBufSize = 0;
  other._langCode[0] = '\0';
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
  }
  return *this;
}

bool T4Dictionary::loadFromSD(const char* path) {
  close();  // ensure clean state

  if (!Storage.openFileForRead("T4", path, _file)) {
    LOG_ERR("T4", "Failed to open dictionary: %s", path);
    return false;
  }

  // Read header
  uint8_t headerBuf[t4::T4_TRIE_HEADER_SIZE];
  if (_file.read(headerBuf, sizeof(headerBuf)) !=
      static_cast<int>(sizeof(headerBuf))) {
    LOG_ERR("T4", "Failed to read header from %s", path);
    close();
    return false;
  }

  if (!t4::validateTrieHeader(headerBuf, sizeof(headerBuf))) {
    t4::T4TrieHeader hdr;
    memcpy(&hdr, headerBuf, sizeof(hdr));
    LOG_ERR("T4", "Invalid dictionary: magic=0x%08X version=%u",
            hdr.magic, hdr.version);
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
  LOG_INF("T4", "Loaded dictionary: %s (%s, %u words, %u nodes)",
          path, _langCode, hdr.word_count, hdr.node_count);
  return true;
}

bool T4Dictionary::pressButton(uint8_t btn) {
  if (!_loaded || btn < 1 || btn > 4) return false;

  // Clear previous candidates — new node means new candidates
  freeCandidates();

  uint8_t idx = btn - 1;
  uint32_t childOff = _currentNode.child_offset[idx];
  if (childOff == t4::T4_TRIE_NULL_OFFSET) {
    return false;
  }

  return readNodeAtOffset(childOff, _currentNode);
}

uint16_t T4Dictionary::getCandidateCount() const {
  return _candidateCount;
}

bool T4Dictionary::loadCandidates() {
  if (!_loaded || _currentNode.word_count == 0) {
    _candidateCount = 0;
    return _currentNode.word_count == 0;  // success if no words (intermediate node)
  }

  freeCandidates();

  // Allocate candidate buffer
  size_t bufSize = DEFAULT_CANDIDATE_BUF_SIZE;
  _candidateBuf = makeUniqueNoThrow<char[]>(bufSize);
  if (!_candidateBuf) {
    LOG_ERR("T4", "OOM: candidate buffer %d bytes", bufSize);
    return false;
  }
  _candidateBufSize = bufSize;

  // Read words from String Pool
  _file.seekSet(_currentNode.str_offset);
  size_t bytesRead = 0;
  uint16_t wordsRead = 0;
  char ch;
  while (wordsRead < _currentNode.word_count && bytesRead < bufSize - 1) {
    int r = _file.read();
    if (r < 0) {
      LOG_ERR("T4", "Read error at word %u", wordsRead);
      freeCandidates();
      return false;
    }
    ch = static_cast<char>(r);
    if (ch == '\0') {
      _candidateBuf[bytesRead++] = '\0';
      ++wordsRead;
    } else {
      _candidateBuf[bytesRead++] = ch;
    }
  }

  _candidateCount = wordsRead;
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
  if (!_loaded) return;
  freeCandidates();
  readNodeAtOffset(t4::T4_TRIE_HEADER_SIZE, _currentNode);
}

void T4Dictionary::close() {
  if (_file.isOpen()) {
    _file.close();
  }
  freeCandidates();
  _loaded = false;
  _langCode[0] = '\0';
}

bool T4Dictionary::isLoaded() const {
  return _loaded;
}

const char* T4Dictionary::getLangCode() const {
  return _langCode;
}

// ── Private helpers ─────────────────────────────────────────────────────

bool T4Dictionary::readNodeAtOffset(uint32_t offset,
                                     t4::T4TrieNode& out) {
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

void T4Dictionary::freeCandidates() {
  _candidateBuf.reset();
  _candidateBufSize = 0;
  _candidateCount = 0;
}
