# Graphics / animation loading (DMA-based — immune to FastROM & DBR)

How Lagoon gets tiles, sprites, and palette into the PPU, and **why none of it
benefits from FastROM or from pointing the data-bank (DB/DBR) at the fast `$8x`
mirror** (the question raised in
[object-update-loop.md → "Would pointing DB at the fast mirror help?"](object-update-loop.md)).

Short version: **the loaders are DMA**. DMA takes its source bank from an explicit
channel register, not DBR, and the DMA unit runs at a **fixed 8 master cycles/byte
regardless of `$420D`** — so neither the DBR trick nor FastROM can speed graphics
transfers. The CPU-speed knobs only matter for *CPU* loops that read ROM via
absolute addressing, and Lagoon's graphics path has essentially none.

## The two facts that settle it

1. **DMA ignores DBR.** A channel reads its source from the A-bus bank register
   (`$43x4`) + address (`$43x2/3`); DBR governs only *CPU* `abs`/`abs,X`/`abs,Y`.
2. **DMA is fixed-rate.** Every DMA byte costs 8 master cycles (2.68 MHz) no matter
   what `$420D` (MEMSEL/FastROM) is set to. FastROM accelerates CPU fetches only.
   Re-pointing a DMA source at the fast mirror (`$90:…` vs `$10:…`) changes nothing.

## The DMA loaders (kick = `STA $420B` / `MDMAEN`)

| Site (file)        | Ch | B-bus dest        | Source            | Size    | Role |
|--------------------|----|-------------------|-------------------|---------|------|
| `$00:8199` (`0x0199`) | 0 | OAM (`$2104`)    | `$7E:2000` (WRAM shadow) | `$0220` (=544, full OAM) | sprite table upload |
| `$00:8344` (`0x0344`) | 0 | VRAM (`$2118`)   | `$00:8349` (ROM)  | (init)  | VRAM clear/init |
| `$00:8E69` (`0x0E69`) | 0 | CGRAM/VRAM        | `$7E:24xx` (WRAM) | `$0200` (=palette) | palette upload |
| `$00:8F11` (`0x0F11`) | 3 | VRAM (`$2118`)   | `$7E:4000` (WRAM) | `$0480` | tile block from WRAM staging |
| `$00:8F44` (`0x0F44`) | 3 | VRAM (`$2118`)   | **variable** `$0410`/`$0412`/`$0413` | variable | general VRAM uploader (used by the animated-tile loader below) |

The general uploader at `$00:8F44` reads its source **address `$0410`, bank `$0412`,
size `$0413`** from WRAM variables, so the source bank is whatever the requester put
in `$0412` — frequently a ROM bank, streamed ROM→VRAM directly by the DMA unit.

## The animated-tile loader — `$01:DFAC` (file `0x0DFAC`)

This is the "updates every couple of frames" animation: a cycling frame index drives
a per-frame tile DMA.

```
LDA $040A / STA $40 / ASL / ADC $40   ; A = $040A * 3   ($040A = animation frame index)
TAY
REP #$20
LDA $DFE2,X -> $48                      ; $48 = frame-table base (X selects which animation)
LDA $DFE4,X -> $0415
LDA $DFE6,X -> $0413                    ; transfer size
LDA ($48),Y  -> $0410                   ; DMA source ADDRESS  (table[frame*3])
INY / INY
LDA ($48),Y  -> $0412                   ; DMA source BANK     (table[frame*3+2])
LDA #$04 / TSB $023A                    ; request the ch3 VRAM DMA ($00:8F44 fires it)
RTS
```

- `$040A` is the cycling frame index (reset to 0 at the wrap check near `$01:DFA9`).
- `($48)` points at a **3-byte-per-frame source table** (`addr.w, bank.b`). For
  selector `X=0`, `$48 = $01:D9E3`; decoding it: frame 0 = `$10:8000`,
  frame 1 = `$10:8180` — **`$180` bytes apart, matching the `$0413 = $0180` size**.
  So each frame is a `$180`-byte tile block in **ROM bank `$10`**, DMA'd straight to
  VRAM, cycled to animate (water/lava/etc.).

**DBR relevance:** the *only* DBR-dependent ROM reads here are the 3-byte source
descriptor (`$DFE2,X` table + `($48),Y`). DB=`$81` would shave ~6–18 mc per call,
and the loader doesn't even run every frame → negligible. The `$180`-byte payload is
DMA, so it's immune (both facts above).

## The one CPU VRAM loop — `$00:83B7` (file `0x03B7`)

```
LDA #$01FF
loop: STA $2118 / DEX / BNE loop        ; fill VRAM with a constant
```

A VRAM **clear/fill** — it writes a constant and **reads no ROM**, so it has nothing
for DBR to accelerate either. (Its FastROM benefit is only the already-fast code
fetch of the tiny loop body.)

## Where a CPU ROM-read *would* exist (and DBR could matter)

A CPU routine that streams ROM is the one shape that could benefit from a CPU-speed
knob. The room loader **does** have one — the **tilemap decompressor `$02:85F0`**,
which unpacks a byte stream from ROM bank `$04` into the `$7E:5000` tilemap buffer.
But it reads via a **bank-explicit 24-bit pointer** (`LDA [$D0],Y`, bank in `$D2`),
so DB is irrelevant; FastROM applies only by pointing that pointer at the fast mirror
(`$84`), and even then the gain is ~2% of a one-time room-load cost. Full analysis in
[map-decompressor.md](map-decompressor.md). Tile *pixel* graphics remain raw DMA
(this doc) — there is no pixel-graphics decompressor.

## Bottom line

Graphics and animation loading in Lagoon is DMA, which is fixed-rate and bank-explicit
— **FastROM and the DBR-to-fast-mirror trick cannot speed it.** Making these transfers
cheaper is a *strategy* question (coalesce uploads, fewer per-frame DMAs, HDMA where
applicable), not a CPU-clock question. See
[object-update-loop.md](object-update-loop.md) for the per-object update cost (which
*is* CPU/WRAM-bound) and the full DBR analysis.

## Tooling

`.agents/lagoon_dis.py` (`dis`/`scan`) over `l.cdl`. Loaders found by scanning the
DMA-kick `STA $420B` (`8D 0B 42`), then reading back each channel's `$43xx` setup;
the animated-tile path found from the `$0410/$0412/$0413` writers (`$01:DFCB` etc.)
and decoding the `($48)` frame table as 3-byte `addr.w,bank.b` records.
