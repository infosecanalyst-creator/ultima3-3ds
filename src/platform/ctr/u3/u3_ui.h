// u3_ui.h - Ultima III 3DS front end: bottom-screen UI, input, top strips
#ifndef U3_UI_H
#define U3_UI_H

#include <stdint.h>

#define U3_DIR "sdmc:/3ds/u3-3ds"
#define U3_GAME_DIR U3_DIR "/game"
#define U3_BOOK_DIR U3_DIR "/books"

struct U3Settings {
	bool strips;        // show top-screen info strips
	bool turbo_toggle;  // ZR toggles fast-forward instead of hold
	bool swap_ab;       // A=Esc, B=Enter
	bool cluebook;      // spoiler cluebook unlocked in the book list
	bool lefty;         // left-handed: D-pad and A/B/X/Y swap places
	bool swapped;       // game on the touchscreen, this panel on the top screen (ZL / SELECT)
};
extern U3Settings u3_settings;

uint32_t u3_ticks_ms();
bool u3_turbo_active();

// Lifecycle (called from the DOSBox 3DS platform layer)
void u3_ui_input();                     // every GetInput() call (~1 kHz); does its own throttling
void u3_ui_on_frame(const uint16_t *pix, int pitch_px, int w, int h);  // each emulated frame
void u3_ui_save_all();                  // persist map, pins, settings (on exit / suspend)

// Top strips (u3_top.cpp)
void u3_top_update(bool force);
void u3_top_set_panel(const uint16_t *px);   // non-null: show this 320x240 panel on the top screen
void u3_top_panel_changed();                 // the panel was redrawn
void u3_top_loading(bool on, int files);     // loading screen over the game until it draws
void u3_top_working(int kind, uint32_t now); // 0 none, 1 "Working...", 2 "Saving..."

// Button feedback (u3_sfx.cpp / u3_ui.cpp): every enabled touch button clicks quietly
// when pressed and shows a press animation (held: pushed in; released: brief flash).
struct U3Canvas;
void u3_click();
void u3_press_down(int id);            // touch-down on an enabled button: click
void u3_press_up(int id);              // released on the same button: start the flash
int  u3_press_look(int id, bool held); // 0 normal, 1 held, 2..256 flash (alpha + 1)
// Draws a button background for the given look; returns the y offset for its content.
int  u3_btn_bg(U3Canvas &c, int x, int y, int w, int h, int r, uint32_t bg, int look);
bool u3_press_animating();

// Key injection. Presses are never queued: a press is only taken when the game has
// taken the previous one; otherwise it is dropped. Returns whether it was taken.
bool u3_key_tap(char c);

// Book reader (u3_book.cpp)
struct U3Canvas;
void u3_book_draw(U3Canvas &c, int y0);
void u3_book_touch(int x, int y, int phase);   // phase 0 down, 1 drag, 2 up
// Buttons while the Book tab is open. Returns 0 if not used, 1 if used,
// 2 if the player asked to close the manual (B or START).
int  u3_book_buttons(uint32_t keys_down, uint32_t keys_held);
void u3_book_tick();                           // Circle Pad / C-Stick scrolling (call often)
bool u3_book_dirty();
void u3_book_open_spells(char kind);
void u3_book_open_guide();

#endif
