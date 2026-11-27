# Changelog

(DOSBox's own history is in `ChangeLog`.)

## 2026-11-22

- Title screens: the Commands tab now shows what the game asks for. "Press <Space>" is one big Continue button; the game's menus ("Option:") become one button per entry with the game's own words (Journey Onward, Organize a Party...). All commands brings the letter pad back.
- Start a New Game (tapped on that menu) asks first, with NO chosen.
- B does nothing at the title menus (the game took Esc as "Return to the View" and restarted the title).
- Manual: an open book now shows its page on the top screen and its sections on the whole touch screen (two columns), like the other ports. With the screens swapped it stays as it was.
- Manual: section names that did not fit go on two lines instead of being cut with "..."; five taller rows and a scrollbar.
- START menu footer uses the same words as the other ports and follows left-handed play and Swap A and B. It is one size larger.
- Exit question tells you how long ago you last saved.
- Brighter small text: strip labels, command names, the manual's footer; the Settings note is normal size. "Pass" is now "Space". Descend and Transact keep a margin.
- Guide updated (starting a game, strips off fills the screen).

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
