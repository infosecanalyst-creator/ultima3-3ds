// u3_ui.h - Ultima III 3DS front end: bottom-screen UI, input, top strips
#ifndef U3_UI_H
#define U3_UI_H

#include <stdint.h>

#define U3_DIR "/3ds/u3-3ds"
#define U3_GAME_DIR U3_DIR "/game"
#define U3_BOOK_DIR U3_DIR "/books"

struct U3Settings {
	bool strips;        // show top-screen info strips
	bool turbo_toggle;  // ZR toggles fast-forward instead of hold
	bool swap_ab;       // A=Esc, B=Enter
	bool cluebook;      // spoiler cluebook unlocked in the book list
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

// Key injection
void u3_key_tap(char c);                // queue a key press for an ASCII char
void u3_key_special(int kbd_key);       // queue a KBD_KEYS value

// Book reader (u3_book.cpp)
struct U3Canvas;
void u3_book_draw(U3Canvas &c, int y0);
void u3_book_touch(int x, int y, int phase);   // phase 0 down, 1 drag, 2 up
bool u3_book_button(uint32_t keys_down);       // returns true if consumed
void u3_book_tick();                           // circle pad scrolling etc.
bool u3_book_dirty();
void u3_book_open_spells(char kind);           // 'W' wizard, 'C' cleric

#endif
