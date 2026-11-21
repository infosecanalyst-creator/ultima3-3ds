# Changelog

(DOSBox's own history is in `ChangeLog`.)

## 2026-11-21

- If the power is cut while the port's own files (map, clues, settings) are being written, the next start puts the previous good copy back instead of losing them.
- Game files DOSBox still has open when the game ends (EXIT, HOME > Close) are written out before the SD card is unmounted.
- The README controls are up to date.
- A compiler warning class in the port's memory code is fixed (types only; no behaviour change).

## 2026-11-20

- Every SD write is checked. A failed write shows "Save failed (SD card)", and Exit game then asks "Not saved. Exit anyway?" with NO chosen.
- The port's own files are written to a temporary file first and swapped in, so a failed write keeps the old file.
- The front page is the port's README (the DOSBox manual moved to docs/README-DOSBox.txt).
- Install path: the program is /3ds/u3-3ds.3dsx. The START menu header shows the build version.
- Exit no longer saves from the exit hook; it waits for every write and stops the writer before the SD card is unmounted (the cause of some lost saves).
- HOME and sleep ask the game to save at its next step rather than saving inside the hook.

## 2026-11-19

- Exit waits for every write to finish and joins the writer thread.
