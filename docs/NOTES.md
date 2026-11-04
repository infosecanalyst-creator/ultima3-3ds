# Development notes

Working notes for u3-3ds: how it's put together, what was learned testing it, and what's
still open.

## How it fits together

- **Base:** MrHuu's dosbox-3ds (DOSBox SVN r4483), built with current devkitARM in Docker.
  Upstream build fixes: the ffmpeg rule for keyboard images, `smdhtool` instead of the
  missing `bannertool`, generated image headers before compiling, and relaxed warnings for
  the newer compiler. The `.cia` target is dropped; only `.3dsx` is built.
- **Video** (`src/platform/ctr/sdlvid/video/n3ds`): a vendored copy of the devkitPro
  3ds-sdl-1.2 video driver (LGPL). Its only modes were stretch or 1:1, so it was patched to
  draw the game into an explicit 4:3 rect (40,0 320x240) with 2x supersampling, then
  composite a 512x256 RGBA overlay (the info strips) in the same frame.
- **Audio** (`src/platform/ctr/sdlvid/audio/n3ds`): the same library's audio driver,
  vendored to raise its output buffers from 2 to 4.
- **Front end** (`src/platform/ctr/u3/`):
  - `u3_state` reads game state out of emulated memory
  - `u3_top` draws the info strips
  - `u3_ui` draws the bottom-screen tabs and handles input and key injection
  - `u3_map` is the auto-map
  - `u3_text` reads the message window off the screen and keeps the log
  - `u3_book` is the reader
  - `u3_gfx` is a small software 2D canvas, with fonts from `tools/mkfont.py`
    (DejaVu fonts)

## Game memory map (offsets from the EXODUS.BIN segment)

These come from Michael C. Maggio's disassembly notes (Exodus Project, `doc/u3tech`). The
base segment is found by scanning for the "Wrong Diskette" string at +0x2B19, with
"PARTY.ULT" at +0x15D4 as a second check.

| Offset | Meaning |
|---|---|
| 0x0100 | map data: Sosaria or town 64x64, or the dungeon's 8 levels of 16x16 |
| 0x1324 / 0x1325 | Trammel / Felucca phase (0-7) |
| 0x14BA | PARTY.ULT image; 0x14BC map id (0 Sosaria, 1 dungeon, 2 town, 3 castle, 4 shrine, 0x80 combat) |
| 0x14C1 | party size; 0x14C2/3 saved overworld x,y |
| 0x14CC | 4 x 64-byte player records (roster format: status +0x11, HP BCD +0x1A, max HP +0x1C, food BCD +0x21, marks/cards bits +0x0E) |
| 0x15CC / 0x15CD | current x / y; 0x15CF dungeon level |
| 0x16E1 | 19 (x,y) entrances for towns, castles and dungeons, in the order of `u3_place_names` |
| 0x2A92 | wind: 0 calm, 1 N, 2 E, 3 S, 4 W (checked against the game's own text) |
| 0x58CC | dungeon facing: 0 N, 1 E, 2 S, 3 W (step table at 0x76F0) |

## Lessons from testing

- **Input:** SDL's event pump also calls `hidScanInput()`, so `hidKeysDown()` loses presses.
  Edges are now worked out from the held state. Keys are injected with a 90 ms hold and a
  50 ms gap, which the game reads reliably. Letters typed on the keyboard are sent with
  Shift so they appear as capitals.
- **Audio crackle:** this was emulation falling behind, which starves the mixer. Fixed by:
  - polling input every 8 ms instead of every emulated millisecond
  - `cycles=fixed 3000` (Ultima III is fine at this)
  - the dynamic CPU core
  - 4 output buffers

  On a New 3DS XL the game then holds about 100% of real time with only occasional single
  underruns. The app logs speed and underruns every 10 s to `/3ds/u3-3ds/debug.log`.
- **HOME menu:** the app has to leave when `aptMainLoop()` returns false. Otherwise the
  system kills it about 30 s later.
- **Azahar:** needs a `sdmc:/3ds/dspfirm.cdc` file, or the app exits with a DSP error. Any
  file works there. Azahar runs this app at about 45-85% speed, so judge performance on
  real hardware.
- **Text reader:** CHARSET.VGA glyphs are matched to screen cells. ',' and '.' share a shape,
  and the reader prefers '.'.
- **ftpd:** some uploads take about 20 s each, apparently a periodic stall on the 3DS side.
  `tools/ftp-install.py` skips files that are already current, so re-running it resumes.

## Still to verify on hardware

- The dungeon auto-map's orientation and wall reveal. This hasn't been walked yet.
- Fast-forward (hold ZR), which locks DOSBox's ticks.

## Ideas not done

- Save states (this DOSBox has none).
- QWERTY order for the Commands tab (left alphabetical on purpose, to match the original
  reference card).
- Click sound and press animation on buttons, per the project-wide standard.
- Moving the book reader to the standard manual controls (Circle Pad and C-Stick scroll,
  D-Pad section buttons, L/R sections, B or START to close).
