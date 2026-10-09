# Monk Tower PSP

Native PSP adaptation of Maciej Główka's MIT-licensed coffee-break roguelike
[Monk Tower](https://maciekglowka.itch.io/monk-tower)
([source](https://github.com/maciekglowka/tower-rl)). The sprite atlases in
`assets/` are the upstream originals; the game itself is rewritten in C for the
PSP.

## Release Candidate 2

- Original 8 x 8 board scale and original PNG sprite atlases
- Procedural rooms, walls, doors, stairs, items and enemies
- Turn-based movement, bump combat and enemy turns
- Fog of war and five-tile view range
- Four weapon slots with shoulder-button switching and durability
- Swords, poison dagger, axe, spear, stun wand, displacement wand and warhammer
- Rats, snakes, novices, monks and ranged librarians
- A pause/settings menu with music volume, restart, title and Exit to XMB
- Original procedural chiptune background music (no additional music asset license)
- Music automatically pauses while the pause/settings menu is open
- Reachability-checked staircase, item and enemy placement on every floor
- Six discoverable potion types and four consumable slots
- Complete thirteen-enemy roster with poison, stun, swapping, phasing and summoning
- Enemy loot, gold pickups, breakable vases, Forge and Herbalist upgrades
- Golden Sword, Green Hammer and the complete weapon set
- Pillars, three-stage spike hazards and the real Lost Scroll objective on floor 20
- Automatic turn saves, title-screen Continue and persistent music volume
- Procedural pickup, combat, use, stair, upgrade, victory and defeat sound cues
- In-game tower guide and end-of-run statistics
- Multiple procedurally generated floors
- PSP-native controls and a landscape HUD

This native adaptation now contains the complete core game loop and all major
upstream content categories. Exact upstream probabilities, presentation and
late-floor balance remain intentionally adapted for the PSP layout and controls.

## Controls

- D-pad or analog stick: move/attack/open
- L/R: switch active weapon slot
- Circle: switch active potion slot
- Square: use active potion
- Triangle: wait one turn
- Select: open/close the tower guide
- X: start/restart or choose a menu option
- Triangle on title: continue the automatically saved run
- Start: open/close pause and settings
- Circle: back from the menu

## Build and package

Requires the [PSPDEV](https://pspdev.github.io/) toolchain with SDL2 and
SDL2_image.

```sh
export PSPDEV=~/pspdev
export PATH="$PSPDEV/bin:$PATH"
make
```

Keep `EBOOT.PBP` and the `assets` directory together on the memory stick. If
`assets` is missing the game now says so on screen and names the first file it
could not find, rather than dropping straight back to the XMB.
Run and settings data are stored in `ms0:/PSP/SAVEDATA/MONKTOWER`.

## XMB art

`psp-xmb/ICON0.PNG` (144x80) is the thumbnail in the game list and
`psp-xmb/PIC1.PNG` (480x272) is the full-screen background the XMB fades in
once the title is highlighted. Both are exported from the `monktower` page of
the Art 4 Ports Figma file and stored as opaque 8-bit RGB -- the source frames
have no transparency, and dropping the alpha channel saves about a quarter of
the bytes.

The Makefile points `PSP_EBOOT_ICON`/`PSP_EBOOT_PIC1` at them and also names
them as prerequisites of `EBOOT.PBP`. That second part is not redundant:
pspsdk's `build.mak` passes the art to `pack-pbp` on the command line but never
declares it as a dependency, so replacing the art without the extra rule leaves
the old PBP in place.
