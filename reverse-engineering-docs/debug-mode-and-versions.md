# Debug "Edit Mode", regional & unused content

Game-level trivia and a leftover developer tool, from **The Cutting Room Floor**
(<https://tcrf.net/Lagoon_(SNES)>). Less about randomizer internals, more about
what the developers left in — but the **Edit Mode** is a genuinely useful testing
aid (warp + level + inventory) when verifying randomizer output.

> **Source / confidence.** From TCRF via web search (the TCRF article fetched
> through a content proxy returned an unrelated prompt-injection page, so this was
> recovered by search and **not yet re-verified** against our ROM). The codes are
> for **US/EU**; addresses are SNES.

## Edit Mode (debug menu) — `$02:DA0D-DB89`

A leftover debug/edit menu lives just after the "New Game" intro, at SNES
**`$02:DA0D-$02:DB89`** (file `0x015A0D-0x015B89`). It is normally unreachable but
can be entered on the **US and European** versions:

1. Have an existing save file.
2. Enable one of the cheats below.
3. Select **Continue** on the title screen.

| Cheat type            | Code(s)                       |
|-----------------------|-------------------------------|
| Game Genie            | `F620-07AC` + `2C29-0DDC`     |
| Pro Action Replay     | `02D94F18` + `02D950DA`       |

Once in, you can:

- enable **full inventory**,
- **modify the current level**,
- **warp to any map**.

Storyline flags are taken from the loaded save (so which events are "done" depends
on your save file — see the event flags in [ram-map.md](ram-map.md)).

> The Edit Mode lands in the same `$02:D9xx-DAxx` region as the **new-game init**
> and the **starting-stat immediates** documented in
> [player-stats.md](player-stats.md) (`$02:D9CC` init, `$02:D9D9` starting level,
> etc.) — useful context if you ever single-step that area.

### German-only debug leftover

The **German** release keeps an extra debug feature removed from all other
versions: PAR `028256EA` makes every dialogue event print its **event number**
on-screen after the dialogue. (Maps to the event-flag system in
[ram-map.md](ram-map.md).)

## Unused content

- **Three unused item slots** named **"Armor Shop"** (EN) / **"ぶきや" (Bukiya)**
  (JP) / **"RÜSTUNGSLADEN"** (DE). They are all set to *true* when Edit Mode /
  debug is enabled, i.e. they appear in the debug full-inventory but are not
  obtainable in normal play. These correspond to the otherwise-blank slots in the
  item-flag bytes (e.g. the "unused" `$7E:04D5/04D6` and the "final two items" at
  `$7E:04D7` in [ram-map.md](ram-map.md)).

## Regional differences (US/EU vs JP)

- **Kemco screen:** the US and European versions add **"Licensed by Nintendo"** to
  the Kemco logo screen.
- **Atland church priest:** the priest at Atland's church was **edited to remove
  religious connotations** in the western releases.

## See also

- [ram-map.md](ram-map.md) — the inventory/event flag bytes the Edit Mode toggles.
- [player-stats.md](player-stats.md) — the `$02:D9xx` new-game init right beside the Edit Mode.
- [rom-map.md](rom-map.md) — bank/address conventions.