# Screen fade (menu open / transitions) — bank $00

Opening the item menu fades the screen to black via a brightness ramp on the PPU
**INIDISP** register (`$2100`, bits 0-3 = master brightness $0..$F). The fade is
**slow because it waits 3 frames per brightness step** (15 steps × 3 = ~45 frames
≈ 0.75 s). The wait, not the step count, is the knob.

## Call chain (the stack the user observed)

```
$81:8096  ...                     (game-mode handler)
$80:80B6  game-mode dispatcher
$80:80E9  ...
$80:83C4  fade-out routine        <-- the fade loop lives here
$80:84E0  wait-N-frames           <-- PC parks here, spinning on $023C
```
(Banks $80/$81 are the FastROM mirrors of $00/$01; headerless PC = `bank&$7F<<15 | addr&$7FFF`.)

## The fade-out routine — `$00:83C4` (file `0x003C4`)

```
83C4 LDA $023D / AND #$80 / CMP #$80 / BNE +1 / RTL   ; already blanked? bail
83CE LDA $0275 / BPL $83E7                            ; $0275>=0 => snap to black, no ramp
83D3 LDA #$03                ; <-- frames to wait per step   (operand @ 0x003D4)
83D5 JSL $0084E0             ;     wait 3 frames
83D9 LDA $023D / AND #$0F
83DE DEC                     ; brightness -= 1   (one byte 3A @ 0x003DE)
83DF STA $023D
83E2 STA $2100              ; write INIDISP brightness
83E5 BNE $83D3              ; loop until brightness == 0
83E7 LDA #$80 / STA $023D / STA $2100 / RTL          ; force-blank, done
```

- `$023D` = current brightness + state (bit7 = force-blank/blanked flag).
- `$0275` bit7 selects gradual fade (negative) vs instant snap (non-negative).

## The wait routine — `$00:84E0` (shared, do NOT edit this)

```
84E0 STA $023C            ; frame counter = A
84E3 LDA $023C / BNE $84E3 ; spin
84E8 RTL
```
`$023C` is decremented every vblank by the NMI handler at **`$00:891B`**
(`LDA $023C / DEC / STA $023C`). So `LDA #$N / JSL $84E0` = "wait N frames".
This is the game's generic frame-wait used everywhere — change the *constant at
the call site*, never `$84E0` itself.

## How to speed up the menu fade

**Best knob — shrink the per-step wait** at `0x003D4`:

| Value @ `0x003D4` | Frames/step | Total fade | Effect |
|-------------------|-------------|-----------|--------|
| `03` (vanilla)    | 3           | ~45 (~0.75 s) | default |
| `02`              | 2           | ~30 (~0.5 s)  | smooth, faster |
| `01`              | 1           | ~15 (~0.25 s) | quick, still a visible fade |
| `00`              | 0 (no sync) | 1 frame   | instant cut to black (no gradient) |

`01` keeps a smooth gradient (one INIDISP step per displayed frame) while being 3×
faster. `00` runs all 15 writes within a single frame → effectively an instant cut.

**Stepping the brightness by >1 is the wrong knob:** the loop relies on `DEC`
landing exactly on 0 (`BNE`), and 15 is odd, so subtracting 2 would skip 0 and
spin ~255 extra frames. Reduce the wait instead.

## There are TWO different fade-in routines — pick the right one

`$0003D4` (fade-out) has **two** "fade back in" siblings, and they are used by
different game paths. Speeding up one does **not** speed up the other.

### Fade-in A — menu/HUD — `$00:83FF` (file `0x003FF`, wait operand @ `0x0040D`)

Closing the menu / fading back ramps `$0240` up via `ADC #$10` (16 steps to `$FF`)
and waits 3 frames per step at `LDA #$03 / JSL $84E0` (operand @ `0x0040D`). It is
**not reached by any `JSL`** — it's called locally inside the bank-$00 menu code.
This is the byte the randomizer currently patches as the "fade-in".

### Fade-in B — screen/room transitions — `$00:8458` (file `0x00458`, wait operand @ `0x0046D`)

This is the **true sibling of the fade-out `$83C4`**: both act on `$023D`/INIDISP.
`$83C4` `DEC`s brightness down to 0; `$8458` `INC`s it back up from 0 to `$0F`,
waiting 3 frames per step at `LDA #$03 / JSL $84E0` — the `#$03` operand is at file
offset **`0x0046D`** (`LDA #$03` @ `$00:846C`).

```
8458 LDA $023D / CMP #$0F / BNE +1 / RTL   ; already full bright? bail
8460 JSL $008112                           ; (re-enable display / setup)
8464 LDA #$00 / STA $023D / STA $2100      ; start at brightness 0
846C LDA #$03                ; <-- frames to wait per step   (operand @ 0x0046D)
846E JSL $0084E0             ;     wait 3 frames
8472 LDA $023D / INC / STA $023D / STA $2100
847C CMP #$0F / BNE $846C    ; loop until brightness == $0F
8480 RTL
```

Fade-in B is called **only from bank $02**, paired right after a fade-out `$83C4`
in the screen-rebuild sequence — e.g. `$02:B6B6 JSL $0083C4` (out) … rebuild BG/
tilemap … `$02:B6E3 JSL $008458` (in); same pattern at `$02:B7E0/$02:B814` and
`$02:C65B/$02:C672`. **This is the dungeon screen-to-screen transition.**

> ⚠️ The randomizer's `--fade` patches `0x0003D4` (transition + menu fade-out) and
> `0x0040D` (menu fade-in A) but **not** `0x0046D`. Result: when walking between
> dungeon screens the fade-*out* speeds up but the fade-*in* stays vanilla, because
> the transition fade-in is routine B. To fix, also write `01` to `0x0046D`.

## Exposed as a randomizer option

`RandomizerOptions::fade` (bool, default off). When set, `randomize_rom` writes
`01` to `0x0003D4` (fade-out per-step wait) and `0x0040D` (menu fade-in A),
dropping each from 3 frames/step to 1 (~3x faster, still a smooth per-frame ramp).
It is a plain on/off flag — not configurable to other values. CLI: `--fade`. Web:
`fade` / "Faster menu fade" checkbox.

## Tooling
Disassembled with `.agents/lagoon_dis.py` (CDL-aware). NMI/$023C link confirmed by
scanning code refs to `$023C`.
