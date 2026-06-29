# Town movement & player position

## Key RAM variables (player = object 0, base `$0500`, 0x40-byte stride)

| Addr   | Size | Meaning                                              |
|--------|------|------------------------------------------------------|
| `$0502`| 16b  | **Player world X** (`$0503` high byte)               |
| `$0504`| 16b  | **Player world Y** (`$0505` high byte)               |
| `$0506`| 16b  | **X velocity** (added to `$0502` each frame)         |
| `$0508`| 16b  | **Y velocity** (added to `$0504` each frame)         |
| `$050B`| 8b   | Direction index (0=right,1=down,2=left,3=up)         |
| `$051A`| 8b   | Speed tier (selects a row of the velocity table)     |
| `$0519`| 8b   | Facing direction                                     |
| `$021F`| 8b   | JOY1 high mirror: bit3=Up bit2=Down bit1=Left bit0=Right |

Confirmed at runtime (Mesen): walking left/right changes `$0502`, up/down changes
`$0504`, and `$0506` holds the X-velocity (e.g. `FD FF` = −3 when holding left).

> Data Crystal's RAM map lists `$0502`/`$0504` as 1-byte "Horizontal/Vertical
> Coord" — same addresses, but they document only the low byte. The runtime
> evidence above (and the integrator) confirm both are **16-bit**. See
> [ram-map.md](ram-map.md).

## Movement model — `position += velocity`

Per frame: `$0502 += $0506`, `$0504 += $0508` (integrator `$01:A4EB`). The velocity
is set from a **speed-tier table** by the generic object setter at `$02:80EA`:

```
LDA $050B,X / BMI zero                 ; $050B = direction; high bit set -> stop
LDA $051A,X / ASL*4  -> tier*16        ; $051A = speed tier
LDA $050B,X / ASL*2  -> + dir*4        ; $050B = direction
TAY
LDA $8129,Y -> STA $0506,X             ; X velocity (16-bit, two 8-bit loads)
LDA $812B,Y -> STA $0508,X             ; Y velocity (16-bit, two 8-bit loads)
```

### This is the *shared* per-object path (player and monsters alike)

The setter `$02:80EA` is not called directly — it's **command 19** in the bank-`$02`
routine-pointer table at `$02:8000` (`$02:8026` holds the word `$80EA`). It's
invoked through the command dispatcher `$00:80B9`/`$00:80D5` (`JSL $0080B9` with the
id in `A`), which sets `DBR=$02` (`LDA #$02 / PHA / PLB`) before the handler runs —
that is why the table read `LDA $8129,Y` lands in bank `$02`.

Command 19 is issued by the **shared phase-1 "alive" AI handler `$01:81A4`**, which
runs for *every live object* once per frame (it's the state-1/2 entry in the
per-object dispatch — see [object-update-loop.md](object-update-loop.md)):

```
$01:81A4  JSR $9592          ; movement "brain": picks this object's direction $050B
          ...
$01:81B4  LDA #$13 / JSL $0080B9   ; command $13 = 19 -> velocity setter $02:80EA
```

So the player and every monster execute the **same** code, with `X` = the object
slot being processed. Each one indexes the same `$8129` table by **its own** tier
`$051A` and direction `$050B`.

### Why monsters "match the player" — shared row, not a copy

There is **no copy-player-speed routine**: nothing in the ROM reads the player's
velocity or tier to stamp onto another object (zero hits for `LDA $0506`/`LDA $0508`/
`LDA $051A` absolute). Monsters track the player purely because both index the same
table row: the player's tier is `2`, so any monster whose tier is also `2` reads the
identical 16 bytes. Rewriting tier 2 changed *both* — they were always reading the
same bytes.

Where does a monster's `$051A` come from? **Its spawn template.** There are *zero*
discrete writes to `$051A` for non-player objects (no `STA $051A,X` anywhere) — and
zero to the state byte `$0518` either — while the despawn routine `$02:81DE`
bulk-*zeroes* the whole `0x40`-byte slot. Objects are therefore created by
block-copying a full `0x40`-byte record into the slot, so a monster's tier is baked
into its template. The player is the lone exception: it gets the template **plus**
discrete overrides at init (`$02:D9D8` block: position, tier, pose, dir, HP/MP/…),
including the only absolute `STA $051A` in the game, at `$02:D9DE`.

### Velocity table `$02:8129` (16 bytes/tier = 4 dirs x {Xvel.w, Yvel.w})

Only **7 rows exist (tiers 0–6)**. The slot a "tier 7" would occupy (`$02:8199`) is
*not* free — it's another command routine (idx 20 in the `$02:8000` table). So a new
tier requires relocating the table (see the randomizer option below).

| Tier | Speed | Row offset (headerless ROM) |
|------|-------|------------------------------|
| 0    | ±1    | `0x010129`                   |
| 1    | ±2    | `0x010139`                   |
| **2**| **±3**| **`0x010149`** ← *vanilla* player walk |
| 3    | ±4    | `0x010159`                   |
| 4    | ±5    | `0x010169`                   |
| 5    | ±6    | `0x010179`                   |
| 6    | ±7    | `0x010189`                   |

**In the vanilla game the player walks at tier 2 = ±3 px/frame** (the randomizer
moves the player to a relocated tier 7 — see below). The four nonzero entries of the
tier-2 row:

| Dir        | Offset      | Vanilla |
|------------|-------------|---------|
| Right (+X) | `0x010149`  | `+3`    |
| Down  (+Y) | `0x01014F`  | `+3`    |
| Left  (−X) | `0x010151`  | `−3` (`FD FF`) |
| Up    (−Y) | `0x010157`  | `−3` (`FD FF`) |

### Dead ends (things that are NOT town walk speed)

- `#$0003` immediates at `$01:9CC6/9CE3/9D84/9D9F` — these write `$0502/$0504` but
  *zero* the velocity; a stop/snap path. Patching them did nothing.
- `$01:C633`/`$01:C63B` velocity table (±6) fed by setter `$01:C5ED` from the
  object-0 special update `$01:80AF` — a different (field/action) mode, not town.
  Patching it (even to ±15) did nothing to town walking.

## Exposed as a randomizer option (player-only, via a dedicated tier)

`RandomizerOptions::walk_speed` (default 3, clamped to [1, 15]) gives the player a
**dedicated speed tier 7** so only the player is affected — tiers 0–6, and therefore
every monster, keep their vanilla speeds. CLI: `--walk-speed/-w`. Web: `walkSpeed` /
"Town walk speed" dropdown (Default / 33% / 66% / 100% = 3 / 4 / 5 / 6).

Because tier 7's table slot is occupied (see above) and the read is `LDA $8129,Y`
(DBR-relative, Y-indexed — no `long,Y` form, so it must stay in bank `$02`), the
randomizer relocates the table rather than editing tier 2 in place:

1. **Relocate** the table into unused bank-`$02` space — `$02:E427` / file
   `0x016427`, a 128-byte zero block untouched by every CDL (code *and* data) — and
   copy tiers 0–6 verbatim so monsters are byte-for-byte unchanged.
2. **Append tier 7** = `±walk_speed` (the four nonzero entries, same row layout).
3. **Repoint** the setter's four reads (`$02:8103/8109/810F/8115`,
   `LDA $8129/$812A/$812B/$812C,Y`, operands at file `0x010104/010A/0110/0116`) from
   base `$8129` to `$E427`.
4. **Switch the player's init tier** from 2 to 7 by rewriting the `$02:D9D8` block
   (file `0x0159D8`): the vanilla `LDA #$01 / STA $052C / INC / STA $051A` (tier =
   level+1 = 2) becomes an explicit `LDA #$07 / STA $051A`, reusing `A=1` for both
   level `$052C` and pose `$050A` to free the byte and a trailing `NOP` to preserve
   the 19-byte block length. The starting-level immediate stays at `0x0159D9`.

The relocation is speed-neutral (still read under `DBR=$02`); moving it to the `$82`
FastROM mirror would save only the 4 table bytes/object/frame (~0.01% of a frame),
so it isn't done. Implemented in `randomizer.hpp`.

### Follower (Thor/Giles) improvements (`--fast-follower`)

Two paired, follower-only hooks behind the `--fast-follower` option (off by
default; both gated at runtime on slot 1 + the follow flag, so monsters are never
touched). Hook 1 is the speed bump below; hook 2 is the direction-update fix in the
next section.

#### Hook 1 — speed: forced to tier 2 (vanilla ±3)

The follower is **object slot 1** (base `$0540`) and moves through the *same*
generic setter as everything else, but its tier byte `$055A` is **never written by
any code** — it stays `0` (= ±1 px/frame, the slowest row), so it crawls behind the
player. (Confirmed at runtime: while Giles followed, slot 1 read flags `$C8` = live,
tier `$055A` = `00`, X-velocity `+1`.) The game reserves slot 1 for the follower
whenever the follow flag `$7E:04E1` is set (bit 1 = Thor, bit 0 = Giles; cf. its own
"follower present?" check at `$01:89BD`, which tests slot 1).

Nothing sets the follower's tier, so the randomizer **hooks the tier load** in the
setter to substitute **tier 2** *only* for slot 1 while a follower is active:

```
$02:80EF  LDA $051A,X   ->  JMP $F400          ; 3-byte in-place hook

$02:F400  CPX #$0040        ; slot 1 (the follower slot)?
$02:F403  BNE  $F40F        ; no  -> object's own tier
$02:F405  LDA  $04E1        ; follow flag (DBR=$02 -> WRAM $7E:04E1)
$02:F408  BEQ  $F40F        ; no follower active -> own tier
$02:F40A  LDA  #$02         ; follower -> tier 2 (vanilla ±3)
$02:F40C  JMP  $80F2
$02:F40F  LDA  $051A,X      ; everyone else -> own tier
$02:F412  JMP  $80F2
```

Tier 2 (not the player's tier 7) is deliberate: the follow AI is tuned for the
vanilla player speed and overshoots / reverses direction at higher speeds, so the
follower is pinned to the moderate ±3 the AI was built around. Tier 2 is shared with
some monsters, but the hook only changes which row the *follower* reads, not the
row's contents, so no monster is affected.

The trampoline lives in CDL-clean bank-`$02` free space (`$02:F400` / file
`0x017400`); both `JMP`s are bank-relative so they resolve identically whether the
setter runs as `$02` (SlowROM) or `$82` (FastROM). Gated to slot 1 + the follow
flag, and applied only when `--fast-follower` is set. Implemented in `randomizer.hpp`.

#### Hook 2 — direction-recompute cadence (waypoint threshold)

The follower is a **waypoint follower**, not a position-chaser. Its escort AI lives
in bank `$02` (`$96xx`/`$97xx`, dispatched on its sub-state `$051B`; it's
follower-exclusive — it copies the *player's* direction bitmask `$053F` and toggles
the follow flags `$04E6`/`$04E1`). In sub-state 0 (`$02:9689`) it walks toward its
waypoint `$0579/$057B` and only **recalculates** its direction once it gets within
**16px** of that waypoint:

```
$02:9689  LDA $055F,X / AND #$04 / BEQ $96F6   ; "arrived" flag clear -> recalc ($96F6 writes $057F)
$02:96B5  CMP #$0010 / BCS $96F0               ; |dx to waypoint| >= 16px -> keep walking, no recalc
$02:96C7  CMP #$0010 / BCS $96F0               ; |dy to waypoint| >= 16px -> keep walking, no recalc
          ; within 16px of the waypoint -> mark arrived -> recalc next frame
```

The recalc (`$96F6`, which copies the player's `$053F` when close or picks the
dominant axis toward the player when far via `$A25D`) is what writes the follower's
`$053F` (`$057F`) — observed firing once per ~30 frames (~0.5s) at vblank. Because
the cadence is gated by *reaching the waypoint* (not by time or by being off-course),
bumping the follower to ±3 makes it travel ~3× farther per leg before it's allowed to
re-aim → it overshoots and ping-pongs left/right at the stair throat.

> The earlier `$01:B1E2`/`$B273` "position-chase" theory was wrong: a write
> breakpoint on `$7E:054B` showed the follower's direction is written only by command
> 17 (`$02:80D3`), resolved from `$053F`, and `$053F` itself is set on this ~30-frame
> waypoint cadence — the follower never executes `$B1E2` (that's a monster behavior).

`--fast-follower` widens the "arrived" threshold from **16px → 48px** (the two
`CMP #$0010` immediates at `$96B6`/`$96C8`: `$10 → $30`), so the follower recomputes
its direction ~3× sooner, matching the speed bump. The escort AI is follower-only, so
the build-time option gate suffices (no runtime slot/flag gate). Off by default;
implemented in `randomizer.hpp`.

## MesenCE recipe

- *Write* breakpoint on CPU `$0506` → breaks in setter `$02:8106` when you start
  walking; confirms the table read (`$8129` vanilla, `$E427` after the patch).
- Watch `$0502`/`$0504` (pos), `$0506`/`$0508` (vel), `$051A` (tier), `$050B` (dir).
- To verify the player-only patch: watch `$051A` (player tier should read `07`) and a
  tier-2 NPC's `$051A` (still `02`); raise `--walk-speed` and confirm only the player
  speeds up.
- Follower: while Thor/Giles follows, slot 1's tier `$055A` still reads its template
  value (`00`), but its X/Y velocity `$0546`/`$0548` should now move at `walk_speed`
  (the hook overrides the tier read without writing `$055A`). The follow flag is
  `$7E:04E1` (bit 1 Thor, bit 0 Giles).
