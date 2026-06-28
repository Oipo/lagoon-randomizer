# ROM map (Lagoon USA, headerless LoROM)

Master index of known ROM locations, transcribed from the **Data Crystal** ROM map
(<https://datacrystal.tcrf.net/wiki/Lagoon_(SNES)/ROM_map>) and cross-checked
against this project's runtime-verified docs. Where they overlap they **agree**,
which is a good sanity check on both. Entries that this project has independently
confirmed are linked to the relevant doc; the rest are "per Data Crystal, not yet
re-verified here".

> **Source / confidence.** Unless linked to another doc in this repo, an entry is
> transcribed from Data Crystal and **not yet independently verified** against our
> `Lagoon (USA)` ROM. Treat addresses as leads, not gospel — Data Crystal under-
> sizes several fields (it lists the low byte of 16-bit values; see
> [ram-map.md](ram-map.md)).

## Cartridge / header

| Property      | Value                          |
|---------------|--------------------------------|
| Mapper        | **LoROM**, **SlowROM** (200 ns) — this project's FastROM patches flip it to 120 ns |
| ROM size      | 1 MiB (8 Mbit)                 |
| SRAM          | 8 KiB                          |
| Internal checksum | `0x20E3` (good)            |
| Dumps         | Lagoon (U), (E), (J)           |

## Address notation

Data Crystal writes locations as `bb:aaaa` where the whole thing is a 24-bit
**headerless file offset** `0xbbaaaa`, with the SNES LoROM address in brackets.
This repo's convention is `SNES $bank:addr / file 0xNNNNNN`. Conversion (standard
LoROM):

```
file   = (bank & 0x7F) * 0x8000 + (addr & 0x7FFF)
bank   = file / 0x8000
addr   = (file % 0x8000) + 0x8000        ; always in $8000-$FFFF
```

So Data Crystal `0x00:e140` = file `0x00E140` = SNES **$01:E140**; their
`0x01:5640` = file `0x015640` = SNES **$02:D640**. All code/data banks below
($00-$05 in DC notation) convert cleanly and match our findings; the audio entry
is internally inconsistent in Data Crystal (flagged below).

## Code — routines

| SNES        | file       | What                                                       |
|-------------|-----------|------------------------------------------------------------|
| `$00:8013`  | `0x000013`| Reset vector / init block (`$00:8013-80AF`)                |
| `$00:80B3`  | `0x0000B3`| Main game subroutine                                       |
| `$00:80B9`  | `0x0000B9`| Game-intro subroutine (alt at `$00:80E6`)                  |
| `$00:9304`  | `0x001304`| Program loop calling intro + main (`$00:9304-9366`)        |
| `$01:9220`  | `0x009220`| **HP subtraction** (Nasir + monsters) — see [player-stats.md](player-stats.md) (`$01:9226`) |
| `$01:94F7`  | `0x0094F7`| **Strength-increase / stat recompute** — [player-stats.md](player-stats.md) (`$0194F7`) |
| `$01:952F`  | `0x00952F`| **Defense-increase** — [player-stats.md](player-stats.md)  |
| `$01:CD95`  | `0x00CD95`| Tests event byte `$7E:04E2` — see [ram-map.md](ram-map.md) flag system |
| `$01:CDA3`  | `0x00CDA3`| **Chest/shop bit-toggling** subroutine (`$01:CDA3-CDB3`)   |
| `$01:CDE6`  | `0x00CDE6`| **Item-ownership check** subroutine (`$01:CDE6-CDF5`)      |
| `$02:85F6`  | `0x0105F6`| Dialogue-text loading routine (`0x28` bytes)               |
| `$02:8810`  | `0x010810`| Name/place string reading routine — see [text-encoding.md](text-encoding.md) |
| `$02:D640`  | `0x015640`| Opening-animation call ([02:d64c])                         |
| `$02:DDD7`  | `0x015DD7`| Opening-logo subroutine                                    |
| `$02:DF2C`  | `0x015F2C`| Display opening logo                                       |
| `$02:DF4E`  | `0x015F4E`| Subroutine call                                            |

## Data — tables (with their own docs)

| SNES        | file       | What                                                  | Doc |
|-------------|-----------|-------------------------------------------------------|-----|
| `$01:D057`  | `0x00D057`| Atland weapon-shop inventory + prices                 | [game-data-tables.md](game-data-tables.md) |
| `$01:D07A`  | `0x00D07A`| Voloh weapon-shop prices                              | game-data-tables.md |
| `$01:D08A`  | `0x00D08A`| Denegal weapon-shop prices                            | game-data-tables.md |
| `$01:D3D9`  | `0x00D3D9`| **8-byte bitmask table** (`01 02 04 08 10 20 40 80`?) used by the flag code | [ram-map.md](ram-map.md) |
| `$01:E0B0`  | `0x00E0B0`| Base Strength table (`0x47` bytes)                    | [player-stats.md](player-stats.md) |
| `$01:E0F8`  | `0x00E0F8`| Base Defense table (`0x47` bytes)                     | player-stats.md |
| `$01:E140`  | `0x00E140`| Max-HP table (`0x23` = 35 bytes)                      | player-stats.md |
| `$01:E164`  | `0x00E164`| Max-MP table (`0x23` bytes)                           | player-stats.md |
| `$02:D9D9`  | `0x0159D9`| Starting **level** immediate                          | player-stats.md |
| `$02:D9EC`  | `0x0159EC`| Starting **Strength** immediate (shares HP load)      | player-stats.md |
| `$02:D9FB`  | `0x0159FB`| Starting **Defense** immediate                        | player-stats.md |
| `$02:DA01`  | `0x015A01`| Starting **Gold** (2 bytes)                           | player-stats.md |
| `$05:91F9`  | `0x0291F9`| **Monster** data table (`0x0F`/entry: HP/Gold/EXP)    | [game-data-tables.md](game-data-tables.md) |
| `$05:946B`  | `0x02946B`| **Boss** data table (`0x0F`/entry)                    | game-data-tables.md |
| `$05:CA81`  | `0x02CA81`| Atland Mayor's gold gift (2 bytes)                    | game-data-tables.md |

Note the leveling tables and starting-stat immediates above are exactly the ones
[player-stats.md](player-stats.md) reverse-engineered independently — Data Crystal
and our disassembly agree on every one.

## Text banks (bank $04)

Dialogue/string data, encoded per [text-encoding.md](text-encoding.md):

| SNES range          | file range            |
|---------------------|-----------------------|
| `$04:8200-$04:8670` | `0x020200-0x020670`   |
| `$04:8870-$04:D010` | `0x020870-0x025010`   |
| `$04:D210-$04:E680` | `0x025210-0x026680`   |
| `$04:E790-$04:F7D0` | `0x026790-0x0277D0`   |

## Graphics

| SNES range          | file range            | Content                         |
|---------------------|-----------------------|---------------------------------|
| `$11:8000-$11:B600` | `0x088000-0x08B600`   | Nasir character gfx (uncompressed) |
| `$11:B600-$11:BE00` | `0x08B600-0x08BE00`   | Shield gfx (uncompressed)       |
| `$17:8E00-$17:A800` | `0x0B8E00-0x0BA800`   | Item gfx (uncompressed)         |
| `$1D:E47A-$1D:E54F` | `0x0EE47A-0x0EE54F`   | Logo compression dictionary     |
| `$1D:E54F`          | `0x0EE54F`            | Logo gfx (compressed)           |

Compression is the **dictionary method** (a per-asset dictionary array sits
immediately before its compressed data); a lot of gfx is left uncompressed. See
[text-encoding.md](text-encoding.md) for the decompression scratch in WRAM.

## Audio

| Data Crystal entry            | Note |
|-------------------------------|------|
| `0x04:ff3d` → SNES `[0c:ff3d]`| "Load audio data to memory." **Inconsistent in Data Crystal**: file `0x04FF3D` LoROM-converts to `$09:FF3D`, not the listed `$0C:FF3D` (off by 3 banks). Not re-verified here. |
| `$0D:80A8` / `0x0680A8` (`0xC9C` bytes) | Audio code/data |

## Unused space

| SNES range          | file range            |
|---------------------|-----------------------|
| `$1F:FCA0-$1F:FFD0` | `0x0FFCA0-0x0FFFD0`   | Free ~0x330 bytes near the end of ROM — candidate for injected randomizer code/hooks. |

## See also

- [ram-map.md](ram-map.md) — WRAM, including the inventory/equipment/event flag system.
- [game-data-tables.md](game-data-tables.md) — monster/boss/shop data (randomizer targets).
- [text-encoding.md](text-encoding.md) — TBL + text banks.
- [debug-mode-and-versions.md](debug-mode-and-versions.md) — Edit Mode, regional diffs, unused content.