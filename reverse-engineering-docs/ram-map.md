# RAM map (WRAM `$7E`/`$7F`)

WRAM layout, transcribed from the **Data Crystal** RAM map
(<https://datacrystal.tcrf.net/wiki/Lagoon_(SNES)/RAM_map>) and reconciled with
this project's runtime-verified docs. The big new content here vs. our other docs
is the **inventory / equipment / event flag system** (`$7E:04D0-04E5`) — the
natural target for an item-shuffle / progression randomizer.

> **Reconciliation note.** Data Crystal lists several fields as **1 byte** that
> this project has confirmed at runtime are **16-bit** (HP, position, etc.). The
> *addresses* agree; Data Crystal just documents the low byte. Where the two
> conflict on width, **our runtime-verified docs win**
> ([player-stats.md](player-stats.md), [town-movement.md](town-movement.md)).

## Inventory & equipment flags — bit-packed booleans

Each item/piece of equipment the player owns is a single **bit**. The
chest/shop-pickup code (`$01:CDA3`, see [rom-map.md](rom-map.md)) sets a bit using
the 8-byte bitmask table at `$01:D3D9` (`01 02 04 08 10 20 40 80`-style), and the
ownership check `$01:CDE6` tests it. Bit order is listed high→low (bit 7 first)
as Data Crystal documents it.

### Equipment — `$7E:04D0-04D3`

| Addr        | Bits  | Items (per bit)                                                                 |
|-------------|-------|--------------------------------------------------------------------------------|
| `$7E:04D0`  | 6-0   | short sword, silver sword, magic sword, force sword, moon blade, bandit armor, gold armor |
| `$7E:04D1`  | 7-0   | sonic armor, thunder armor, moon armor, iron shield, large shield, great shield, maxim shield, moon shield |
| `$7E:04D2`  | 7-0   | protective ring, power ring, defensive ring, curing ring, time ring, earth staff, sky staff, star staff |
| `$7E:04D3`  | 7-3   | moon staff, fire crystal, wind crystal, water crystal, thunder crystal          |

> `$7E:04D2 bit 1` is reused as an **event** flag (see below), so it is shared
> between equipment and event meaning per Data Crystal — verify before touching.

### Consumable / quest items — `$7E:04D4-04D7`

| Addr        | Bits  | Items                                                                            |
|-------------|-------|----------------------------------------------------------------------------------|
| `$7E:04D4`  | 7-0   | healing pot, shiny ball, shiny stone, elixir, bright stone, life ball, Samson key, key of Philips |
| `$7E:04D5`  | —     | (unused)                                                                         |
| `$7E:04D6`  | —     | (unused)                                                                         |
| `$7E:04D7`  | 7-6   | final two items                                                                  |

## Event / progression flags — `$7E:04E0-04E5`

Story progression. Routine `$01:CD95` explicitly tests `$7E:04E2`.

| Addr        | Bits      | Events (per bit)                                                                                |
|-------------|-----------|------------------------------------------------------------------------------------------------|
| `$7E:04D2`  | 1         | Spoke to mayor of Voloh & received moveable mantle                                              |
| `$7E:04E0`  | 7-4       | Defeated Samson, Natela, Eardon, Duma                                                           |
| `$7E:04E1`  | 1-0       | Followed by Thor, followed by Giles                                                             |
| `$7E:04E2`  | 7,5,4,3,2,0 | Giles returned to Atland; went to cleric & mayor; talked to mayor outside Gold Cave; visited mayor's house at game start; talked to mayor at house; talked to Voloh elder about tablets |
| `$7E:04E3-04E5` | —     | (unused)                                                                                       |

## Shop state

| Addr        | What                                       |
|-------------|--------------------------------------------|
| `$7E:0459`  | Weapon-shop inventory array (start)         |

## Player object (`$7E:0500` block)

The player is object 0 of the 33-slot object array (base `$0500`, stride `$40`) —
full layout in [object-update-loop.md](object-update-loop.md). Data Crystal's
stat/position entries, reconciled with our verified widths:

| Addr        | DC label              | Reconciled (this project)                                  |
|-------------|-----------------------|------------------------------------------------------------|
| `$7E:0502`  | Horizontal coord (1b) | **World X, 16-bit** ([town-movement.md](town-movement.md)) |
| `$7E:0504`  | Vertical coord (1b)   | **World Y, 16-bit** (town-movement.md)                     |
| `$7E:0520`  | HP current (2b)       | HP, 16-bit ✓ ([player-stats.md](player-stats.md))          |
| `$7E:0522`  | MP current (1b)       | MP, **16-bit** (player-stats.md)                           |
| `$7E:0524`  | Strength (1b)         | Strength, **16-bit** (player-stats.md)                     |
| `$7E:0526`  | Defense (1b)          | Defense, **16-bit** (player-stats.md)                      |
| `$7E:0528`  | Gold (2b)             | Gold, 16-bit ✓                                             |
| `$7E:052A`  | Experience (2b)       | Experience, 16-bit ✓ (clamped to `$FFFF`)                  |
| `$7E:052C`  | Level (1b)            | Current level ✓ (max 35) — indexes the leveling tables     |
| `$7E:0530`  | **Selected weapon** (1b) | Class/equipment **bonus index** in our docs — DC's "selected weapon" label **corroborates** the equipment hypothesis in player-stats.md |
| `$7E:0536`  | **Selected item** (1b)   | Set to `$FFFF` by new-game init in our docs; DC reads it as "selected item" |
| `$7E:1011`  | Qty HP healed while resting | **Confirms** `$1011` = the HP add/subtract delta in player-stats.md |

The `$0530` / `$0536` / `$1011` agreements above are useful: they independently
back up player-stats.md's open questions (`$0530` is equipment-related; `$1011` is
the heal/damage delta).

## Decompression / dialogue scratch (`$7F`)

| Addr                                   | What                                                       |
|----------------------------------------|------------------------------------------------------------|
| `$7E:5000`                             | Dialogue text buffer (first byte of array)                 |
| `$7F:2000, 2110, 2220, 2330, 2440, 2800` | Decompression-dictionary working arrays                   |
| `$7F:7F7C`                             | Temp storage for graphics after decompression              |

## See also

- [rom-map.md](rom-map.md) — ROM side, including the flag code (`$01:CDA3/CDE6`) and bitmask table.
- [player-stats.md](player-stats.md) / [town-movement.md](town-movement.md) — runtime-verified stat/position details.
- [game-data-tables.md](game-data-tables.md) — the item/equipment names map to shop and gfx data here.