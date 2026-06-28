# Text encoding (TBL) & dialogue

Lagoon's in-game text uses a custom single-byte encoding (with a two-byte
"phrase/dictionary" escape range). Transcribed from the **Data Crystal** TBL
(<https://datacrystal.tcrf.net/wiki/Lagoon_(SNES)/TBL>). Text data lives in
**bank `$04`** (see [rom-map.md](rom-map.md)); the loader is `$02:85F6` and the
name/place string reader is `$02:8810`.

> **Source / confidence.** Transcribed from Data Crystal, not yet re-verified
> against our ROM. Useful for a text/name randomizer or for relocating strings.

## Character map

| Bytes        | Meaning                                   |
|--------------|-------------------------------------------|
| `0x30-0x39`  | digits `0`-`9`                            |
| `0x41-0x5A`  | uppercase `A`-`Z`                         |
| `0x5D-0x76`  | lowercase `a`-`z`                         |
| `0xEF`       | space                                     |

### Punctuation & symbols

| Byte | Char | Byte | Char | Byte | Char |
|------|------|------|------|------|------|
| `0x2A` | `(` | `0x2B` | `)` | `0x2C` | `,` |
| `0x2D` | `-` | `0x2E` | `.` | `0x2F` | `!` |
| `0x3A` | `:` | `0x3B` | `←` (left arrow) | `0x3C` | `=` |
| `0x3D` | `→` (right arrow) | `0x3F` | `?` | `0x5C` | `'` |

### Control codes

| Byte         | Effect                                    |
|--------------|-------------------------------------------|
| `0x07`       | turn off window border                    |
| `0x08`, `0x09` | character ignored                       |
| `0x0A`       | return cursor to beginning of window      |
| `0x0B`, `0x0C` | end of message window                   |
| `0x0D`       | end of line                               |
| `0x0E`       | return cursor to beginning of window      |
| `0x0F`       | end of message series                     |
| `0x10`       | character ignored                         |
| `0x11`, `0x12` | highlight the text that follows         |
| `0x14-0x17`  | character ignored                         |
| `0x29`       | transparent text cell                     |
| `0xFF`       | end of messages                           |

### Two-byte phrase / dictionary codes

| Range            | Meaning                                                  |
|------------------|----------------------------------------------------------|
| `0x013B-0x01A5`  | Dictionary entries — location, item, and character names |

These are how recurring names are stored compactly. **Caveat for a name
randomizer:** Data Crystal's Notes also state that character names are
**hardcoded inline** throughout the dialogue, so a name can appear both via these
dictionary codes *and* spelled out in the text bank — changing a name fully may
require editing every occurrence, not just one dictionary entry.

## Pipeline

```
$02:85F6  dialogue-text loading routine (0x28 bytes)
$02:8810  name/place string reading routine
$7E:5000  decoded dialogue text buffer (in WRAM, see ram-map.md)
```

## See also

- [rom-map.md](rom-map.md) — text bank ranges in bank `$04`, gfx compression.
- [ram-map.md](ram-map.md) — `$7E:5000` text buffer and `$7F` decompression scratch.