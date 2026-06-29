# Map / tilemap decompressor — `$02:85F0` (FastROM analysis)

The room-load decompressor left `(open)` in
[graphics-loading.md](graphics-loading.md). It turns out to be a **tilemap (layout)
unpacker**, not a tile-*pixel* decompressor (tile graphics are raw DMA, see that
doc). It reads a command/tile byte stream from ROM **bank `$04`** and writes decoded
tile indices into the `$7E:5000` tilemap buffer. It runs on **room/map load**
(transitions), not per frame.

## Data flow

Source pointer set up at `$02:82B0` (four sibling sites, all bank `$04`):

```
LDA $048679,X -> $D0          ; map stream addr low   (pointer table $04:8679,X, X = room id)
LDA $04867A,X -> $D1          ; addr high
LDA #$04      -> $D2          ; source BANK = $04   (the compressed map data bank)
JSR $85F0                     ; run the unpacker
```

The unpacker — `$02:85F0` (file `0x105F0`):

```
LDY #$0000 / JSR $8939        ; reset output cursor ($0447) + state
loop ($85F6):
  LDA [$D0],Y / INY           ; <-- read next stream byte from $04:.. via 24-bit pointer
  CMP #$FF / BEQ done         ; $FF = end of stream
  CMP #$0F / BEQ row          ; $0F = row/section advance
  PHA / (clear $0450 bit0) / PLA
  CMP #$20 / BCC command      ; < $20  -> control code  ($02:8662: AND #$3F, dispatch via $8669)
  LDX $0447 / STA $7E5000,X   ; >= $20 -> tile index, write to the $7E:5000 tilemap buffer
  INX / STX $0447 / INC $0453
  BRA loop
```

So bytes `>= $20` are tile indices copied straight to the map buffer; bytes `< $20`
are control codes dispatched through a small jump table at `$02:8669` (run-length /
row / attribute commands). Classic byte-stream tilemap packer.

## FastROM analysis — applies, but negligible, and it is **not** a DBR change

**The source read is bank-explicit, so DBR is irrelevant.** `LDA [$D0],Y` takes its
bank from the pointer byte `$D2`, not from DBR. The
[DB/DBR trick](object-update-loop.md) does nothing here.

**FastROM *can* apply — via the pointer bank.** `$D2 = $04` is a slow bank (banks
`$00-$3F` at `$8000+` are 8 mc even with `$420D` on). Pointing the pointer at the
**fast mirror `$84`** (`$84:xxxx` ≡ `$04:xxxx`, byte-identical, 6 mc) makes each
stream read 8 → 6 mc — **2 mc/byte saved**. That is the same idea as the existing
code-bank FastROM bumps, extended to a *data* pointer. Four one-byte patches:

| Site         | Change            | imm file offset |
|--------------|-------------------|-----------------|
| `$02:82C4`   | `LDA #$04` → `#$84` | `0x102C5` |
| `$02:82E5`   | `LDA #$04` → `#$84` | `0x102E6` |
| `$02:8306`   | `LDA #$04` → `#$84` | `0x10307` |
| `$02:8327`   | `LDA #$04` → `#$84` | `0x10328` |

**Why the gain is tiny.** Per output byte the inner loop does **one** savable source
read plus ~6 unavoidable WRAM accesses (`PHA`/`PLA`, `LDA`/`STA $0450`, `LDX`/`STX
$0447`, `STA $7E5000,X`, `INC $0453`) — each 8-16 mc and **immovable** (WRAM is 8
mc regardless of FastROM/DBR). The savable 2 mc is ~2% of an ~80-100 mc/byte loop. So
FastROM-ifying the source shaves **~2% off the decompressor's run time**, and that run
time is a **one-time room-load cost, not framerate** — a few hundred to a few
thousand stream bytes per room ⇒ the saving is **sub-millisecond per transition,
imperceptible**.

**Verdict.** Safe and trivial (4 bytes, `$84` mirrors `$04` exactly) but performance-
negligible. Worth applying only as part of a "finish the FastROM data-bank pass" tidy-
up, never for a perceptible speed-up. The decompressor is WRAM-write-bound, the same
wall that limits the [object loop](object-update-loop.md) — only here the writes go to
the `$7E:5000` tilemap buffer instead of the object array.

## Note on the other ROM-stream interpreters

The bank-`$0C` code at `$0C:F890` is a separate **script/command interpreter** (reads
its stream via `LDA $0CF9FC,X` absolute-long, dispatches `JSL`s) — also bank-explicit,
same conclusion: DBR-irrelevant, FastROM only via the explicit bank, room/cutscene-
time not framerate.

## Tooling

`.agents/lagoon_dis.py`. Found by tracing the `$7E:5000` tilemap-buffer writer
(`$02:8612`) back to its read loop (`LDA [$D0],Y` at `$02:85F6`), then the `$D0-$D2`
pointer setup (`STA $D2` sites) to the bank-`$04` immediates.
