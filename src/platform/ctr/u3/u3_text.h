// u3_text.h - reads Ultima III's message window off the emulated screen and
// keeps a scrollable transcript plus the player's pinned clues.
#ifndef U3_TEXT_H
#define U3_TEXT_H

#include <stdint.h>
#include <string>
#include <vector>

struct U3Line {
	std::string text;
	std::string where;   // location when it appeared
	bool pinned;
};

extern std::vector<U3Line> u3_log;      // full message history (bounded)
extern uint32_t u3_log_changes;

bool u3_text_init(const char *charset_path);   // CHARSET.VGA
// Feed the current 320x200 RGB565 frame (call ~10x/sec)
void u3_text_scan(const uint16_t *pix, int pitch_px, int w, int h);
// Current window contents (7 lines of 16 chars)
const char *u3_text_window_line(int i);
bool u3_text_window_has(const char *needle);   // case-insensitive, any line

void u3_pins_load(const char *path);
void u3_pins_save(const char *path);
// Words useful for the typing helper (from pinned lines + things typed before)
void u3_words(std::vector<std::string> &out);
void u3_word_typed(const std::string &w);

#endif
