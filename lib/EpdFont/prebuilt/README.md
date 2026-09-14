# Prebuilt SD Font Packs

Prebuilt `.cpfont` files that ship with the repository so users can install
them without building from source. Each family mirrors the entries in
`lib/EpdFont/scripts/sd-fonts.yaml`.

## Emoji Pack

`Emoji/Emoji_{12,14,16,18}.cpfont` — the monochrome emoji pack used by
MeshCore chats (per-glyph emoji fallback for the reader font in message
threads). Generated from the committed monochrome Noto Emoji TTF
(`lib/EpdFont/builtinFonts/source/NotoEmoji/`) with the curated `emoji`
interval preset.

Installation options (local only — the pack is never offered in the
on-device font download menu):

- **Web interface:** File Transfer → Fonts → upload the files in
  `Emoji/` (family name is derived from the filenames).
- **Manual SD copy:** copy `Emoji/` to `/.fonts/Emoji/` (or
  `/fonts/Emoji/`) on the SD card.

The Emoji pack is a chat fallback, not a reading font — it never appears
in the reader font-family pickers nor in the on-device download list.

Regenerate after changing `sd-fonts.yaml` or `emoji_ranges.py`:

```sh
python3 lib/EpdFont/scripts/build-sd-fonts.py \
    --only Emoji --output-dir lib/EpdFont/prebuilt
```

See `docs/sd-card-fonts.md` and the USER_GUIDE "Emoji in Chats" section.
