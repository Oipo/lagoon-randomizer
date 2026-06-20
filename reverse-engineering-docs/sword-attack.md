# Sword attack — hit detection & reach (bank $01)

Lagoon's melee attack is an instantaneous **AABB (axis-aligned box) overlap test**:
each frame the player is in the attack state, the game builds a rectangular sword
hitbox in front of the player (from a per-direction offset table) and sweeps every
enemy object slot, dealing damage to any whose position falls inside the box.

There is **no projectile** — just a box anchored to the player. The "length of the
sword" is literally how far that box extends past the player in the facing
direction, and it lives in one small table: **`$01:B6F0`** (file offset `0x0B6F0`).

## Call chain

```
player object update ($01:A8C3 / $01:A9D0)
  └─ JSR $B5EE   attack sweep (this is the whole melee routine)
       gate: $0500 bit0 clear, $051B & $04 == 0, $0511 == 2, $0530 != 0
       ├─ JSR $B6BD              build the sword hitbox  -> scratch $40/$42/$44/$46
       ├─ LDY #$0040            start at object slot 1 (player is slot 0)
       │   loop, step Y += $40, until Y == $0840 (sweeps all enemy slots):
       │   ├─ filter: $0500,Y & $C8 == $C8 (active), $0501,Y & $83 == 0, JSR $B641
       │   ├─ JSR $B7AE          AABB test: is object Y inside the box?
       │   └─ (hit) PHY / JSR $B9A6 / PLY    apply the hit to victim Y
       └─ RTS
```

### The AABB test — `$01:B7AE`
16-bit A. Returns `$FF` on overlap, `$00` otherwise. `$40/$42` are the box's
left/right X bounds, `$44/$46` the top/bottom Y bounds:

```
LDA $0502,Y / CMP $40 / BCC miss / CMP $42 / BCS miss   ; X inside [left,right)?
LDA $0504,Y / CMP $44 / BCC miss / CMP $46 / BCS miss   ; Y inside [top,bottom)?
hit: LDA #$FFFF ; miss: LDA #$0000
```

### The hitbox builder — `$01:B6BD`  ← the reach lives here
```
LDA $050A            ; player attack-pose / facing index (0..7)
ASL ASL ASL          ; x8  (each entry = four signed 16-bit offsets = 8 bytes)
TAY
LDA $0502 + [$B6F0,Y] -> $40   ; box LEFT   = playerX + dx0
LDA $0502 + [$B6F2,Y] -> $42   ; box RIGHT  = playerX + dx1
LDA $0504 + [$B6F4,Y] -> $44   ; box TOP    = playerY + dy0
LDA $0504 + [$B6F6,Y] -> $46   ; box BOTTOM = playerY + dy1
```
`$050A` is the player's animation/facing index (set by the pose-setter at
`$02:80A0` from `$053F`). The box is `[playerX+dx0, playerX+dx1] x
[playerY+dy0, playerY+dy1]`, so the player position `$0502/$0504` is the box's
near corner and the offsets place/size the box. `$B6BD` is called from nowhere
except the attack sweep, so this table is exclusively the player's melee reach.

## The reach table — `$01:B6F0` (file `0x0B6F0`, 64 bytes = 8 entries x 4 signed words)

Directions match the game's convention (0=right,1=down,2=left,3=up). **Only rows
0-3 are reachable by the player's sword:** `$B6BD` indexes by the player's `$050A`,
and the pose-setter `$02:80A0` derives `$050A` from the table `$02:80DA`, whose
only non-negative entries are `{0,1,2,3}`. Rows 4-7 (a shorter variant) are never
selected by the player and are left untouched by the randomizer.

| Idx | Dir   | dx0 (left) | dx1 (right) | dy0 (top) | dy1 (bottom) | reach | file off |
|-----|-------|-----------|------------|-----------|--------------|-------|----------|
| 0   | right | +0        | **+28**    | -8        | +12          | 28 → right | `0x0B6F0` |
| 1   | down  | -24       | +8         | +0        | **+17**      | 17 ↓ down  | `0x0B6F8` |
| 2   | left  | **-28**   | +0         | -12       | +8           | 28 ← left  | `0x0B700` |
| 3   | up    | -8        | +24        | **-17**   | +0           | 17 ↑ up    | `0x0B708` |
| 4   | right | +0        | **+25**    | -8        | +8           | 25 → right | `0x0B710` |
| 5   | down  | -16       | +16        | +0        | **+15**      | 15 ↓ down  | `0x0B718` |
| 6   | left  | **-25**   | +0         | -8        | +8           | 25 ← left  | `0x0B720` |
| 7   | up    | -16       | +16        | **-15**   | +0           | 15 ↑ up    | `0x0B728` |

**The reach (how far the sword extends in front) is the bold "far edge":**
horizontal attacks reach **28 px**, vertical attacks reach only **17 px**. That
up/down stubbiness is the "really small sword".

### To set sword reach (the implemented randomizer option)

For each direction the near edge already sits on the player (offset 0), so the
reach equals the far-edge offset. Set it to an absolute pixel value `N` by writing
the signed 16-bit far-edge word of the relevant row 0-3 (right/down positive,
left/up negative):

| Dir   | Word file off | Field | Value written |
|-------|---------------|-------|---------------|
| right | `0x0B6F2`     | dx1   | `+N`          |
| down  | `0x0B6FE`     | dy1   | `+N`          |
| left  | `0x0B700`     | dx0   | `-N`          |
| up    | `0x0B70C`     | dy0   | `-N`          |

Example: rightward reach of 56 px → write `38 00` at `0x0B6F2`. Vanilla values
(28/17/28/17) write back the original bytes, so the default is a no-op.

This is exposed as `RandomizerOptions::sword_reach_{right,down,left,up}` (each
validated to `[1,128]`): CLI `--sword-reach right,down,left,up`, web four "Sword
reach" inputs (`swordReachRight/Down/Left/Up`). See `randomize_rom` in
`randomizer.hpp`. Rows 4-7 are intentionally not touched (unreachable by the
player, per the pose-table note above).

Notes:
- This only moves the *tip* (far edge); the perpendicular thickness is unchanged,
  so the sword gets longer, not wider. To also widen, adjust the perpendicular
  pair (dy0/dy1 for left/right attacks, dx0/dx1 for up/down).
- The sweep tests every enemy slot independently, so a longer box can hit multiple
  enemies at once — expected, and how it already behaves at vanilla length.

## Related routines (damage, for reference)

- `$01:B9A6` apply-hit (victim Y, attacker X): valid-target check `$01:BA52`,
  knockback direction from relative positions (`$01:B9C5`=L/R, `$01:B9DC`=U/D),
  then the damage formula.
- Damage formula `$01:BA0A`: `LDA $0524,X` (attacker Strength) and `$0526,Y`
  (defender Defense) → `$0370`/`$0372`, then `$01:BA6B` scales (`STR<<2`, `DEF<<1`)
  and `JSR $9226` to subtract from the victim's HP `$0520,Y`.
- Subtract-HP `$01:9226` is the same generic routine documented in
  [player-stats.md](player-stats.md); here X/Y is the enemy slot, not the player.

## Tooling

Disassembled with `.agents/lagoon_dis.py` (CDL-aware 65816 disassembler/scanner)
and `.agents/xref.py` (call/pointer xref), both keyed off `l.cdl`'s M/X flags.
