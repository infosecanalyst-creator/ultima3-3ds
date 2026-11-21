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

## Download

CI builds every push to `main`: the latest `u3-3ds.3dsx` (with its build log and
`u3-3ds.elf.gz` / `u3-3ds.sym.gz` symbols) is on the
[`builds` branch](https://github.com/infosecanalyst-creator/ultima3-3ds/tree/builds);
`SOURCE_COMMIT` names the commit. The START menu shows the build version in its header.

## Build

    docker build -t u3build tools/   # devkitARM + ffmpeg
    docker run --rm -v $PWD:/src -w /src u3build make -f Makefile.ctr

## Install over Wi-Fi

1. Make a staging folder that mirrors the SD card. Put `u3-3ds.3dsx` directly in
   `<stage>/3ds/` (as `<stage>/3ds/u3-3ds.3dsx`), and copy everything under
   `dist/3ds/u3-3ds/` plus your own patched Ultima III folder (as `game/`) into
   `<stage>/3ds/u3-3ds/`.
2. Optional: build the original-manual pages from your GOG PDFs with
   `tools/make-books.sh <pdf-folder> <stage>`.
3. Start ftpd on the 3DS, then run `python3 tools/ftp-install.py <stage> <3ds-ip>`.

## Tools

- `tools/Dockerfile` - build image (devkitARM + ffmpeg)
- `tools/mkfont.py` - regenerates `src/platform/ctr/u3/u3_fonts.c` from DejaVu fonts
- `tools/make-books.sh`, `tools/ocr-to-book.py` - manual pages and large-text Book of Play from your PDFs
- `tools/ftp-install.py` - incremental FTP install/update; prints the on-device perf log

See `docs/NOTES.md` for the memory map, testing lessons and open items.

## SD card layout

    /3ds/u3-3ds.3dsx             the app (directly in /3ds)
    /3ds/u3-3ds/u3.conf          (dist/3ds/u3-3ds/u3.conf)
    /3ds/u3-3ds/game/            Ultima III + Exodus Project upgrade (not included)
    /3ds/u3-3ds/books/           guide.txt, spells.txt (included) + manual scans (generated)

**Bring your own game files:** you need your own copy of Ultima III (e.g. the GOG release) with the
Exodus Project upgrade applied. Ultima III is © Electronic Arts. No game data or original manuals
are included.
Code is GPL-2.0 (DOSBox); the vendored SDL 1.2 video driver is LGPL-2.1; fonts are DejaVu.

The upstream DOSBox manual that used to be this repository's front page is now
[`docs/README-DOSBox.txt`](docs/README-DOSBox.txt).
