#!/usr/bin/env python3
"""Compile a word-frequency list into a T4 predictive-text binary trie (.trie)."""
import argparse
import struct
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# ── Constants ───────────────────────────────────────────────────────────────

HEADER_SIZE = 16
NODE_SIZE = 22
MAGIC = 0x54347269
VERSION = 1
NULL_OFFSET = 0xFFFFFFFF

# Letter groupings: button 1-4 (0-indexed: 0-3)
GROUPINGS = {
    "en": ["abcdef", "ghijkl", "mnopqrs", "tuvwxyz"],
    "ru": ["абвгдеёж", "зийклмно", "прстуфхц", "чшщъыьэюя"],
}

# ── Letter mapping ──────────────────────────────────────────────────────────


def build_letter_map(groups: List[str]) -> Dict[str, int]:
    """Build a mapping from letter → button index (0-3)."""
    letter_map = {}
    for btn_idx, group in enumerate(groups):
        for ch in group:
            letter_map[ch] = btn_idx
    return letter_map


def word_to_sequence(word: str, letter_map: Dict[str, int]) -> List[int]:
    """Convert a word to a button-press sequence. Returns empty list if
    any character is not in the letter map."""
    seq = []
    for ch in word:
        if ch not in letter_map:
            return []
        seq.append(letter_map[ch])
    return seq


# ── Argument parsing ────────────────────────────────────────────────────────


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile word-frequency list to T4 .trie binary"
    )
    parser.add_argument(
        "--lang", required=True, choices=["en", "ru"],
        help="Language code"
    )
    parser.add_argument(
        "--input", required=True, type=Path,
        help="Path to word-frequency text file"
    )
    parser.add_argument(
        "--output", required=True, type=Path,
        help="Path to output .trie file"
    )
    return parser.parse_args()


# ── Input parsing ───────────────────────────────────────────────────────────


def parse_input(filepath: Path) -> List[Tuple[str, int]]:
    """Parse word-frequency file. Returns list of (word, freq) sorted by
    descending frequency. Deduplicates: keeps highest frequency per word.
    Skips invalid lines with a warning."""
    seen: Dict[str, int] = {}  # word → highest frequency seen
    with open(filepath, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                print(
                    f"Warning: line {lineno}: missing frequency, skipping",
                    file=sys.stderr,
                )
                continue
            word = parts[0].lower()
            if not word:
                continue
            try:
                freq = int(parts[1])
            except ValueError:
                print(
                    f"Warning: line {lineno}: invalid frequency, skipping",
                    file=sys.stderr,
                )
                continue
            if freq < 1:
                print(
                    f"Warning: line {lineno}: non-positive frequency, skipping",
                    file=sys.stderr,
                )
                continue
            # Keep highest frequency for duplicates
            if word not in seen or freq > seen[word]:
                seen[word] = freq
    # Convert to sorted list
    entries = [(w, f) for w, f in seen.items()]
    entries.sort(key=lambda x: (-x[1], x[0]))
    return entries


# ── Trie building ───────────────────────────────────────────────────────────


class TrieNode:
    """In-memory trie node. One node per unique button sequence prefix."""
    __slots__ = ("children", "words")  # memory-efficient

    def __init__(self):
        self.children: Dict[int, "TrieNode"] = {}  # button(0-3) → child node
        self.words: List[str] = []  # words ending at this node (frequency order)


def build_trie(
    entries: List[Tuple[str, int]], letter_map: Dict[str, int]
) -> Tuple[TrieNode, int]:
    """Build a trie from word-frequency entries. Returns (root_node, actual_word_count).
    Node count is computed later by assign_offsets() during serialization."""
    root = TrieNode()
    actual_count = 0

    for word, freq in entries:
        seq = word_to_sequence(word, letter_map)
        if not seq:
            print(
                f"Warning: word '{word}' contains unmapped characters, skipping",
                file=sys.stderr,
            )
            continue
        node = root
        for btn in seq:
            if btn not in node.children:
                node.children[btn] = TrieNode()
            node = node.children[btn]
        node.words.append(word)
        actual_count += 1

    return root, actual_count


# ── Serialization ───────────────────────────────────────────────────────────


def assign_offsets(root: TrieNode) -> Tuple[List[TrieNode], Dict[int, int]]:
    """DFS traversal assigning each node an index. Returns (node_list, index_map).
    index_map: id(node) → index (0 = root)."""
    nodes: List[TrieNode] = []
    index_map: Dict[int, int] = {}

    def dfs(node: TrieNode) -> None:
        idx = len(nodes)
        nodes.append(node)
        index_map[id(node)] = idx
        for btn in sorted(node.children.keys()):  # deterministic order
            dfs(node.children[btn])

    dfs(root)
    return nodes, index_map


def build_string_pool(nodes: List[TrieNode]) -> Tuple[bytes, Dict[int, int]]:
    """Build the string pool and return (pool_bytes, str_offset_map).
    str_offset_map: id(node) → offset within string pool."""
    pool_parts: List[bytes] = []
    offset_map: Dict[int, int] = {}
    running_len: int = 0  # incremental counter, avoiding O(N²) repeated joins

    for node in nodes:
        if node.words:
            offset_map[id(node)] = running_len
            for word in node.words:
                encoded = word.encode("utf-8") + b"\0"
                pool_parts.append(encoded)
                running_len += len(encoded)
        else:
            offset_map[id(node)] = 0  # unused for nodes without words

    return b"".join(pool_parts), offset_map


def serialize(
    root: TrieNode, lang_code: str, word_count: int, output_path: Path
) -> None:
    """Serialize trie to .trie binary file."""
    nodes, index_map = assign_offsets(root)
    node_count = len(nodes)
    string_pool, str_offsets = build_string_pool(nodes)

    # Compute absolute file offsets
    string_pool_start = HEADER_SIZE + node_count * NODE_SIZE
    child_offsets_map: Dict[int, List[int]] = {}
    for i, node in enumerate(nodes):
        offs = [NULL_OFFSET] * 4
        for btn, child in node.children.items():
            child_idx = index_map[id(child)]
            offs[btn] = HEADER_SIZE + child_idx * NODE_SIZE
        child_offsets_map[i] = offs

    with open(output_path, "wb") as f:
        # Header
        lang_bytes = lang_code.encode("ascii")
        assert len(lang_bytes) == 2, "lang_code must be exactly 2 ASCII chars"
        header = struct.pack(
            "<IHHII",
            MAGIC,
            VERSION,
            (lang_bytes[0] << 8) | lang_bytes[1],  # pack as big-endian 2-byte pair
            word_count,
            node_count,
        )
        f.write(header)

        # Node Pool
        for i, node in enumerate(nodes):
            child_offs = child_offsets_map[i]
            str_off = str_offsets[id(node)]
            if node.words:
                str_off += string_pool_start  # make absolute
            node_bytes = struct.pack(
                "<4s4s4s4sHI",
                struct.pack("<I", child_offs[0]),
                struct.pack("<I", child_offs[1]),
                struct.pack("<I", child_offs[2]),
                struct.pack("<I", child_offs[3]),
                len(node.words),
                str_off,
            )
            assert (
                len(node_bytes) == 22
            ), f"Node serialization size mismatch: {len(node_bytes)}"
            f.write(node_bytes)

        # String Pool
        f.write(string_pool)

    print(
        f"Written {output_path}: {node_count} nodes, {word_count} words, "
        f"{output_path.stat().st_size} bytes"
    )


# ── Main ────────────────────────────────────────────────────────────────────


def main() -> None:
    args = parse_args()
    groups = GROUPINGS[args.lang]
    letter_map = build_letter_map(groups)
    print(f"Language: {args.lang}")
    print(f"Groups: {groups}")

    entries = parse_input(args.input)
    print(f"Read {len(entries)} unique words from input")

    root, actual_word_count = build_trie(entries, letter_map)
    print(f"Built trie with {actual_word_count} words")

    serialize(root, args.lang, actual_word_count, args.output)


if __name__ == "__main__":
    main()
