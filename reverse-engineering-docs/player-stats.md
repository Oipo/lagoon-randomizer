# Player stats (WRAM)

Player stats live in the player object-0 region (base `$0500`, see
[town-movement.md](town-movement.md)). Each is a 16-bit little-endian word (low
byte at the listed address, high byte in the next).

| Addr        | Size | Stat       | New-game start |
|-------------|------|------------|----------------|
| `$0520`     | 16b  | HP         | 10             |
| `$0522`     | 16b  | MP         | 3              |
| `$0524`     | 16b  | Strength   | 10             |
| `$0526`     | 16b  | Defense    | 5              |
| `$0528`     | 16b  | Gold       | 100 (`0x64`)   |
| `$052A`     | 16b  | Experience | 0 (RAM-cleared)|
| `$052C`     | 16b  | **Current level** | 1 (init `$02:D9DA`) |
| `$0530`     | 8b?  | Character/class index | — (used to pick stat bonus) |

> Correction: `$052C` is the **current level**, NOT max HP. Increasing it raises
> max HP/MP. Max HP/MP are not stored as their own fields — they are **derived
> from level via tables** (see below) and cached at `$0370` (max HP) / `$0371`
> (max MP). The HP/MP clamp routines do `LDA $052C` (level) → `TAY` →
> `LDA $E140,Y` to get the cap each time.

## Per-level stat tables — bank $01

`$052C` (level) indexes these. All confirmed against observed values (HP
10/17/23, MP 3/8/10 at levels 1/2/3) and the level-1 entries match the
new-game init constants (Str 10, Def 5).

| Table              | Addr / ROM off | Width | Index    | First entries (lvl 1..) |
|--------------------|----------------|-------|----------|--------------------------|
| **Exp to next**    | `$01:E068` / `0x00E068` | 16b | `level*2` | 10, 40, 90, 170, 280, 400, 560… |
| **Max HP**         | `$01:E141` / `0x00E141` | 8b  | `level`   | 10, 17, 23, 28, 36, 43, 54… |
| **Max MP**         | `$01:E165` / `0x00E165` | 8b  | `level`   | 3, 8, 10, 12, 19, 22, 27… |
| Base Strength      | `$01:E0B0` / `0x00E0B0` | 16b | `level*2` | 10, 13, 17, 22, 26, 30, 34… |
| Base Defense       | `$01:E0F8` / `0x00E0F8` | 16b | `level*2` | 5, 10, 16, 19, 23, 26, 31… |
| Class bonus → Str  | `$01:E188` / `0x00E188` | 16b | `class*2` | (added to Strength) |
| Class bonus → Def  | `$01:E194` / `0x00E194` | 16b | `class*2` | (added to Defense)  |
| Class bonus (3rd)  | `$01:E1A0` / `0x00E1A0` | 16b | `class*2` | — |

Note: the HP/MP tables are based at `$E140`/`$E164` and indexed by the 1-based
`level` (so `level 1 → $E141`/`$E165`); the 16-bit tables are based at the real
start and indexed by `level*2`. Max level is **35** (`CMP #$23` in the level-up
check). The `$E188` table is the per-class **Strength bonus**, not "exp to next"
(that earlier guess was a near-miss — exp-to-next is `$E068`).

### Level-up flow

- Award exp + check: `$01:94B2` — `exp ($052A) += $0370`, then
  `LDA $052A / SBC $E068,Y` (Y=level*2); if `exp ≥ threshold` → `INC $052C`
  (level up) and `JSL $0194F7`.
- Recompute stats: `$0194F7` — e.g. `LDA $E0B0,Y (base Str[level]) /
  ADC $E188,X (class bonus[$0530]) / STA $0524`; same shape for Defense via
  `$E0F8`/`$E194`. Max HP/MP cached via `LDA $052C / TAY / LDA $E140,Y` (→`$0370`)
  and `LDA $E164,Y` (→`$0371`) at `$01:9580`.

## New-game init — `$02:D9CC`

A one-time setup routine (preceded by a RAM-clear loop at `$02:D9BE`) seeds the
start position and stats with `LDX #imm / STX $05xx` pairs:

```
LDX #$0280 / STX $0502     ; start X = 0x280
LDX #$0070 / STX $0504     ; start Y = 0x70
LDX #$000A / STX $0520     ; HP       = 10
           / STX $0524     ; Strength = 10  (reuses #$000A)
LDX #$0003 / STX $0522     ; MP       = 3
LDX #$0005 / STX $0526     ; Defense  = 5
LDX #$0064 / STX $0528     ; Gold     = 100
LDX #$FFFF / STX $0536     ; (flag, purpose TBD)
```

These immediates are the natural targets for a "starting stats" randomizer option.
Headerless ROM offsets of each `LDX #imm` low byte (`STX` order in the routine):

| Stat        | `LDX` at  | imm low-byte offset |
|-------------|-----------|---------------------|
| HP + Str    | `$02:D9EB`| `0x0159EC`          |
| MP          | `$02:D9F4`| `0x0159F5`          |
| Defense     | `$02:D9FA`| `0x0159FB`          |
| Gold        | `$02:DA00`| `0x015A01`          |

(HP and Strength share the one `#$000A` load, so a separate Strength value would
need its own `LDX` inserted.)

## HP/MP modification routines — bank $01

`$0520`/`$0522` are the *canonical* current HP/MP (not a display shadow):
- `$01:91EA` add-to-HP, `$01:9226` subtract-from-HP (delta in `$1011`).
- `$01:940D` add-to-MP, `$01:9449` subtract-from-MP.
- These are indexed by `,X` (per character/party slot) and clamp current HP/MP
  to max by `LDA $052C` (level) → `LDA $E140,Y`/`$E164,Y` (the per-level table).

## Standing-still HP/MP regen (per-frame)

Observed: editing `$0520`/`$0522` *down* in the memory viewer gets restored back
to 10/3 next frame while standing still, *unless a menu is open*. Since 10/3 are
the current **max** HP/MP at this early point (level 1, from the tables above),
this is the dungeon/town **regen** that slowly refills HP/MP when idle — it tops
current (`$0520`/`$0522`) up toward max (cached `$0370`/`$0371`, derived from
level via `$E141`/`$E165`).

Open questions (NOT yet reliably resolved — this code region is dense interleaved
code/data and resists static disassembly; do NOT trust a raw byte-pattern match
for `INC $0520`/`EE 20 05` as a real instruction here):
- **Exact regen routine + PC.**
- **Regen rate** (how many HP/MP per N frames; likely a frame-counter gate).
- **Idle gate** — what it checks (velocity `$0506`/`$0508` == 0, or input, or a
  player-state byte such as `$050A`).

To resolve at runtime: set a **Write** breakpoint on CPU `$0520` while standing
still in town. The firing PC that is NOT the heal/damage routines (`$01:91EA` /
`$01:9226`) nor the new-game init (`$02:D9EE`) is the regen. From there: the
max check is `LDA $052C / TAY / ... $E140,Y` (cap to table value); a nearby
`DEC`/`BNE` on a counter is the rate; the branch guarding entry is the idle gate.

## Open questions / unidentified

Loose ends from the stat/level reverse-engineering, with what is known so far.
Hypotheses are marked `(?)` — not yet confirmed.

### RAM (object-0 block `$0500`)

- **`$052E`** — purpose unknown. Read at `$01:BDB0` (`LDA $052E`) and `$02:B270`.
  Sits between Experience (`$052A`) and Level (`$052C`); earlier mislabeled "max MP".
- **`$0530` / `$0532`** — index into the class/equipment bonus tables during the
  level-up recompute (`$0530*2` → `$E188`/`$E194`; `$0532*2` → `$E1A0`). Meaning
  (character vs. class vs. equipped-gear slot) (?) unconfirmed.
- **`$0531` / `$0533`** — condition bytes checked in the recompute. `$0533==2`
  adds `+30` (`#$001E`) to Strength at `$01:9515`; `$0533==1` triggers another
  Defense bonus at `$01:9560`. Likely equipped-weapon/armor type (?).
- **`$0536`** — set to `$FFFF` by the new-game init; purpose TBD.
- **`$0370` / `$0371`** — cached max HP / max MP (recomputed from level at
  `$01:9580`). Confirmed as a cache, but also reused as scratch elsewhere
  (e.g. the exp-gain amount in the level-up routine `$01:94B2`).

### ROM tables (bank $01)

- **`$01:E120`** — 16 × 16-bit, `[101,110,124,137,142,150,161,172,184,193,203,
  212,223,231,243,255]`. Increasing; purpose unknown (another curve? a 16-entry
  index?). Not yet tied to any read.
- **`$01:E1A0`** — third level-up bonus table; traced to a **second Defense**
  bonus (`LDA $0526 / ADC $E1A0,X / STA $0526`), indexed by `$0532`. Confirmed
  target (Defense) but the index's meaning is the `$0532` question above.
- **`$01:E1AC`** — read/compared at `$01:9750`/`$01:9755` (`CMP $E1AC,Y` /
  `LDA $E1AC,Y`) in the field/HUD scroll code. Raw looks like 4-byte rows
  (`03 06 1E 32 / 07 0C 23 37 / …`). Purpose unknown.

### Mechanics

- **Max HP/MP tables are 8-bit** (`$E141`/`$E165`, cap at `255`) while current
  HP/MP (`$0520`/`$0522`) are 16-bit. How HP/MP above 255 at high levels is
  handled (a paired high-byte table, or a hard 255 cap) (?) is unresolved — the
  tables end at `FF` by ~level 30.
- **Experience cap** — `$052A/$052B` is clamped to `$FFFF` (65535) in the
  award-exp routine (`$01:94B2`), so max total exp is 65535.
- **Standing-still regen** — routine PC, rate, and idle gate still open (see the
  regen section above).
- **`$0530` "class" vs. single character** — Lagoon is largely single-character
  (Nasir), so what selects a non-zero bonus index is unclear; possibly equipment.
