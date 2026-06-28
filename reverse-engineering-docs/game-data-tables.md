# Game data tables — monsters, bosses, shops

The data-driven content that a balance/economy randomizer would target: enemy
HP/Gold/EXP, boss rewards, and weapon-shop inventories & prices. Transcribed from
the **Data Crystal** ROM map (<https://datacrystal.tcrf.net/wiki/Lagoon_(SNES)/ROM_map>).
Addresses given as `SNES $bank:addr / file 0xNNNNNN` (LoROM conversion in
[rom-map.md](rom-map.md)).

> **Source / confidence.** Transcribed from Data Crystal, **not yet re-verified**
> against our ROM or with the disassembler. The per-entry field offsets below are
> inferred from Data Crystal's worked examples (the four named monsters land on a
> consistent `0x0F` stride). Confirm with a *read* breakpoint in MesenCE before
> wiring up a randomizer option.

## Monster table — `$05:91F9` / file `0x0291F9`

Entries are **`0x0F` (15) bytes** each, contiguous. Field offsets within an entry
(little-endian 16-bit), inferred from the worked examples:

| Offset | Field         |
|--------|---------------|
| `+0x00`| (unidentified)|
| `+0x01`| **HP** (2b)   |
| `+0x09`| **Gold** (2b) |
| `+0x0B`| **EXP** (2b)  |
| `+0x0D`| (unidentified, 2b) |

First four entries (the ones Data Crystal names):

| # | Monster         | Entry base (SNES / file) | HP off | Gold off | EXP off |
|---|-----------------|--------------------------|--------|----------|---------|
| 0 | Gold Cave Demon | `$05:91F9` / `0x0291F9`  | `91FA` | `9202`   | `9204`  |
| 1 | Slime           | `$05:9208` / `0x029208`  | `9209` | `9211`   | `9213`  |
| 2 | Skeleton        | `$05:9217` / `0x029217`  | `9218` | `9220`   | `9222`  |
| 3 | Beetle          | `$05:9226` / `0x029226`  | `9227` | `922F`   | `9231`  |

(The offsets in the last three columns are SNES `$05:xxxx`; subtract `0x8000` and
add `0x028000` for the file offset, e.g. `$05:9202` = file `0x029202`.) The table
continues past Beetle at the same `0x0F` stride for the remaining regular enemies.

## Boss table — `$05:946B` / file `0x02946B`

Also `0x0F`-byte entries. Data Crystal names the gold/EXP reward fields for these
bosses (HP presumably at the same `+0x01` slot as regular monsters):

| Boss     | Entry base (SNES / file) |
|----------|--------------------------|
| Samson   | `$05:946B` / `0x02946B`  |
| Natela   | `$05:947A` / `0x02947A`  |
| Eardon   | `$05:9489` / `0x029489`  |
| Duma     | `$05:9498` / `0x029498`  |
| Thimale  | `$05:94C5` / `0x0294C5`  |
| Battler  | `$05:94D4` / `0x0294D4`  |
| Ella     | `$05:953D` / `0x02953D`  |

The named bosses are `0x0F` apart in runs (Samson→Duma are consecutive), but there
are **gaps** — Duma `$9498` → Thimale `$94C5` is `0x2D` (= 3 × `0x0F`), and
Battler `$94D4` → Ella `$953D` is `0x69` (= 7 × `0x0F`) — so the table holds
several **unnamed** intermediate boss entries between them.

## Weapon shops — bank `$01`

| Shop   | Inventory / prices (SNES / file)         | Notes                                  |
|--------|------------------------------------------|----------------------------------------|
| Atland | inventory `$01:D057` / `0x00D057` (3 items); prices `$01:D05A` / `0x00D05A` | 3-item starter shop |
| (ref)  | `$01:D06A` / `0x00D06A`                   | shop-prices reference                   |
| Voloh  | prices `$01:D07A` / `0x00D07A`           |                                        |
| Denegal| prices `$01:D08A` / `0x00D08A`           |                                        |

Inventory bytes are item IDs (the same items enumerated as equipment flags in
[ram-map.md](ram-map.md)); prices are the cost values. The runtime shop-inventory
state lives at `$7E:0459` (ram-map.md), and pickups/purchases set the ownership
bit via `$01:CDA3` + the `$01:D3D9` bitmask table.

## Misc reward

| Reward                  | SNES / file              | Size |
|-------------------------|--------------------------|------|
| Atland Mayor's gold gift| `$05:CA81` / `0x02CA81`  | 2 b  |

## Randomizer relevance

- **Monster/boss HP, Gold, EXP** — direct difficulty/economy knobs; 16-bit fields,
  trivially rewritable once the per-entry offsets are confirmed.
- **Shop inventory & prices** — item-availability and economy shuffling.
- **Starting gold / Mayor's gift** — early-game economy seeds (starting stats are
  in [player-stats.md](player-stats.md)).

## See also

- [rom-map.md](rom-map.md) — full address index and notation key.
- [ram-map.md](ram-map.md) — item/equipment IDs (the flag system) these tables reference.
- [player-stats.md](player-stats.md) — player-side leveling/stat tables.