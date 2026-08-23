#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== T4 Dictionary Pipeline Test ==="

# Build English trie
echo "--- Building English dictionary ---"
python3 "$PROJECT_DIR/scripts/build_t4_dict.py" \
  --lang en \
  --input "$PROJECT_DIR/test/t4_dict/en_1000.txt" \
  --output /tmp/en_1000.trie

# Validate English trie
echo "--- Validating English .trie ---"
python3 "$PROJECT_DIR/test/t4_dict/validate_trie.py" /tmp/en_1000.trie

# Build Russian trie
echo "--- Building Russian dictionary ---"
python3 "$PROJECT_DIR/scripts/build_t4_dict.py" \
  --lang ru \
  --input "$PROJECT_DIR/test/t4_dict/ru_1000.txt" \
  --output /tmp/ru_1000.trie

# Validate Russian trie
echo "--- Validating Russian .trie ---"
python3 "$PROJECT_DIR/test/t4_dict/validate_trie.py" /tmp/ru_1000.trie

# Cleanup
rm -f /tmp/en_1000.trie /tmp/ru_1000.trie

echo ""
echo "=== ALL TESTS PASSED ==="
