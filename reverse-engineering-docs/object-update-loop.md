# Per-frame object update loop & enemy AI (bank $01 / $02)

How Lagoon updates **every on-map object** (the player, NPCs, enemies, projectiles
— "sprites") once per frame, and where enemy AI lives. This is the routine that the
[performance question](#performance-note--why-5-sprites-lags) traces back to: the
per-object cost here is what blows the frame budget.

All of this executes in bank `$01`/`$02`, which are now in the FastROM mirror, so
the debugger no longer breaks in `$00000-$05FFFF` — the same bytes run as `$81`/`$82`.

## Object table layout

Objects are a flat array in WRAM: **base `$0500`, stride `$40`, 33 slots**
(`$0500`-`$0D3F`). Slot 0 is the player; slots 1-32 are everything else. A parallel
per-object array lives at **`$7E:8000`** (same stride/index, purpose: extended
sprite/animation state — see despawn below).

Per-object fields confirmed so far (offset within the `$40`-byte slot, via `,X`):

| Off  | Field                                                        |
|------|-------------------------------------------------------------|
| `$00`| active flags — **bits 7+6 set (`& $C0 == $C0`) = live slot** |
| `$01`| flags (`$10`,`$20`,`$08`,`& $03` all tested in various phases) |
| `$02`/`$03` | world X (16b)                                         |
| `$04`/`$05` | world Y (16b)                                         |
| `$06`/`$07` | X velocity (16b, added to X by the integrator)        |
| `$08`/`$09` | Y velocity (16b)                                       |
| `$0A`| pose / animation-cell index (sub-dispatch key in phase 5)   |
| `$0B`| direction (0=R,1=D,2=L,3=U)                                  |
| `$10`| cleared by the integrator each frame                        |
| `$11`| object kind/role (player-attack gate uses `==2`)            |
| `$12`| animation timer (`INC`'d in phase 2, wraps at 6)            |
| `$13`| scripted facing → copied into `$19`                         |
| `$14`/`$16` | sub-pixel fraction accumulators (integrator)          |
| **`$18`**| **primary state (0-5) — the master AI dispatch key** |
| `$19`| facing (sign bit = "invisible/skip")                        |
| `$1A`| speed tier (indexes the velocity table, see town-movement)  |
| `$1B`| sub-state (dispatch key for `$01:81BB`)                      |
| `$1F`| status flags (`$40`,`$08` tested/set)                       |
| `$20`+| stats — HP/MP/Str/Def for any combatant slot (see player-stats) |
| `$30`-`$33`| **non-player: anchor/home X,Y** (despawn distance ref); for the **player** these same bytes are the class/stat indices — overloaded by slot |

## The main loop — `$01:80AF` (file `0x080AF`)

```
80AF  LDX #$0000                 ; start at slot 0 (player)
80B2 loop:
      LDA $0500,X / AND #$C0
      CMP #$C0 / BNE 80CD        ; slot not live -> skip the whole pipeline (cheap)
      JSR $8184                  ; phase 1  AI / "think"      (dispatch on $0518)
      JSR $C5ED                  ; phase 2  anim timer + facing
      JSR $9BCE                  ; phase 3  build OAM cells    (dispatch on $0518)
      JSR $A4D3                  ; phase 4  integrate + collide
      JSR $A0E1                  ; phase 5  multi-cell sprite layout (dispatch on $0518)
      JSR $A7B5                  ; phase 6  (dispatch on $0518)
80CD: JSL $0281DE                ; despawn / GC check
      REP #$20 / TXA / ADC #$0040 / TAX / SEP #$20   ; next slot
      CPX #$0840 / BNE 80B2      ; loop all 33 slots
80E3  ... post-pass JSLs (whole-frame work: $02B2C1, $01C4AF, ...)
```

**Every live object runs all six phases + the despawn check, every frame.** The
inactive-slot skip is cheap (4 instructions), so cost scales with the number of
**live** objects, not the 33-slot capacity.

## The AI is a stack of parallel state machines

Phases 1, 3, 5, 6 are each an independent jump-table dispatch **keyed on the same
`$0518,X` (primary state)**. The dispatch idiom is identical everywhere:

```
LDA $0518,X / ASL / TAY
LDA table,Y -> $26 / LDA table+1,Y -> $27
JMP ($26)
```

The five top-level dispatch tables (6 entries each, state 0-5):

| Phase | Entry  | Table     | state0 | state1 (alive) | state2 | state3 | state4 | state5 |
|-------|--------|-----------|--------|------|------|------|------|------|
| 1 AI  | `$8184`| `$01:8198`| `$81A4`| `$81A4` | `$81A4` | `$81BB`| `$8201`| `$8201`|
| 3 OAM | `$9BCE`| `$01:9BE2`| `$9BEF`| `$9C1B` | `$9C1B` | `$A01A`| `$9BEE`| `$9BEE`|
| 5 cells| `$A0E1`| `$01:A101`| `$A10D`| `$A243` | `$A2BE` | `$A312`| (none)| (none)|
| 6     | `$A7B5`| `$01:A7C9`| `$A7D6`| `$AC4F` | `$AB31` | `$ACAA`| (none)| `$B12B`|

There is a **second dispatch level inside the alive state**. Phase 1 state-1
handler `$81A4` calls `$01:9592`, which *re-dispatches on `$0518`* through table
`$01:9B28` (state1 → `$9618`). Phase 3's `$9C1B` then dispatches **again on facing
`$0519`** (table `$01:9C38`) to place cells per direction, and phase 5's `$A243`
dispatches on **pose `$050A`** (table `$01:A26E`) to lay out multi-tile sprites
(sub-cells offset by ±`$12` px). Phase 1 also has a sibling dispatch `$01:81BB`
keyed on sub-state `$051B` (table `$01:81CF`).

Net: a single live object can hit **6-8 indirect `JMP ($26)` dispatches per frame**
before any actual work runs.

### State `$0518` meaning (0-5)

Inferred from handler shapes; 0/1/3 are solid, 2/4/5 less so:

| State | Meaning (confidence)                                              |
|-------|------------------------------------------------------------------|
| 0     | spawn / init                                                     |
| 1     | **alive / active** — the bulk of AI + movement runs here        |
| 2     | special-active (carried/attached?) — `(?)`                       |
| 3     | hurt / knockback / death-start (phase 5 *always* processes 3)   |
| 4, 5  | dying / cleanup — most handlers no-op or fall back to the loop `(?)` |

## What each phase does

- **Phase 1 — `$01:8184` (AI / "think").** State 1 → `$81A4` → `JSR $9592`
  (re-dispatch, the real per-state brain) + `JSR $D3E9` + a couple of subsystem
  calls via `JSL $0080B9` (the game's command dispatcher, called with an id in A).
- **Phase 2 — `$01:C5ED` (anim timing + facing).** `INC $0512,X` timer (wraps at
  6), then by state copies scripted facing `$0513,X → $0519,X`. This is also where
  per-tier velocity gets refreshed (the `$02:8129` speed table, see town-movement).
- **Phase 3 — `$01:9BCE` (OAM cell build / "sprite locations").** State 1 →
  `$9C1B`: skips if facing `$0519` is negative (invisible), else dispatches per
  direction (`$9C38`) and computes **screen-space cell positions** from world pos
  (`LDA $0502 + $9DBA,Y …`). This is the most literal "update sprite locations".
- **Phase 4 — `$01:A4D3` (integrate + collide).** `JSR $A4EB` does
  `$0502 += $0506`, `$0504 += $0508` (the integrator from town-movement), then
  `$A56A` / `$A5AA` / `$A624` handle map-tile collision / interaction.
- **Phase 5 — `$01:A0E1` (multi-cell layout).** Gated on facing visibility; state
  1 → `$A243` lays out the object's individual hardware sprites (sub-cells at
  ±`$12` px) by pose `$050A`.
- **Phase 6 — `$01:A7B5`.** State 1 → `$AC4F`; role not fully pinned `(?)` (likely
  shadow/contact/interaction).
- **Despawn — `$02:81DE` (file `0x101DE`).** For objects flagged `& $C0 == $80`
  in state 2: computes distance from the player (`$0502 - $0530,X`,
  `$0504 - $0532,X`); if either axis is >255 px away (high byte not `00`/`FF`),
  it **zeroes the whole `$40`-byte slot at `$0500,X` and the parallel `$7E:8000,X`
  block** — i.e. off-screen culling. Anchor `$0530-$0533` is the cull reference.

### Where enemy-specific behavior comes from

`$0518` is a **shared lifecycle**, not the enemy type — the player runs the same six
tables. Per-*type* behavior is selected deeper in the state-1 chain
(`$81A4 → $9592 → $9B28[1]=$9618 …`) and via data fields (`$0511` kind, `$051B`
sub-state, the scripted-facing `$0513`, speed tier `$051A`). Mapping each enemy id
to its concrete `$9618`-subtree behavior is the natural next dig (left open below).

## OAM upload

The phase-3/5 handlers write cells into an **OAM shadow buffer in WRAM**; the actual
DMA to the PPU OAM happens in the **NMI / VBlank handler** (NMI is `$00:891B`, per
[screen-fade.md](screen-fade.md)). `$00:8518` is the *BG/room* PPU setup
(`$2105`-`$2109` from the room table `$0086E6,X`), not the OAM DMA — the OAM DMA
channel itself is not yet pinned (one of `$00:8199/8344/88C3/8E69/8F11/8F44`, the
`STA $420B` sites). `(open)`

## Performance note — why 5 sprites lags

This loop *is* the bottleneck behind the slowdown. FastROM put all of bank
`$01`/`$02` code into the 3.58 MHz mirror, but the dominant cost is WRAM, which
FastROM never touches.

### Cost model

Master clock ~21.48 MHz → at 60 Hz, **~357k master cycles (mc) per frame**, call it
~340k usable after DRAM refresh + HDMA. The two access costs that matter:

- **FastROM code/operand fetch:** 6 mc/byte (what `$420D` bought).
- **WRAM data access:** **8 mc/byte, always** — `$420D` does nothing to it. A
  *16-bit* WRAM field read/write is 16 mc.

So the whole frame is only **~21,000 sixteen-bit WRAM ops** if you did nothing else.
Every per-object field touch spends against that budget, and ~5 live objects is
where 6 phases × per-object WRAM work crosses it.

### Where the cycles go

1. **Redundant dispatches (small, ~2%).** Every phase re-runs `LDA $0518,X / ASL /
   TAY / LDA table,Y / STA $26 / LDA table+1,Y / STA $27 / JMP ($26)` — re-reading
   the *same* state byte and recomputing `state*2` to look up the same handler in a
   different table, plus a full `$26/$27` round-trip. ~27 cyc ≈ ~170 mc, done 6-8×
   per object → ~1.4k mc/object → ~7k mc for 5 objects ≈ **~2% of the frame**. Real
   and removable, but *not* the headline.
2. **WRAM round-trips (the structural cost).** The six phases are separate `JSR`'d
   routines sharing only `X`, so field *values* aren't carried between them. World
   position `$0502/$0504` alone is re-loaded from WRAM ~4-6× per object per frame —
   phase 3 (`$9C1B`, OAM cells), phase 4 (`$A56A` collision + `$A4EB` integrate,
   read+write), phase 5 (`$A243` layout) — each a 16-bit (16 mc) hit, purely
   because nothing caches it across phases.
3. **The handler bodies are the fat.** Boundary collision compares, the
   hardware-multiply indexing in `$A624` (write `$4202/3`, ~8-cycle latency stall
   before reading `$4216`), the full OAM rebuild *every frame*, and byte-at-a-time
   clears (despawn `$02:81DE` zeroes 0x40 bytes one at a time). All WRAM-bound; this
   dwarfs the dispatch overhead.

### What can be done about wall-to-wall WRAM

Hard truth first: **WRAM is fixed at 8 mc/byte, there is no faster general RAM on a
stock SNES, and FastROM never touches it.** You can't make an access cheaper — every
lever just does *fewer* of them.

| Lever | What it does | Reward | Risk / effort |
|-------|--------------|--------|---------------|
| **Dirty-flag presentation** | Phases 3 & 5 rebuild OAM from scratch every frame even for an unmoved/unchanged object. A "needs-redraw" flag lets idle/slow objects skip the expensive WRAM+multiply work most frames. Composes with the existing off-screen cull. | **High** | Medium effort, *lower* risk (localized) |
| **Direct Page = object base** | Set `D = $0500 + slot` once/object, then `$0502,X`→`$02` (dp). Data is still 8 mc, but shaves the abs operand byte (6 mc) + index penalty + 1 internal cycle off each of ~40-100 field accesses/object (~300-1000 mc/object). | Medium | **High** — every handler rewritten to dp; must prove nothing relies on `D=0` (shared dp scratch `$26`/`$40` would alias the struct) |
| **Merge phases / cache in registers** | Fuse the pipeline so pos/state load once and thread through, killing cross-phase re-reads. | Medium-high | High — architectural rewrite |
| **16-bit batch clear/copy loops** | Byte-at-a-time clears (despawn's 0x40 loop, etc.) → `REP #$20` halves those accesses. | Low-medium | Low |
| **Kill the dispatch recompute** | Compute `$0518*2` once/object, pass it in. | ~2% | Free-ish if already restructuring |
| **Fewer REP/SEP toggles** | Each toggle ~18 mc; they pepper every handler. | Low | Low |

**Recommendation:** the floor is WRAM and it's immovable, so the only question is
"touch it fewer times." The dispatch redundancy is real but ~2% — not worth chasing.
The fat is the per-object bodies doing a full collision + multiply + OAM rebuild
every frame, so the best effort/reward is **dirty-flagging the presentation phases**
so idle objects skip phases 3/5 — no risky global Direct-Page rewrite required.

Before optimizing, profile in Mesen2: PC histogram with 2 vs 6 live objects, diff,
and confirm the bodies (collision/multiply/OAM) dominate over the dispatch plumbing.

## Open questions

- Exact meaning of states 2/4/5 and the `$01:81BB`/`$051B` sub-state machine.
- Per-enemy-type AI: map object kind (`$0511`?) → the `$9618` alive-subtree branch.
- The OAM→PPU DMA site/channel and the shadow-buffer address.
- Purpose of the `$7E:8000` parallel per-object array (extended sprite state?).

## Tooling

Disassembled with `.agents/lagoon_dis.py` (`dis`/`scan`) and `.agents/xref.py`,
both CDL-aware (`l.cdl` M/X flags). Main loop found by scanning the slot-advance
idiom `ADC #$0040` (`69 40 00`) + `CPX #$0840` (`E0 40 08`); dispatch tables dumped
as 16-bit word arrays from each `JMP ($26)` site.