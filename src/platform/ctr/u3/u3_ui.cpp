// u3_ui.cpp - Ultima III 3DS front end: bottom-screen UI and input
#include "u3_ui.h"
#include "u3_gfx.h"
#include "u3_state.h"
#include "u3_text.h"
#include "u3_map.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <deque>
#include <vector>
#include <string>

#include "keyboard.h"

extern bool ticksLocked;   // DOSBox fast-forward

U3Settings u3_settings = { true, false, false, false };

uint32_t u3_ticks_ms() { return (uint32_t)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000)); }

// ------------------------------------------------------------------ palette
enum : uint32_t {
	C_BG = 0x111318, C_PANEL = 0x1B1E25, C_BTN = 0x262A33, C_BTN_HI = 0x343944, C_BTN_DOWN = 0x4A5263,
	C_EDGE = 0x353A46, C_TEXT = 0xECE8DE, C_DIM = 0x8C919D, C_FAINT = 0x5A6070, C_ACCENT = 0xE8B04A,
	C_ACCENT_BG = 0x3A2F1A, C_GOOD = 0x5CCB6A, C_INFO = 0x7FD4FF
};

// ------------------------------------------------------------------ canvas
static uint16_t bottom_px[320 * 240];
static U3Canvas cv;
static bool inited = false;
static bool dirty = true;
static uint32_t last_draw = 0;

// ------------------------------------------------------------------ buttons (immediate mode)
struct Btn { int x, y, w, h, id; };
static std::vector<Btn> btns;
static int touch_id = -1;        // button under the stylus at touch-down
static bool touching = false;
static int touch_x0, touch_y0, touch_x, touch_y;
static uint32_t touch_t0;

enum {
	ID_NONE = 0, ID_TAB = 100, ID_CMD = 200, ID_NUM = 300, ID_KEY = 400, ID_CHIP = 5000,
	ID_PASS = 700, ID_ESC, ID_ENTER, ID_KBD, ID_DIGITS, ID_SPELLS,
	ID_KB_SPACE = 720, ID_KB_BACK, ID_KB_ENTER, ID_KB_CLOSE, ID_KB_MODE, ID_KB_ESC,
	ID_MAP_LEVEL = 740, ID_MAP_ZOOM = 760, ID_MAP_CENTER, ID_MAP_PLACES, ID_MAP_PLACE = 780, ID_MAP_BACK = 810,
	ID_LOG_FILTER = 820, ID_LOG_LINE = 1000,
	ID_MENU = 900, ID_CONFIRM_YES = 950, ID_CONFIRM_NO,
};

static void reg(int x, int y, int w, int h, int id) { Btn b = { x, y, w, h, id }; btns.push_back(b); }
static int hit(int x, int y) {
	for (int i = (int)btns.size() - 1; i >= 0; i--) {
		const Btn &b = btns[i];
		if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return b.id;
	}
	return ID_NONE;
}

static void button(int x, int y, int w, int h, int id, const char *label, const u3_font &f = font_ui,
                   uint32_t bg = C_BTN, uint32_t fg = C_TEXT) {
	bool down = touching && touch_id == id && hit(touch_x, touch_y) == id;
	cv.fill_round(x, y, w, h, 5, down ? C_BTN_DOWN : bg);
	int tw = u3_text_width(f, label);
	cv.text(f, x + (w - tw) / 2, y + (h - f.height) / 2, label, fg);
	reg(x, y, w, h, id);
}

// ------------------------------------------------------------------ key injection
struct KeyEv { KBD_KEYS k; bool shift; };
static std::deque<KeyEv> keyq;
static int key_phase = 0;          // 0 idle, 1 down, 2 gap
static uint32_t key_time = 0;
static KBD_KEYS key_cur = KBD_NONE;
static bool key_shift = false;

static KBD_KEYS ascii_key(char c) {
	static const KBD_KEYS letters[26] = { KBD_a, KBD_b, KBD_c, KBD_d, KBD_e, KBD_f, KBD_g, KBD_h, KBD_i, KBD_j,
		KBD_k, KBD_l, KBD_m, KBD_n, KBD_o, KBD_p, KBD_q, KBD_r, KBD_s, KBD_t, KBD_u, KBD_v, KBD_w, KBD_x, KBD_y, KBD_z };
	static const KBD_KEYS digits[10] = { KBD_0, KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9 };
	if (c >= 'a' && c <= 'z') return letters[c - 'a'];
	if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
	if (c >= '0' && c <= '9') return digits[c - '0'];
	switch (c) {
	case ' ': return KBD_space; case '\n': case '\r': return KBD_enter; case 27: return KBD_esc;
	case 8: return KBD_backspace; case '-': return KBD_minus; case '.': return KBD_period; case ',': return KBD_comma;
	case '\'': return KBD_quote; case '/': return KBD_slash; case ';': return KBD_semicolon; case '=': return KBD_equals;
	}
	return KBD_NONE;
}
void u3_key_tap(char c) { KBD_KEYS k = ascii_key(c); if (k != KBD_NONE) { KeyEv e = { k, false }; keyq.push_back(e); } }
static void key_tap_upper(char c) { KBD_KEYS k = ascii_key(c); if (k != KBD_NONE) { KeyEv e = { k, c >= 'A' && c <= 'Z' }; keyq.push_back(e); } }
void u3_key_special(int k) { KeyEv e = { (KBD_KEYS)k, false }; keyq.push_back(e); }

static void dbg(const char *fmt, int a, int b) {
	static int lines = 0;
	if (lines > 400) return;
	FILE *f = fopen(U3_DIR "/debug.log", "a");
	if (!f) return;
	fprintf(f, "%u ", (unsigned)u3_ticks_ms()); fprintf(f, fmt, a, b); fputc('\n', f); fclose(f); lines++;
}

static void key_pump(uint32_t now) {
	switch (key_phase) {
	case 0:
		if (keyq.empty()) return;
		key_cur = keyq.front().k; key_shift = keyq.front().shift; keyq.pop_front();
		if (key_shift) KEYBOARD_AddKey(KBD_leftshift, true);
		KEYBOARD_AddKey(key_cur, true);
		key_phase = 1; key_time = now;
		break;
	case 1:
		if (now - key_time >= 90) { KEYBOARD_AddKey(key_cur, false); if (key_shift) KEYBOARD_AddKey(KBD_leftshift, false); key_phase = 2; key_time = now; }
		break;
	case 2:
		if (now - key_time >= 50) key_phase = 0;
		break;
	}
}

// ------------------------------------------------------------------ turbo
static bool turbo_on = false;
bool u3_turbo_active() { return turbo_on; }
static void set_turbo(bool on) {
	if (on == turbo_on) return;
	turbo_on = on;
	ticksLocked = on;
	u3_top_update(true);
}

// ------------------------------------------------------------------ tabs
enum Tab { TAB_PAD, TAB_MAP, TAB_BOOK, TAB_LOG, TAB_MENU, TAB_COUNT };
static const char *TAB_NAMES[TAB_COUNT] = { "Commands", "Map", "Book", "Log", "Menu" };
static int tab = TAB_PAD;

static bool kb_open = false;
static int kb_mode = 0;            // 0 letters, 1 digits/punctuation
static std::string kb_typed;       // what was typed since the keyboard opened
static bool kb_auto = false;       // opened by O/Y: Enter closes it
static std::vector<std::string> chips;
static int chip_scroll = 0;

static void open_keyboard(int mode, bool autoclose = false) {
	kb_auto = autoclose;
	kb_open = true; kb_mode = mode; kb_typed.clear(); chip_scroll = 0;
	u3_words(chips);
	dirty = true;
}
static void close_keyboard() {
	if (!kb_typed.empty()) u3_word_typed(kb_typed);
	kb_open = false; dirty = true;
}

static void set_tab(int t) {
	t = (t + TAB_COUNT) % TAB_COUNT;
	if (t == tab) return;
	tab = t; dirty = true;
}

// ------------------------------------------------------------------ commands
struct Cmd { char key; const char *word; };
static const Cmd CMDS[26] = {
	{ 'A', "Attack" }, { 'B', "Board" }, { 'C', "Cast" }, { 'D', "Descend" }, { 'E', "Enter" },
	{ 'F', "Fire" }, { 'G', "Get" }, { 'H', "Hand" }, { 'I', "Ignite" }, { 'J', "Join" },
	{ 'K', "Klimb" }, { 'L', "Look" }, { 'M', "Modify" }, { 'N', "Negate" }, { 'O', "Other" },
	{ 'P', "Peer" }, { 'Q', "Quit" }, { 'R', "Ready" }, { 'S', "Steal" }, { 'T', "Transact" },
	{ 'U', "Unlock" }, { 'V', "Volume" }, { 'W', "Wear" }, { 'X', "X-it" }, { 'Y', "Yell" }, { 'Z', "Ztats" },
};

static bool cmd_relevant(char k) {
	if (!u3s.valid || u3s.nparty == 0) return false;
	switch (u3s.map) {
	case U3MAP_DUNGEON: return strchr("KDIGPC", k) != 0;
	case U3MAP_TOWN: case U3MAP_CASTLE: return strchr("TEXGLOU", k) != 0;
	case U3MAP_COMBAT: return strchr("ACRZ", k) != 0;
	case U3MAP_SOSARIA: return strchr("EBXFP", k) != 0;
	}
	return false;
}

static void draw_tabs() {
	cv.fill(0, 0, 320, 24, C_PANEL);
	int x = 0;
	static const int W[TAB_COUNT] = { 84, 52, 56, 52, 76 };
	for (int t = 0; t < TAB_COUNT; t++) {
		bool sel = t == tab && !kb_open;
		bool down = touching && touch_id == ID_TAB + t;
		if (sel) cv.fill(x, 0, W[t], 24, C_BG);
		else if (down) cv.fill(x, 0, W[t], 24, C_BTN_DOWN);
		cv.text_center(sel ? font_bold : font_ui, x + W[t] / 2, 5, TAB_NAMES[t], sel ? C_ACCENT : C_DIM);
		if (sel) cv.hline(x + 8, 22, W[t] - 16, C_ACCENT);
		reg(x, 0, W[t], 24, ID_TAB + t);
		x += W[t];
	}
}

static void draw_pad() {
	const int cols = 5, cw = 62, ch = 28, gx = 2, gy = 2, x0 = 3, y0 = 27;
	for (int i = 0; i < 30; i++) {
		int cx = x0 + (i % cols) * (cw + gx), cy = y0 + (i / cols) * (ch + gy);
		if (i < 26) {
			const Cmd &c = CMDS[i];
			int id = ID_CMD + i;
			bool down = touching && touch_id == id && hit(touch_x, touch_y) == id;
			bool rel = cmd_relevant(c.key);
			cv.fill_round(cx, cy, cw, ch, 5, down ? C_BTN_DOWN : (rel ? C_ACCENT_BG : C_BTN));
			char k[2] = { c.key, 0 };
			cv.text(font_big, cx + 5, cy + (ch - font_big.height) / 2, k, rel ? C_ACCENT : C_TEXT);
			cv.text(font_small, cx + 20, cy + (ch - font_small.height) / 2 + 1, c.word, rel ? 0xF4E2B8 : C_DIM);
			reg(cx, cy, cw, ch, id);
		} else {
			static const char *L[4] = { "Pass", "Esc", "Enter", "Abc" };
			static const int I[4] = { ID_PASS, ID_ESC, ID_ENTER, ID_KBD };
			button(cx, cy, cw, ch, I[i - 26], L[i - 26], font_bold, 0x2C3340, C_INFO);
		}
	}
	// party / numbers row
	int y = y0 + 6 * (ch + gy) + 2, w = 50;
	for (int i = 0; i < 4; i++) {
		char lbl[24];
		const U3Member &m = u3s.m[i];
		int id = ID_NUM + i + 1;
		bool down = touching && touch_id == id && hit(touch_x, touch_y) == id;
		cv.fill_round(3 + i * (w + 2), y, w, 31, 5, down ? C_BTN_DOWN : C_BTN);
		snprintf(lbl, sizeof lbl, "%d", i + 1);
		cv.text(font_big, 3 + i * (w + 2) + 5, y + 7, lbl, C_TEXT);
		if (u3s.valid && m.present) {
			char nm[8]; snprintf(nm, sizeof nm, "%.6s", m.name);
			cv.text(font_small, 3 + i * (w + 2) + 18, y + 4, nm, C_DIM);
			int bw = w - 22, pct = m.maxhp ? m.hp * bw / m.maxhp : 0;
			uint32_t col = m.status == 'P' ? 0xB46CFF : (m.status == 'D' || m.status == 'A') ? 0x6A6A70 : C_GOOD;
			cv.fill(3 + i * (w + 2) + 18, y + 20, bw, 4, 0x15171C);
			cv.fill(3 + i * (w + 2) + 18, y + 20, pct, 4, col);
		}
		reg(3 + i * (w + 2), y, w, 31, id);
	}
	button(3 + 4 * (w + 2), y, 52, 31, ID_DIGITS, "0-9", font_bold);
	button(3 + 4 * (w + 2) + 54, y, 60, 31, ID_SPELLS, "Spells", font_bold, 0x2E2840, 0xC8B4FF);
}

// ------------------------------------------------------------------ keyboard
static const char *KB_ROWS[2][3] = {
	{ "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.-" },
	{ "1234567890", "-/:;()&@\"!", ".,?'=+*#  " },
};

static void draw_keyboard() {
	cv.fill(0, 24, 320, 216, C_BG);
	// chips row
	cv.fill(0, 24, 320, 30, C_PANEL);
	int x = 4;
	if (chips.empty()) {
		cv.text(font_small, 8, 33, "Pinned clue words appear here", C_FAINT);
	} else {
		for (int i = chip_scroll; i < (int)chips.size() && x < 316; i++) {
			int w = u3_text_width(font_bold, chips[i].c_str()) + 14;
			if (x + w > 316) break;
			button(x, 27, w, 24, ID_CHIP + i, chips[i].c_str(), font_bold, C_ACCENT_BG, C_ACCENT);
			x += w + 4;
		}
	}
	// typed preview
	cv.text(font_small, 6, 58, kb_typed.empty() ? "Type, then Enter" : "Typed:", C_DIM);
	if (!kb_typed.empty()) cv.text(font_bold, 44, 57, kb_typed.c_str(), C_TEXT);
	// keys
	const int kw = 30, kh = 34, ky0 = 74;
	for (int r = 0; r < 3; r++) {
		const char *row = KB_ROWS[kb_mode][r];
		int n = (int)strlen(row);
		int rx = (320 - (n * (kw + 2) - 2)) / 2;
		for (int i = 0; i < n; i++) {
			if (row[i] == ' ') continue;
			char lbl[2] = { row[i], 0 };
			button(rx + i * (kw + 2), ky0 + r * (kh + 3), kw, kh, ID_KEY + (unsigned char)row[i], lbl, font_big);
		}
	}
	int y = ky0 + 3 * (kh + 3);
	button(4, y, 50, 36, ID_KB_MODE, kb_mode ? "ABC" : "123", font_bold, 0x2C3340, C_INFO);
	button(56, y, 44, 36, ID_KB_ESC, "Esc", font_bold, 0x2C3340, C_INFO);
	button(102, y, 104, 36, ID_KB_SPACE, "space", font_ui);
	button(208, y, 40, 36, ID_KB_BACK, "<-", font_bold);
	button(250, y, 66, 36, ID_KB_ENTER, "Enter", font_bold, C_ACCENT_BG, C_ACCENT);
	// close strip at the top-right of the tab bar
	button(258, 2, 60, 20, ID_KB_CLOSE, "Close", font_small, C_BTN, C_DIM);
}

static void kb_press(int id) {
	if (id >= ID_KEY && id < ID_KEY + 256) {
		char c = (char)(id - ID_KEY);
		key_tap_upper(c);
		if (isalnum((unsigned char)c)) kb_typed += (char)toupper((unsigned char)c);
	} else if (id >= ID_CHIP && id < ID_CHIP + 500) {
		int i = id - ID_CHIP;
		if (i < (int)chips.size()) {
			for (size_t k = 0; k < chips[i].size(); k++) key_tap_upper(chips[i][k]);
			kb_typed += chips[i];
		}
	} else switch (id) {
	case ID_KB_SPACE: u3_key_tap(' '); kb_typed += ' '; break;
	case ID_KB_BACK: u3_key_tap(8); if (!kb_typed.empty()) kb_typed.erase(kb_typed.size() - 1); break;
	case ID_KB_ENTER: u3_key_tap('\n'); if (kb_auto) close_keyboard(); else { if (!kb_typed.empty()) u3_word_typed(kb_typed); kb_typed.clear(); } break;
	case ID_KB_ESC: u3_key_tap(27); close_keyboard(); break;
	case ID_KB_CLOSE: close_keyboard(); break;
	case ID_KB_MODE: kb_mode ^= 1; break;
	}
	dirty = true;
}

// ------------------------------------------------------------------ map tab
static int map_area = -1;          // -1 = follow party
static int map_level = -1;         // -1 = follow party level
static int map_zoom = 1;           // 0 whole (3px), 1 near (8px)
static float map_cx = 32, map_cy = 32;
static bool map_follow = true;
static bool map_picker = false;
static uint32_t map_seen_changes = 0;

static int map_view_area() {
	if (map_area >= 0) return map_area;
	int a = u3_map_current_area();
	return a >= 0 ? a : 0;
}

static void draw_map() {
	int a = map_view_area();
	char title[64];
	if (map_picker) {
		cv.text(font_bold, 8, 28, "Explored places", C_TEXT);
		button(244, 26, 72, 22, ID_MAP_BACK, "Back", font_ui);
		int y = 52, col = 0;
		button(4, y, 154, 24, ID_MAP_PLACE + 0, "Sosaria", font_ui, map_view_area() == 0 ? C_ACCENT_BG : C_BTN);
		col = 1;
		for (int p = 0; p < U3_NUM_PLACES; p++) {
			if (!u3_area_known(p + 1)) continue;
			int bx = 4 + col * 158, by = y;
			button(bx, by, 154, 24, ID_MAP_PLACE + p + 1, u3_place_names[p], font_small,
			       map_view_area() == p + 1 ? C_ACCENT_BG : C_BTN);
			col++;
			if (col == 2) { col = 0; y += 26; }
			if (y > 212) break;
		}
		return;
	}
	bool dungeon = a > 0 && u3_place_kind(a - 1) == 2;
	if (a == 0) snprintf(title, sizeof title, "Sosaria");
	else snprintf(title, sizeof title, "%s", u3_place_names[a - 1]);
	if (dungeon) {
		int L = map_level >= 0 ? map_level : (u3_map_current_area() == a && u3s.map == U3MAP_DUNGEON ? u3s.dlevel : 0);
		u3_draw_dungeon(cv, a, L, 0, 26, 214, 214, true);
		cv.fill(214, 24, 106, 216, C_PANEL);
		cv.text(font_small, 220, 28, title, C_DIM);
		char lt[16]; snprintf(lt, sizeof lt, "Level %d", L + 1);
		cv.text(font_bold, 220, 42, lt, C_INFO);
		for (int i = 0; i < 8; i++) {
			char b[4]; snprintf(b, sizeof b, "%d", i + 1);
			bool known = u3_dungeon_level_known(a, i);
			bool here = u3_map_current_area() == a && u3s.map == U3MAP_DUNGEON && u3s.dlevel == i;
			uint32_t bg = i == L ? C_ACCENT_BG : C_BTN;
			uint32_t fg = i == L ? C_ACCENT : known ? C_TEXT : C_FAINT;
			button(220 + (i % 2) * 48, 62 + (i / 2) * 34, 44, 30, ID_MAP_LEVEL + i, b, font_big, bg, fg);
			if (here) cv.disc(220 + (i % 2) * 48 + 38, 62 + (i / 2) * 34 + 6, 3, 0xFFE070);
		}
		button(220, 204, 92, 30, ID_MAP_PLACES, "Places", font_ui);
		return;
	}
	// 64x64 area
	int ts = map_zoom == 0 ? 3 : 8;
	int mw = 236, mh = 214;
	if (map_follow && u3_map_current_area() == a) { map_cx = u3s.x + 0.5f; map_cy = u3s.y + 0.5f; }
	if (map_zoom == 0) { map_cx = 32; map_cy = 32; }
	u3_draw_area(cv, a, 0, 26, mw, mh, map_cx, map_cy, ts, true);
	cv.fill(mw, 24, 320 - mw, 216, C_PANEL);
	cv.text(font_small, mw + 6, 28, title, C_DIM);
	if (u3_map_current_area() == a && u3s.valid) {
		char pos[24]; snprintf(pos, sizeof pos, "%d, %d", u3s.x, u3s.y);
		cv.text(font_small, mw + 6, 42, pos, C_FAINT);
	}
	button(mw + 6, 62, 72, 32, ID_MAP_ZOOM, map_zoom == 0 ? "Zoom in" : "Whole", font_ui);
	button(mw + 6, 98, 72, 32, ID_MAP_CENTER, "Center", font_ui, map_follow ? C_ACCENT_BG : C_BTN, map_follow ? C_ACCENT : C_TEXT);
	button(mw + 6, 204, 72, 30, ID_MAP_PLACES, "Places", font_ui);
	if (map_zoom == 1) cv.text(font_small, mw + 6, 140, "Drag to pan", C_FAINT);
}

static void map_press(int id) {
	if (id >= ID_MAP_LEVEL && id < ID_MAP_LEVEL + 8) map_level = id - ID_MAP_LEVEL;
	else if (id == ID_MAP_ZOOM) { map_zoom ^= 1; map_follow = true; }
	else if (id == ID_MAP_CENTER) { map_follow = true; map_area = -1; map_level = -1; }
	else if (id == ID_MAP_PLACES) map_picker = true;
	else if (id == ID_MAP_BACK) map_picker = false;
	else if (id >= ID_MAP_PLACE && id < ID_MAP_PLACE + 20) {
		int a = id - ID_MAP_PLACE;
		map_area = (a == u3_map_current_area()) ? -1 : a;
		map_level = -1; map_picker = false; map_follow = map_area < 0;
		if (!map_follow) { map_cx = 32; map_cy = 32; }
	}
	dirty = true;
}

// ------------------------------------------------------------------ log tab
static bool log_pinned_only = false;
static int log_scroll = 0;            // lines scrolled up from the newest
static uint32_t log_seen = 0;
static std::vector<int> log_view;     // indices into u3_log

static void build_log_view() {
	log_view.clear();
	for (int i = 0; i < (int)u3_log.size(); i++)
		if (!log_pinned_only || u3_log[i].pinned) log_view.push_back(i);
}

static void draw_log() {
	build_log_view();
	cv.fill(0, 24, 320, 24, C_PANEL);
	button(4, 26, 92, 20, ID_LOG_FILTER, log_pinned_only ? "Show all" : "Pinned only", font_small);
	cv.text(font_small, 104, 29, "Tap a line to pin it as a clue", C_FAINT);
	const int lh = 17, top = 50, rows = (240 - top) / lh;
	int n = (int)log_view.size();
	if (log_scroll > n - rows) log_scroll = n - rows;
	if (log_scroll < 0) log_scroll = 0;
	int first = n - rows - log_scroll;
	if (first < 0) first = 0;
	if (n == 0) {
		cv.text_center(font_ui, 160, 120, log_pinned_only ? "No pinned clues yet" : "Messages will collect here", C_FAINT);
		return;
	}
	std::string last_where;
	for (int r = 0; r < rows && first + r < n; r++) {
		const U3Line &l = u3_log[log_view[first + r]];
		int y = top + r * lh;
		int id = ID_LOG_LINE + log_view[first + r];
		bool down = touching && touch_id == id && hit(touch_x, touch_y) == id;
		if (down) cv.fill(0, y, 320, lh, C_BTN_DOWN);
		else if (l.pinned) cv.fill(0, y, 320, lh, 0x221D12);
		if (l.pinned) cv.text(font_bold, 6, y + 1, "*", C_ACCENT);
		cv.text(l.pinned ? font_bold : font_ui, 18, y + 1, l.text.c_str(), l.pinned ? 0xF4E2B8 : C_TEXT);
		if (l.pinned || log_pinned_only) {
			int ww = u3_text_width(font_small, l.where.c_str());
			cv.text(font_small, 314 - ww, y + 3, l.where.c_str(), C_FAINT);
		}
		reg(0, y, 320, lh, id);
	}
	// scrollbar
	if (n > rows) {
		int sh = 190 * rows / n; if (sh < 12) sh = 12;
		int sy = top + (190 - sh) * (n - rows - log_scroll) / (n - rows);
		cv.fill(316, sy, 3, sh, C_FAINT);
	}
}

// ------------------------------------------------------------------ menu tab
static int confirm = 0;   // 1 = reset map
static void save_settings();

static void toggle_row(int y, int id, const char *label, const char *desc, bool on) {
	bool down = touching && touch_id == id && hit(touch_x, touch_y) == id;
	cv.fill_round(4, y, 312, 34, 6, down ? C_BTN_DOWN : C_BTN);
	cv.text(font_bold, 12, y + 4, label, C_TEXT);
	cv.text(font_small, 12, y + 19, desc, C_DIM);
	cv.fill_round(270, y + 8, 38, 18, 9, on ? 0x3E7A4A : 0x3A3F4C);
	cv.disc(on ? 299 : 279, y + 17, 7, on ? 0xD8F5DC : 0x9AA0AC);
	reg(4, y, 312, 34, id);
}

static void draw_menu() {
	if (confirm) {
		cv.text_center(font_bold, 160, 70, "Forget everything on the auto-map?", C_TEXT);
		cv.text_center(font_small, 160, 92, "Use this when you start a new game.", C_DIM);
		button(40, 130, 110, 40, ID_CONFIRM_YES, "Reset map", font_bold, 0x4A2222, 0xFFB0B0);
		button(170, 130, 110, 40, ID_CONFIRM_NO, "Keep it", font_bold);
		return;
	}
	toggle_row(28, ID_MENU + 0, "Info strips", "Moons, wind, party health, food, Marks and Cards", u3_settings.strips);
	toggle_row(66, ID_MENU + 1, "Fast-forward toggles", "Off: hold ZR. On: tap ZR to switch it on and off", u3_settings.turbo_toggle);
	toggle_row(104, ID_MENU + 2, "Swap A and B", "Off: A = Enter, B = Esc", u3_settings.swap_ab);
	toggle_row(142, ID_MENU + 3, "Show the cluebook", "Adds the original cluebook (spoilers!) to the Book tab", u3_settings.cluebook);
	button(4, 182, 154, 30, ID_MENU + 10, "Reset auto-map", font_ui);
	button(162, 182, 154, 30, ID_MENU + 11, "Save & exit", font_ui, 0x3A2626, 0xFFC0C0);
	cv.text(font_small, 8, 220, "Save the game with Q (Quit) while outdoors.", C_FAINT);
}

static void menu_press(int id) {
	switch (id) {
	case ID_MENU + 0: u3_settings.strips = !u3_settings.strips; u3_top_update(true); break;
	case ID_MENU + 1: u3_settings.turbo_toggle = !u3_settings.turbo_toggle; set_turbo(false); break;
	case ID_MENU + 2: u3_settings.swap_ab = !u3_settings.swap_ab; break;
	case ID_MENU + 3: u3_settings.cluebook = !u3_settings.cluebook; break;
	case ID_MENU + 10: confirm = 1; break;
	case ID_MENU + 11: u3_ui_save_all(); throw(0);
	case ID_CONFIRM_YES: u3_map_reset(); confirm = 0; break;
	case ID_CONFIRM_NO: confirm = 0; break;
	}
	save_settings();
	dirty = true;
}

// ------------------------------------------------------------------ settings
static void save_settings() {
	FILE *f = fopen(U3_DIR "/settings.cfg", "w");
	if (!f) return;
	fprintf(f, "strips=%d\nturbo_toggle=%d\nswap_ab=%d\ncluebook=%d\n", u3_settings.strips, u3_settings.turbo_toggle,
	        u3_settings.swap_ab, u3_settings.cluebook);
	fclose(f);
}
static void load_settings() {
	FILE *f = fopen(U3_DIR "/settings.cfg", "r");
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof line, f)) {
		int v;
		if (sscanf(line, "strips=%d", &v) == 1) u3_settings.strips = v;
		else if (sscanf(line, "turbo_toggle=%d", &v) == 1) u3_settings.turbo_toggle = v;
		else if (sscanf(line, "swap_ab=%d", &v) == 1) u3_settings.swap_ab = v;
		else if (sscanf(line, "cluebook=%d", &v) == 1) u3_settings.cluebook = v;
	}
	fclose(f);
}

void u3_ui_save_all() {
	u3_map_save(true);
	u3_pins_save(U3_DIR "/clues.txt");
	save_settings();
}

// ------------------------------------------------------------------ draw
static void draw_all() {
	btns.clear();
	cv.clear(C_BG);
	if (kb_open) { draw_tabs(); draw_keyboard(); }
	else {
		draw_tabs();
		switch (tab) {
		case TAB_PAD: draw_pad(); break;
		case TAB_MAP: draw_map(); break;
		case TAB_BOOK: u3_book_draw(cv, 24); break;
		case TAB_LOG: draw_log(); break;
		case TAB_MENU: draw_menu(); break;
		}
	}
	if (turbo_on) {
		cv.fill_round(250, 228, 66, 12, 4, 0x3A3320);
		cv.text_center(font_small, 283, 228, "fast-forward", 0xFFE070);
	}
	cv.present_bottom();
	last_draw = u3_ticks_ms();
	dirty = false;
}

// ------------------------------------------------------------------ actions
static void press(int id) {
	if (id >= ID_TAB && id < ID_TAB + TAB_COUNT) {
		if (kb_open) close_keyboard();
		set_tab(id - ID_TAB);
		return;
	}
	if (kb_open) { kb_press(id); return; }
	switch (tab) {
	case TAB_PAD:
		if (id >= ID_CMD && id < ID_CMD + 26) {
			char k = CMDS[id - ID_CMD].key;
			u3_key_tap(k);
			if (k == 'O' || k == 'Y') open_keyboard(0, true);
		} else if (id >= ID_NUM + 1 && id <= ID_NUM + 4) u3_key_tap((char)('0' + id - ID_NUM));
		else if (id == ID_PASS) u3_key_tap(' ');
		else if (id == ID_ESC) u3_key_tap(27);
		else if (id == ID_ENTER) u3_key_tap('\n');
		else if (id == ID_KBD) open_keyboard(0);
		else if (id == ID_DIGITS) open_keyboard(1);
		else if (id == ID_SPELLS) { u3_book_open_spells(0); set_tab(TAB_BOOK); }
		dirty = true;
		break;
	case TAB_MAP: map_press(id); break;
	case TAB_LOG:
		if (id == ID_LOG_FILTER) { log_pinned_only = !log_pinned_only; log_scroll = 0; }
		else if (id >= ID_LOG_LINE && id < ID_LOG_LINE + (int)u3_log.size() + 1) {
			int i = id - ID_LOG_LINE;
			if (i < (int)u3_log.size()) { u3_log[i].pinned = !u3_log[i].pinned; u3_pins_save(U3_DIR "/clues.txt"); }
		}
		dirty = true;
		break;
	case TAB_MENU: menu_press(id); break;
	}
}

// ------------------------------------------------------------------ init
static void ui_init() {
	cv.init(bottom_px, 320, 240, U3_RGB565);
	load_settings();
	u3_text_init(U3_GAME_DIR "/CHARSET.VGA");
	u3_map_init(U3_GAME_DIR "/SHAPES.VGA", U3_GAME_DIR "/U3VGA.PAL", U3_DIR "/automap.dat");
	u3_pins_load(U3_DIR "/clues.txt");
	inited = true;
	dirty = true;
	u3_top_update(true);
}

// ------------------------------------------------------------------ frame hook
static const uint16_t *frame_pix = 0;
static int frame_pitch = 0, frame_w = 0, frame_h = 0;
static uint32_t last_scan = 0;

void u3_ui_on_frame(const uint16_t *pix, int pitch, int w, int h) {
	frame_pix = pix; frame_pitch = pitch; frame_w = w; frame_h = h;
	uint32_t now = u3_ticks_ms();
	if (now - last_scan >= 90) {
		last_scan = now;
		u3_text_scan(pix, pitch, w, h);
	}
}

// ------------------------------------------------------------------ input
static uint32_t arrows_held = 0;   // bitmask of KBD arrows currently down
static uint32_t last_state = 0, last_state_changes = 0, last_log_changes = 0, last_map_changes = 0;
static uint32_t last_slow = 0;

static void set_arrow(int bit, KBD_KEYS k, bool on) {
	bool was = arrows_held & (1u << bit);
	if (on == was) return;
	if (on) arrows_held |= 1u << bit; else arrows_held &= ~(1u << bit);
	KEYBOARD_AddKey(k, on);
}

void u3_ui_input() {
	uint32_t now = u3_ticks_ms();
	if (!inited) ui_init();

	key_pump(now);

	// SDL's event pump also calls hidScanInput(), so hidKeysDown() is unreliable here:
	// derive presses/releases from the held state ourselves.
	static u32 prev_held = 0;
	u32 kh = hidKeysHeld();
	u32 kd = kh & ~prev_held, ku = ~kh & prev_held;
	prev_held = kh;

	// --- movement: D-pad (+ circle pad unless reading a book)
	circlePosition cp; hidCircleRead(&cp);
	bool cp_move = tab != TAB_BOOK || kb_open;
	int cx = cp_move ? cp.dx : 0, cy = cp_move ? cp.dy : 0;
	set_arrow(0, KBD_up, (kh & KEY_DUP) || cy > 70);
	set_arrow(1, KBD_down, (kh & KEY_DDOWN) || cy < -70);
	set_arrow(2, KBD_left, (kh & KEY_DLEFT) || cx < -70);
	set_arrow(3, KBD_right, (kh & KEY_DRIGHT) || cx > 70);

	// --- fast-forward
	if (u3_settings.turbo_toggle) { if (kd & KEY_ZR) set_turbo(!turbo_on); }
	else set_turbo((kh & KEY_ZR) != 0);

	// --- buttons
	if (kd) {
		u32 kA = u3_settings.swap_ab ? KEY_B : KEY_A, kB = u3_settings.swap_ab ? KEY_A : KEY_B;
		bool consumed = (tab == TAB_BOOK && !kb_open) ? u3_book_button(kd) : false;
		if (!consumed) {
			if (kd & kA) u3_key_tap('\n');
			if (kd & kB) u3_key_tap(27);
			if (kd & KEY_X) u3_key_tap(' ');
			if (kd & KEY_Y) { if (kb_open) close_keyboard(); else open_keyboard(0); }
			if (kd & KEY_ZL) u3_key_tap('z');
			if (kd & KEY_L) { if (kb_open) close_keyboard(); set_tab(tab - 1); }
			if (kd & KEY_R) { if (kb_open) close_keyboard(); set_tab(tab + 1); }
			if (kd & KEY_START) { if (kb_open) close_keyboard(); set_tab(tab == TAB_MENU ? TAB_PAD : TAB_MENU); }
			if (kd & KEY_SELECT) { if (kb_open) close_keyboard(); set_tab(tab == TAB_MAP ? TAB_PAD : TAB_MAP); }
		}
		dirty = true;
	}
	(void)ku;

	// --- touch
	if ((kh & KEY_TOUCH) && !touching) {
		touchPosition t; hidTouchRead(&t);
		if (t.px == 0 && t.py == 0) goto touch_done;   // first sample can be empty
		touching = true; touch_x0 = touch_x = t.px; touch_y0 = touch_y = t.py; touch_t0 = now;
		touch_id = hit(t.px, t.py);
		if (tab == TAB_BOOK && !kb_open && t.py >= 24) u3_book_touch(t.px, t.py, 0);
		dirty = true;
	} else if ((kh & KEY_TOUCH) && touching) {
		touchPosition t; hidTouchRead(&t);
		int dx = t.px - touch_x, dy = t.py - touch_y;
		if (dx || dy) {
			if (tab == TAB_BOOK && !kb_open && touch_y0 >= 24) { u3_book_touch(t.px, t.py, 1); }
			else if (tab == TAB_MAP && !kb_open && !map_picker && map_zoom == 1 && touch_x0 < 236 && touch_y0 > 26) {
				map_follow = false; map_cx -= dx / 8.0f; map_cy -= dy / 8.0f;
				if (map_cx < 0) map_cx += 64; if (map_cx >= 64) map_cx -= 64;
				if (map_cy < 0) map_cy += 64; if (map_cy >= 64) map_cy -= 64;
				touch_id = ID_NONE;
			} else if (tab == TAB_LOG && !kb_open && touch_y0 > 48) {
				static int acc = 0;
				acc += dy;
				while (acc >= 17) { log_scroll++; acc -= 17; }
				while (acc <= -17) { log_scroll--; acc += 17; }
				if (abs(t.py - touch_y0) > 6) touch_id = ID_NONE;
			}
			touch_x = t.px; touch_y = t.py;
			dirty = true;
		}
	} else if (touching && !(kh & KEY_TOUCH)) {
		touching = false;
		int id = hit(touch_x, touch_y);
		if (tab == TAB_BOOK && !kb_open && touch_y0 >= 24) u3_book_touch(touch_x, touch_y, 2);
		else if (id != ID_NONE && id == touch_id) press(id);
		touch_id = -1;
		dirty = true;
	}
touch_done:

	// --- slow work (~12 Hz)
	if (now - last_slow >= 80) {
		last_slow = now;
		u3_state_update();
		u3_map_update();
		u3_top_update(false);
		if (tab == TAB_BOOK) u3_book_tick();
		if (u3s.changes != last_state_changes && (tab == TAB_PAD || tab == TAB_MAP)) { last_state_changes = u3s.changes; dirty = true; }
		if (u3_log_changes != last_log_changes && tab == TAB_LOG) { last_log_changes = u3_log_changes; dirty = true; }
		if (u3_map_changes != last_map_changes && tab == TAB_MAP) { last_map_changes = u3_map_changes; dirty = true; }
		if (tab == TAB_BOOK && u3_book_dirty()) dirty = true;
	}
	(void)last_state; (void)map_seen_changes; (void)log_seen;

	// --- redraw (cap ~30 fps)
	if (dirty && now - last_draw >= 33) draw_all();
}
