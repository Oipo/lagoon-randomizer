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
LDA $051A,X / ASL*4  -> tier*16        ; $051A = speed tier
LDA $050B,X / ASL*2  -> + dir*4        ; $050B = direction
TAY
LDA $8129,Y -> STA $0506,X             ; X velocity (16-bit)
LDA $812B,Y -> STA $0508,X             ; Y velocity (16-bit)
```

### Velocity table `$02:8129` (16 bytes/tier = 4 dirs x {Xvel.w, Yvel.w})

| Tier | Speed | Row offset (headerless ROM) |
|------|-------|------------------------------|
| 0    | ±1    | `0x010129`                   |
| 1    | ±2    | `0x010139`                   |
| **2**| **±3**| **`0x010149`** ← player walk |
| 3    | ±4    | `0x010159`                   |
| 4    | ±5    | `0x010169`                   |
| 5    | ±6    | `0x010179`                   |
| 6    | ±7    | `0x010189`                   |

**The player walks at tier 2 = ±3 px/frame.** The four nonzero entries of that row:

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

## Exposed as a randomizer option

`RandomizerOptions::walk_speed` (default 3, clamped to [1, 15]) rewrites the four
tier-2 entries above to `±walk_speed`. CLI: `--walk-speed/-w`. Web: `walkSpeed` /
"Town walk speed" dropdown (Default / 33% / 66% / 100% = 3 / 4 / 5 / 6).

Caveat: any other object that walks at tier 2 also speeds up. To affect only the
player, the alternative is to change the player's `$051A` speed tier, but that byte
is set by data-driven/indexed code and is context-dependent.

## MesenCE recipe

- *Write* breakpoint on CPU `$0506` → breaks in setter `$02:8106` when you start
  walking; confirms the table read from `$8129`.
- Watch `$0502`/`$0504` (pos), `$0506`/`$0508` (vel), `$051A` (tier), `$050B` (dir).
