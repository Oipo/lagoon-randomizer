# Lagoon (SNES) reverse-engineering docs

Findings for the Lagoon randomizer. Two kinds of page:

**Analysis** — this project's own disassembly/runtime RE (Mesen2 + `.agents/`
tools, CDL-aware). High confidence, often runtime-verified.

**Reference** — transcribed from external wikis (TCRF / Data Crystal) and
cross-checked against the analysis pages. Leads, not yet all re-verified here.

## Analysis pages

| Page | Topic |
|------|-------|
| [player-stats.md](player-stats.md) | HP/MP/Str/Def/Gold/Exp/Level, leveling tables, regen, new-game init |
| [town-movement.md](town-movement.md) | Player position, velocity, speed-tier table, walk-speed option |
| [sword-attack.md](sword-attack.md) | Melee AABB hit test, reach table, sword-reach option |
| [object-update-loop.md](object-update-loop.md) | Per-frame object/AI pipeline, object struct, performance |
| [camera-fix.md](camera-fix.md) | The hardcoded camera-fix IPS patch |
| [screen-fade.md](screen-fade.md) | INIDISP brightness fades, fast-fade option |

## Reference pages (TCRF / Data Crystal)

| Page | Topic |
|------|-------|
| [rom-map.md](rom-map.md) | Master ROM address index, notation/LoROM conversion key |
| [ram-map.md](ram-map.md) | WRAM map + the inventory/equipment/event **flag system** |
| [game-data-tables.md](game-data-tables.md) | Monster/boss HP·Gold·EXP, shop inventories & prices |
| [text-encoding.md](text-encoding.md) | TBL character map, control codes, text banks |
| [debug-mode-and-versions.md](debug-mode-and-versions.md) | Edit Mode debug, regional diffs, unused content |

Source wikis: <https://tcrf.net/Lagoon_(SNES)> and
<https://datacrystal.tcrf.net/wiki/Lagoon_(SNES)>.