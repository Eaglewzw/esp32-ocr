# Font assets

Only `.ttf` files in this directory are packed into the `storage` partition.

- `cjk_full.ttf`: Droid Sans Fallback Full, Apache-2.0. It supplies the broad
  CJK coverage used as the primary font.
- `demo_fallback.ttf`: a small Noto Sans CJK SC subset, SIL Open Font License
  1.1. It supplies ASCII, `¥`, `·`, `←`, and the known demo/UI glyphs that are
  absent from Droid Sans Fallback Full.

The corresponding license texts are kept beside the fonts. See
`docs/font_flash_mmap.md` for the build, flashing, fallback, and replacement
procedure.
