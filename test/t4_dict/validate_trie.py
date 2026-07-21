#!/usr/bin/env python3
"""Validate a T4 .trie binary file: check header, structure, and query accuracy."""
import struct
import sys
from pathlib import Path

MAGIC = 0x54347269
VERSION = 1
HEADER_SIZE = 16
NODE_SIZE = 22
NULL_OFFSET = 0xFFFFFFFF

# Letter groupings matching build_t4_dict.py
GROUPINGS = {
    "en": ["abcdef'", "ghijkl-", "mnopqrs", "tuvwxyz"],
    "ru": ["абвгдеёж-", "зийклмно", "прстуфхц", "чшщъыьэюя"],
}


def build_letter_map(lang: str):
    groups = GROUPINGS[lang]
    return {ch: i for i, g in enumerate(groups) for ch in g}


def word_to_sequence(word: str, letter_map: dict) -> list:
    return [letter_map[ch] for ch in word]


def read_trie(path: Path):
    """Read and validate a .trie file. Returns (nodes, string_pool, lang, word_count)."""
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError(f"File too small: {len(data)} bytes")

    magic, ver, lang_raw, word_count, node_count = struct.unpack_from(
        "<IHHII", data, 0
    )
    assert magic == MAGIC, f"Bad magic: {hex(magic)}, expected {hex(MAGIC)}"
    assert ver == VERSION, f"Bad version: {ver}, expected {VERSION}"
    assert node_count > 0, "node_count must be > 0"
    assert word_count > 0, "word_count must be > 0"

    lang_hi = (lang_raw >> 8) & 0xFF
    lang_lo = lang_raw & 0xFF
    lang = chr(lang_hi) + chr(lang_lo)
    assert lang in GROUPINGS, f"Unknown lang: {lang}"

    # Read node pool
    nodes = []
    for i in range(node_count):
        off = HEADER_SIZE + i * NODE_SIZE
        raw = data[off : off + NODE_SIZE]
        child0, child1, child2, child3 = struct.unpack_from("<IIII", raw, 0)
        wc, str_off = struct.unpack_from("<HI", raw, 16)
        nodes.append(
            {
                "children": [child0, child1, child2, child3],
                "word_count": wc,
                "str_offset": str_off,
            }
        )

    # Validate child offsets are in range
    node_pool_end = HEADER_SIZE + node_count * NODE_SIZE
    for i, n in enumerate(nodes):
        for btn, off in enumerate(n["children"]):
            if off == NULL_OFFSET:
                continue
            assert HEADER_SIZE <= off < node_pool_end, (
                f"Node {i} child {btn} offset {off} out of range "
                f"[16, {node_pool_end})"
            )
        if n["str_offset"] != 0:
            assert n["str_offset"] >= node_pool_end, (
                f"Node {i} str_offset {n['str_offset']} before pool start "
                f"({node_pool_end})"
            )
            assert n["str_offset"] < len(data), (
                f"Node {i} str_offset {n['str_offset']} beyond EOF ({len(data)})"
            )

    string_pool = data[node_pool_end:]
    return nodes, string_pool, lang, word_count


def read_words_in_pool(
    string_pool: bytes, str_offset: int, word_count: int, node_pool_size: int
) -> list:
    """Read null-terminated words from the string pool segment."""
    idx = str_offset - HEADER_SIZE - node_pool_size
    words = []
    for _ in range(word_count):
        end = string_pool.index(b"\0", idx)
        words.append(string_pool[idx:end].decode("utf-8"))
        idx = end + 1
    return words


def query_trie(nodes, string_pool, node_pool_size, sequence):
    """Walk the trie following the button sequence, return words at final node."""
    node_idx = 0  # start at root
    for btn in sequence:
        child_off = nodes[node_idx]["children"][btn]
        if child_off == NULL_OFFSET:
            return []
        node_idx = (child_off - HEADER_SIZE) // NODE_SIZE

    wc = nodes[node_idx]["word_count"]
    if wc == 0:
        return []
    str_off = nodes[node_idx]["str_offset"]
    return read_words_in_pool(string_pool, str_off, wc, node_pool_size)


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <trie_file>", file=sys.stderr)
        sys.exit(1)

    path = Path(sys.argv[1])
    if not path.exists():
        print(f"FAIL: file not found: {path}", file=sys.stderr)
        sys.exit(1)

    try:
        nodes, string_pool, lang, word_count = read_trie(path)
    except (ValueError, AssertionError) as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)

    node_pool_size = len(nodes) * NODE_SIZE
    letter_map = build_letter_map(lang)

    print(
        f"PASS: header valid — lang={lang}, words={word_count}, "
        f"nodes={len(nodes)}"
    )

    # Test 1: Query known word
    test_word = "hello" if lang == "en" else "привет"
    seq = word_to_sequence(test_word, letter_map)
    candidates = query_trie(nodes, string_pool, node_pool_size, seq)
    if test_word in candidates:
        print(
            f"PASS: '{test_word}' found in candidates (seq={seq}): "
            f"{candidates[:5]}"
        )
    else:
        print(
            f"FAIL: '{test_word}' not found in candidates (seq={seq})",
            file=sys.stderr,
        )
        sys.exit(1)

    # Test 2: Candidates in frequency order
    if len(candidates) >= 2:
        idx0 = candidates.index(test_word) if test_word in candidates else -1
        if idx0 == 0:
            print(f"PASS: '{test_word}' is first candidate (highest frequency)")
        else:
            print(
                f"FAIL: '{test_word}' is candidate #{idx0 + 1}, not first",
                file=sys.stderr,
            )
            sys.exit(1)

    # Test 3: Query impossible sequence
    impossible_seq = [0] * 20  # 20 presses of button 1
    empty = query_trie(nodes, string_pool, node_pool_size, impossible_seq)
    if len(empty) == 0:
        print("PASS: impossible sequence returns empty list")
    else:
        print(
            f"FAIL: impossible sequence returned {len(empty)} candidates",
            file=sys.stderr,
        )
        sys.exit(1)

    print("PASS: all validations passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
