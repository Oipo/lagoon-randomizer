# Camera fix

Hardcoded into the randomizer from `Lagoon camera fix [001].ips`. Applied in
`randomize_rom` (randomizer.hpp) after the CRC check, so it is part of every
output ROM.

## What it changes

The IPS is four single-byte writes. The offsets are **headerless** PC addresses
(they land exactly on immediate operands of real 65816 code; the +0x200 headered
interpretation lands on unrelated bytes), so they apply directly to the
post-copier-header image the randomizer works on.

| PC offset | SNES addr | Instruction        | Original | Patched |
|-----------|-----------|--------------------|----------|---------|
| `0x8408`  | `$01:8408`| `SEC; SBC #$00C0`  | `C0`     | `84`    |
| `0x8432`  | `$01:8432`| `LDA #$0040`       | `40`     | `7C`    |
| `0x845F`  | `$01:845F`| `SEC; SBC #$00B0`  | `B0`     | `84`    |
| `0x8489`  | `$01:8489`| `LDA #$0050`       | `50`     | `7C`    |

Each changed byte is the low byte of a 16-bit immediate (the following byte is
`00` in all four cases). They act as camera scroll-boundary thresholds: two pairs
of `SBC #imm` / `LDA #imm` that the camera code compares against, presumably the
horizontal and vertical bounds. The fix narrows/shifts those thresholds.

## Verification

Running `randomize_rom` on `Lagoon (USA).sfc` produces a ROM byte-identical to
applying the original IPS to the same ROM, with only these four offsets differing
from the unpatched image.
