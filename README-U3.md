# u3-3ds — Ultima III: Exodus for the New 3DS

A fork of [MrHuu/dosbox-3ds](https://github.com/MrHuu/dosbox-3ds) that runs the MS-DOS
Ultima III with the [Exodus Project](https://bitbucket.org/mcmagi/ultima-exodus) upgrade,
with a front end built for the 3DS's two screens:

- Top screen: the game at true 4:3 (320x240, 2x supersampled), with info strips for moon
  phases, wind, party health, food, and Marks/Cards collected.
- Bottom screen: a touch command pad for all 26 commands, an auto-map that draws itself as
  you explore (Sosaria, towns, and every dungeon level), a message log with pinnable clues,
  a typing helper, and a built-in library (3DS guide, spell list, and the original manuals
  generated from your own GOG copy).

Game state is read from emulated memory using Michael C. Maggio's EXODUS.BIN disassembly
notes (Exodus Project, doc/u3tech).

## Build

    docker build -t u3build tools/   # devkitARM + ffmpeg
    docker run --rm -v $PWD:/src -w /src u3build make -f Makefile.ctr

## SD card layout

    /3ds/u3-3ds/u3-3ds.3dsx
    /3ds/u3-3ds/u3.conf          (dist/3ds/u3-3ds/u3.conf)
    /3ds/u3-3ds/game/            Ultima III + Exodus Project upgrade (not included)
    /3ds/u3-3ds/books/           guide.txt, spells.txt (included) + manual scans (generated)

Ultima III is © Electronic Arts. No game data or original manuals are included.
Code is GPL-2.0 (DOSBox); the vendored SDL 1.2 video driver is LGPL-2.1; fonts are DejaVu.
