// u3_ui.cpp - Ultima III 3DS front end: the touch panel, buttons, screen layout and input
//
// Shared 3DS conventions (same in every port): A confirm, B back, START opens the menu,
// ZL swaps the screens (SELECT too), L/R tabs, D-Pad moves a highlight in menus, quiet
// click and press animation on every button, unusable buttons greyed and inert, a
// "Working..." pill when a press hasn't been answered, presses are never queued.
#include "u3_ui.h"
#include "u3_gfx.h"
#include "u3_state.h"
#include "u3_text.h"
#include "u3_map.h"
#include "u3_io.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>

#include "keyboard.h"

extern bool ticksLocked;           // DOSBox fast-forward
extern bool keyb_typematic;        // DOSBox hardware key repeat (we repeat held keys ourselves)
bool KEYBOARD_Busy(void);          // a key is still on its way to the game, or waiting in the BIOS buffer
void KEYBOARD_DropTyped(void);     // throw away keys the game hasn't taken
extern "C" { extern int u3v_swap; void u3v_present(void); }

unsigned int u3_files_opened = 0;  // DOS file opens (loading progress)

U3Settings u3_settings = { true, false, false, false, false };

uint32_t u3_ticks_ms() { return (uint32_t)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000)); }

// ------------------------------------------------------------------ palette
enum : uint32_t {
	C_BG = 0x111318, C_PANEL = 0x1B1E25, C_BTN = 0x262A33, C_BTN_HI = 0x343944, C_BTN_DOWN = 0x4A5263,
	C_EDGE = 0x353A46, C_TEXT = 0xECE8DE, C_DIM = 0x8C919D, C_FAINT = 0x5A6070, C_ACCENT = 0xE8B04A,
	C_ACCENT_BG = 0x3A2F1A, C_GOOD = 0x5CCB6A, C_INFO = 0x7FD4FF, C_OFF_BG = 0x1A1C22, C_OFF_FG = 0x4A4F5C
};

// ------------------------------------------------------------------ canvas
static uint16_t bottom_px[320 * 240];
static U3Canvas cv;
static bool inited = false;
static bool dirty = true;
static uint32_t last_draw = 0;
static bool loading = true;        // until the game draws its first picture

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
	ID_DIR = 710,   // 4 direction buttons (prompt row)
	ID_YES = 715, ID_NO,
	ID_KB_SPACE = 720, ID_KB_BACK, ID_KB_ENTER, ID_KB_CLOSE, ID_KB_MODE, ID_KB_ESC,
	ID_MAP_LEVEL = 740, ID_MAP_ZOOM = 760, ID_MAP_CENTER, ID_MAP_PLACES, ID_MAP_PLACE = 780, ID_MAP_BACK = 810,
	ID_LOG_FILTER = 820, ID_LOG_LINE = 1000,
	ID_SET = 900, ID_CONFIRM_YES = 950, ID_CONFIRM_NO,
	ID_SM_RESUME = 960, ID_SM_MANUAL, ID_SM_SAVE, ID_SM_SETTINGS, ID_SM_EXIT, ID_SM_EXIT_YES, ID_SM_EXIT_NO,
};

static void reg(int x, int y, int w, int h, int id) { Btn b = { x, y, w, h, id }; btns.push_back(b); }
static int hit(int x, int y) {
	for (int i = (int)btns.size() - 1; i >= 0; i--) {
		const Btn &b = btns[i];
		if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return b.id;
	}
	return ID_NONE;
}
static const Btn *find_btn(int id) {
	for (size_t i = 0; i < btns.size(); i++) if (btns[i].id == id) return &btns[i];
	return 0;
}

// ---- press feedback (shared with the book reader)
static int fx_id = -1;
static uint32_t fx_t = 0;
enum { FX_MS = 170 };
void u3_press_down(int id) { if (id > 0) u3_click(); }
void u3_press_up(int id) { fx_id = id; fx_t = u3_ticks_ms(); }
bool u3_press_animating() { return fx_id >= 0 && u3_ticks_ms() - fx_t < FX_MS; }
int u3_press_look(int id, bool held) {
	if (held) return 1;
	if (id == fx_id) {
		uint32_t e = u3_ticks_ms() - fx_t;
		if (e < FX_MS) return 2 + (int)(254 * (FX_MS - e) / FX_MS);
	}
	return 0;
}
int u3_btn_bg(U3Canvas &c, int x, int y, int w, int h, int r, uint32_t bg, int look) {
	if (look == 1) {   // pushed in: darker, one pixel smaller, content drops a pixel
		c.fill_round(x, y, w, h, r, 0x0C0D10);
		c.fill_round(x + 1, y + 1, w - 2, h - 2, r, u3_mix(bg, 0x7A8496, 90));
		return 1;
	}
	c.fill_round(x, y, w, h, r, bg);
	if (look >= 2) {   // release flash: a bright rim that fades out
		int a = look - 2;
		c.fill_round(x, y, w, h, r, 0xFFFFFF, a / 6);
		c.frame_round(x, y, w, h, r, 0xFFE9B0, a);
	}
	return 0;
}

// Shorten text with "..." so it never runs past its space.
static std::string fit_str(const u3_font &f, const std::string &str, int maxw) {
	if (u3_text_width(f, str.c_str()) <= maxw) return str;
	int dots = u3_text_width(f, "...");
	std::string t = str;
	while (!t.empty() && u3_text_width(f, t.c_str()) + dots > maxw) t.erase(t.size() - 1);
	while (!t.empty() && t[t.size() - 1] == ' ') t.erase(t.size() - 1);
	return t + "...";
}

static inline bool held_on(int id) { return touching && touch_id == id && hit(touch_x, touch_y) == id; }

static void button(int x, int y, int w, int h, int id, const char *label, const u3_font &f = font_ui,
                   uint32_t bg = C_BTN, uint32_t fg = C_TEXT) {
	int off = u3_btn_bg(cv, x, y, w, h, 5, bg, u3_press_look(id, held_on(id)));
	std::string t = fit_str(f, label, w - 6);
	int tw = u3_text_width(f, t.c_str());
	cv.text(f, x + (w - tw) / 2, y + (h - f.height) / 2 + off, t.c_str(), fg);
	reg(x, y, w, h, id);
}
// A button that can't be used here: greyed, no click, no animation, not registered.
static void button_off(int x, int y, int w, int h, const char *label, const u3_font &f = font_ui) {
	cv.fill_round(x, y, w, h, 5, C_OFF_BG);
	std::string t = fit_str(f, label, w - 6);
	int tw = u3_text_width(f, t.c_str());
	cv.text(f, x + (w - tw) / 2, y + (h - f.height) / 2, t.c_str(), C_OFF_FG);
}

// ------------------------------------------------------------------ key injection
// Never store or queue presses: a press is only taken when the game has taken the previous
// one (DOSBox's keyboard and the BIOS buffer are empty). Otherwise it is dropped. A word
// typed from the keyboard's word buttons is one press, typed a letter at a time.
struct KeyEv { KBD_KEYS k; bool shift; };
static std::vector<KeyEv> act;     // the one action being typed into the game
static size_t act_pos = 0;
static int key_phase = 0;          // 0 ready, 1 key down, 2 gap
static uint32_t key_time = 0, act_wait_t = 0;
static KBD_KEYS key_cur = KBD_NONE;
static bool key_shift = false;
static uint32_t press_t = 0;       // a deliberate press the game hasn't answered yet (0 = none)

static bool keys_idle() { return act_pos >= act.size() && key_phase == 0; }
static bool can_send() { return keys_idle() && !KEYBOARD_Busy(); }
static void note_press() { press_t = u3_ticks_ms(); if (!press_t) press_t = 1; }

static bool send_action(const std::vector<KeyEv> &keys) {
	if (keys.empty() || !can_send()) return false;
	act = keys; act_pos = 0; act_wait_t = u3_ticks_ms();
	note_press();
	return true;
}

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
static bool add_char(std::vector<KeyEv> &v, char c, bool caps) {
	KBD_KEYS k = ascii_key(c);
	if (k == KBD_NONE) return false;
	KeyEv e = { k, caps && c >= 'A' && c <= 'Z' };
	v.push_back(e);
	return true;
}
bool u3_key_tap(char c) { std::vector<KeyEv> v; add_char(v, c, false); return send_action(v); }
static bool key_tap_upper(char c) { std::vector<KeyEv> v; add_char(v, c, true); return send_action(v); }
static bool key_word(const std::string &w) { std::vector<KeyEv> v; for (size_t i = 0; i < w.size(); i++) add_char(v, w[i], true); return send_action(v); }
static bool key_special(KBD_KEYS k) { std::vector<KeyEv> v; KeyEv e = { k, false }; v.push_back(e); return send_action(v); }

// Performance notes go to debug.log through the background writer (never straight to the card).
static void dbg(const char *fmt, int a, int b) {
	static int lines = 0;
	if (lines > 2000) return;
	char line[160];
	int n = snprintf(line, sizeof line, "%u ", (unsigned)u3_ticks_ms());
	n += snprintf(line + n, sizeof line - n, fmt, a, b);
	snprintf(line + n, sizeof line - n, "\n");
	u3w_append(U3_DIR "/debug.log", line);
	lines++;
}

static void key_pump(uint32_t now) {
	switch (key_phase) {
	case 0:
		if (act_pos >= act.size()) return;
		if (KEYBOARD_Busy()) {   // the game hasn't taken the previous letter of this word yet
			if (now - act_wait_t > 1500) { act.clear(); act_pos = 0; KEYBOARD_DropTyped(); }
			return;
		}
		key_cur = act[act_pos].k; key_shift = act[act_pos].shift; act_pos++;
		if (key_shift) KEYBOARD_AddKey(KBD_leftshift, true);
		KEYBOARD_AddKey(key_cur, true);
		key_phase = 1; key_time = now;
		break;
	case 1:
		if (now - key_time >= 90) { KEYBOARD_AddKey(key_cur, false); if (key_shift) KEYBOARD_AddKey(KBD_leftshift, false); key_phase = 2; key_time = now; }
		break;
	case 2:
		if (now - key_time >= 40) { key_phase = 0; act_wait_t = now; }
		break;
	}
}

// Held directions repeat only when the game has taken the last step, so letting go stops at once.
struct Arrow { bool held, down_sent; uint32_t t0, tlast; };
static Arrow arrows[4];
static const KBD_KEYS ARROW_KEYS[4] = { KBD_up, KBD_down, KBD_left, KBD_right };
static void arrow(int i, bool on, uint32_t now) {
	Arrow &a = arrows[i];
	KBD_KEYS k = ARROW_KEYS[i];
	if (on && !a.held) {
		a.held = true; a.t0 = now; a.down_sent = false;
		if (can_send()) { KEYBOARD_AddKey(k, true); a.down_sent = true; a.tlast = now; note_press(); }
	} else if (on) {
		if (now - a.t0 >= 300 && now - a.tlast >= 100 && can_send()) { KEYBOARD_AddKey(k, true); a.down_sent = true; a.tlast = now; }
	} else if (a.held) {
		a.held = false;
		if (a.down_sent) KEYBOARD_AddKey(k, false);
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
enum Tab { TAB_PAD, TAB_MAP, TAB_BOOK, TAB_LOG, TAB_SET, TAB_COUNT };
static const char *TAB_NAMES[TAB_COUNT] = { "Commands", "Map", "Book", "Log", "Settings" };
static int tab = TAB_PAD;

static bool kb_open = false;
static int kb_mode = 0;            // 0 letters, 1 digits/punctuation
static std::string kb_typed;       // what was typed since the keyboard opened
static bool kb_auto = false;       // opened by O/Y: Enter closes it
static std::vector<std::string> chips;
static int chip_scroll = 0;

static int focus_id = -1;          // D-Pad highlight (START menu, and the panel when the screens are swapped)

static void open_keyboard(int mode, bool autoclose = false) {
	kb_auto = autoclose;
	kb_open = true; kb_mode = mode; kb_typed.clear(); chip_scroll = 0;
	u3_words(chips);
	focus_id = ID_KEY + 'A';
	dirty = true;
}
static void close_keyboard() {
	if (!kb_typed.empty()) u3_word_typed(kb_typed);
	kb_open = false; dirty = true;
}

static int book_return_tab = 0;    // where B/START go back to when the manual closes
static void set_tab(int t) {
	t = (t + TAB_COUNT) % TAB_COUNT;
	if (t == tab) return;
	if (t == TAB_BOOK) book_return_tab = tab;
	tab = t; dirty = true;
	focus_id = -1;
}

// ------------------------------------------------------------------ START menu
static bool smenu = false;
static int smenu_confirm = 0;      // 1 = "exit the game?"
static bool can_save() { return u3s.valid && u3s.nparty > 0 && u3s.map == U3MAP_SOSARIA; }
static void open_smenu() {
	if (kb_open) close_keyboard();
	smenu = true; smenu_confirm = 0; focus_id = ID_SM_RESUME; dirty = true;
}
static void close_smenu() { smenu = false; smenu_confirm = 0; focus_id = -1; dirty = true; }

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

// What the game is asking for right now, read off its message window: the keys it
// wants become buttons (prompt row) so nothing has to be found on the keyboard.
enum { PROMPT_NONE, PROMPT_DIR, PROMPT_YN };
static int prompt_kind() {
	if (!u3_in_game()) return PROMPT_NONE;
	const char *last = "";
	for (int i = 6; i >= 0; i--) {
		const char *l = u3_text_window_line(i);
		bool blank = true;
		for (const char *p = l; *p; p++) if (*p != ' ') { blank = false; break; }
		if (!blank) { last = l; break; }
	}
	std::string s(last);
	while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
	if (s.empty()) return PROMPT_NONE;
	std::string up = s;
	for (size_t i = 0; i < up.size(); i++) up[i] = (char)toupper((unsigned char)up[i]);
	if (up.find("Y/N") != std::string::npos || up.find("(Y") != std::string::npos) return PROMPT_YN;
	if (up[up.size() - 1] == '-' || up.find("DIR") != std::string::npos) {
		static const char *dirs[] = { "ATTACK", "FIRE", "LOOK", "TRANSACT", "STEAL", "UNLOCK", "DIR", "GET", "OPEN", 0 };
		for (int i = 0; dirs[i]; i++) if (up.find(dirs[i]) != std::string::npos) return PROMPT_DIR;
	}
	return PROMPT_NONE;
}
static int cur_prompt = PROMPT_NONE;

static void draw_tabs() {
	// Rule 11: tabs 24 px tall, 4 px apart, 6 px in from the screen edges.
	cv.fill(0, 0, 320, 32, C_PANEL);
	int x = 6;
	static const int W[TAB_COUNT] = { 80, 46, 50, 44, 72 };
	for (int t = 0; t < TAB_COUNT; t++) {
		bool sel = t == tab && !kb_open;
		int look = u3_press_look(ID_TAB + t, held_on(ID_TAB + t));
		int off = 0;
		if (sel && look == 0) cv.fill_round(x, 6, W[t], 24, 4, C_BG);
		else if (look) off = u3_btn_bg(cv, x, 6, W[t], 24, 4, sel ? C_BG : C_PANEL, look);
		else cv.fill_round(x, 6, W[t], 24, 4, 0x22262E);
		const u3_font &f = sel ? font_bold : font_ui;
		std::string nm = fit_str(f, TAB_NAMES[t], W[t] - 6);
		cv.text_center(f, x + W[t] / 2, 10 + off, nm.c_str(), sel ? C_ACCENT : C_DIM);
		if (sel) cv.hline(x + 8, 27, W[t] - 16, C_ACCENT);
		reg(x, 6, W[t], 24, ID_TAB + t);
		x += W[t] + 4;
	}
}

static void arrow_glyph(int cx, int cy, int dir, uint32_t col) {
	// dir 0 up, 1 down, 2 left, 3 right
	switch (dir) {
	case 0: cv.tri(cx - 7, cy + 4, cx + 7, cy + 4, cx, cy - 6, col); break;
	case 1: cv.tri(cx - 7, cy - 4, cx + 7, cy - 4, cx, cy + 6, col); break;
	case 2: cv.tri(cx + 4, cy - 7, cx + 4, cy + 7, cx - 6, cy, col); break;
	case 3: cv.tri(cx - 4, cy - 7, cx - 4, cy + 7, cx + 6, cy, col); break;
	}
}

static void draw_pad() {
	const int cols = 5, cw = 58, ch = 24, gx = 4, gy = 4, x0 = 6, y0 = 34;   // rule 11
	for (int i = 0; i < 30; i++) {
		int cx = x0 + (i % cols) * (cw + gx), cy = y0 + (i / cols) * (ch + gy);
		if (i < 26) {
			const Cmd &c = CMDS[i];
			int id = ID_CMD + i;
			bool rel = cmd_relevant(c.key);
			int off = u3_btn_bg(cv, cx, cy, cw, ch, 5, rel ? C_ACCENT_BG : C_BTN, u3_press_look(id, held_on(id)));
			char k[2] = { c.key, 0 };
			cv.text(font_bold, cx + 3, cy + (ch - font_bold.height) / 2 + off, k, rel ? C_ACCENT : C_TEXT);
			int wx = cx + 3 + u3_text_width(font_bold, k) + 2;
			if (wx < cx + 14) wx = cx + 14;
			cv.text(font_small, wx, cy + (ch - font_small.height) / 2 + 1 + off,
			        fit_str(font_small, c.word, cx + cw - 2 - wx).c_str(), rel ? 0xF4E2B8 : C_DIM);
			reg(cx, cy, cw, ch, id);
		} else {
			static const char *L[4] = { "Pass", "Esc", "Enter", "Abc" };
			static const int I[4] = { ID_PASS, ID_ESC, ID_ENTER, ID_KBD };
			button(cx, cy, cw, ch, I[i - 26], L[i - 26], font_bold, 0x2C3340, C_INFO);
		}
	}
	int y = y0 + 6 * (ch + gy);   // 202; bottom row is 30 tall, ends at 232
	const int rh = 30;
	// The game is asking for a direction or yes/no: those keys take over the bottom row.
	if (cur_prompt == PROMPT_DIR) {
		static const int order[4] = { 2, 0, 1, 3 };   // left, up, down, right
		static const char *nm[4] = { "North", "South", "West", "East" };
		for (int k = 0; k < 4; k++) {
			int d = order[k], id = ID_DIR + d, bx = 6 + k * 66;
			int off = u3_btn_bg(cv, bx, y, 62, rh, 5, C_ACCENT_BG, u3_press_look(id, held_on(id)));
			arrow_glyph(bx + 13, y + rh / 2 + off, d, C_ACCENT);
			cv.text(font_small, bx + 24, y + (rh - font_small.height) / 2 + off, nm[d], 0xF4E2B8);
			reg(bx, y, 62, rh, id);
		}
		button(6 + 4 * 66, y, 44, rh, ID_ESC, "Esc", font_bold, 0x2C3340, C_INFO);
		return;
	}
	if (cur_prompt == PROMPT_YN) {
		button(6, y, 122, rh, ID_YES, "Yes", font_bold, C_ACCENT_BG, C_ACCENT);
		button(132, y, 122, rh, ID_NO, "No", font_bold, C_ACCENT_BG, C_ACCENT);
		button(258, y, 56, rh, ID_ESC, "Esc", font_bold, 0x2C3340, C_INFO);
		return;
	}
	// party / numbers row. In the game, slots with nobody in them are greyed and inert;
	// outside it (character creation, the roster) 1-4 are plain number keys.
	bool in_party = u3s.valid && u3s.nparty > 0;
	int w = 48;
	for (int i = 0; i < 4; i++) {
		char lbl[24];
		const U3Member &m = u3s.m[i];
		int id = ID_NUM + i + 1;
		int bx = 6 + i * (w + 4);
		snprintf(lbl, sizeof lbl, "%d", i + 1);
		if (in_party && !m.present) {
			cv.fill_round(bx, y, w, rh, 5, C_OFF_BG);
			cv.text(font_big, bx + 5, y + (rh - font_big.height) / 2, lbl, C_OFF_FG);
			continue;
		}
		int off = u3_btn_bg(cv, bx, y, w, rh, 5, C_BTN, u3_press_look(id, held_on(id)));
		cv.text(font_big, bx + 5, y + (rh - font_big.height) / 2 + off, lbl, C_TEXT);
		if (in_party && m.present) {
			std::string nm = fit_str(font_small, m.name, w - 20);
			cv.text(font_small, bx + 18, y + 3 + off, nm.c_str(), C_DIM);
			int bw = w - 22, pct = m.maxhp ? m.hp * bw / m.maxhp : 0;
			if (pct > bw) pct = bw;
			uint32_t col = m.status == 'P' ? 0xB46CFF : (m.status == 'D' || m.status == 'A') ? 0x6A6A70 : C_GOOD;
			cv.fill(bx + 18, y + 19 + off, bw, 4, 0x15171C);
			cv.fill(bx + 18, y + 19 + off, pct, 4, col);
		}
		reg(bx, y, w, rh, id);
	}
	button(6 + 4 * (w + 4), y, 36, rh, ID_DIGITS, "0-9", font_bold);
	button(6 + 4 * (w + 4) + 40, y, 60, rh, ID_SPELLS, "Spells", font_bold, 0x2E2840, 0xC8B4FF);
}

// ------------------------------------------------------------------ keyboard
static const char *KB_ROWS[2][3] = {
	{ "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.-" },
	{ "1234567890", "-/:;()&@\"!", ".,?'=+*#  " },
};

static void draw_keyboard() {
	cv.fill(0, 32, 320, 208, C_BG);
	// chips row
	cv.fill(0, 32, 320, 30, C_PANEL);
	int x = 6;
	if (chips.empty()) {
		cv.text(font_small, 8, 40, "Pinned clue words appear here", C_FAINT);
	} else {
		for (int i = chip_scroll; i < (int)chips.size() && x < 314; i++) {
			int w = u3_text_width(font_bold, chips[i].c_str()) + 14;
			if (x + w > 314) break;
			button(x, 35, w, 24, ID_CHIP + i, chips[i].c_str(), font_bold, C_ACCENT_BG, C_ACCENT);
			x += w + 4;
		}
	}
	// typed preview
	cv.text(font_small, 6, 67, kb_typed.empty() ? "Type, then Enter" : "Typed:", C_DIM);
	if (!kb_typed.empty()) cv.text(font_bold, 44, 66, fit_str(font_bold, kb_typed, 270).c_str(), C_TEXT);
	// keys (rule 11: 27 x 32, 4 px apart, 6 px from the edges)
	const int kw = 27, kh = 32, ky0 = 86, kg = 4;
	for (int r = 0; r < 3; r++) {
		const char *row = KB_ROWS[kb_mode][r];
		int n = (int)strlen(row);
		int rx = (320 - (n * (kw + kg) - kg)) / 2;
		for (int i = 0; i < n; i++) {
			if (row[i] == ' ') continue;
			char lbl[2] = { row[i], 0 };
			button(rx + i * (kw + kg), ky0 + r * (kh + kg), kw, kh, ID_KEY + (unsigned char)row[i], lbl, font_big);
		}
	}
	int y = ky0 + 3 * (kh + kg);   // 194; row ends at 228
	button(6, y, 48, 34, ID_KB_MODE, kb_mode ? "ABC" : "123", font_bold, 0x2C3340, C_INFO);
	button(58, y, 44, 34, ID_KB_ESC, "Esc", font_bold, 0x2C3340, C_INFO);
	button(106, y, 104, 34, ID_KB_SPACE, "space", font_ui);
	button(214, y, 40, 34, ID_KB_BACK, "<-", font_bold);
	button(258, y, 56, 34, ID_KB_ENTER, "Enter", font_bold, C_ACCENT_BG, C_ACCENT);
	// close button over the last tab
	button(244, 6, 70, 24, ID_KB_CLOSE, "Close", font_ui, C_BTN, C_DIM);
}

static void kb_press(int id) {
	if (id >= ID_KEY && id < ID_KEY + 256) {
		char c = (char)(id - ID_KEY);
		if (key_tap_upper(c) && isalnum((unsigned char)c)) kb_typed += (char)toupper((unsigned char)c);
	} else if (id >= ID_CHIP && id < ID_CHIP + 500) {
		int i = id - ID_CHIP;
		if (i < (int)chips.size() && key_word(chips[i])) kb_typed += chips[i];
	} else switch (id) {
	case ID_KB_SPACE: if (u3_key_tap(' ')) kb_typed += ' '; break;
	case ID_KB_BACK: if (u3_key_tap(8) && !kb_typed.empty()) kb_typed.erase(kb_typed.size() - 1); break;
	case ID_KB_ENTER:
		if (u3_key_tap('\n')) {
			if (kb_auto) close_keyboard();
			else { if (!kb_typed.empty()) u3_word_typed(kb_typed); kb_typed.clear(); }
		}
		break;
	case ID_KB_ESC: u3_key_tap(27); close_keyboard(); break;
	case ID_KB_CLOSE: close_keyboard(); break;
	case ID_KB_MODE: kb_mode ^= 1; focus_id = ID_KB_MODE; break;
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

static int map_view_area() {
	if (map_area >= 0) return map_area;
	int a = u3_map_current_area();
	return a >= 0 ? a : 0;
}

static void draw_map() {
	int a = map_view_area();
	char title[64];
	if (map_picker) {
		cv.text(font_bold, 8, 39, "Explored places", C_TEXT);
		button(242, 34, 72, 24, ID_MAP_BACK, "Back", font_ui);
		int y = 62, col = 0;
		button(6, y, 152, 24, ID_MAP_PLACE + 0, "Sosaria", font_ui, map_view_area() == 0 ? C_ACCENT_BG : C_BTN);
		col = 1;
		for (int p = 0; p < U3_NUM_PLACES; p++) {
			if (!u3_area_known(p + 1)) continue;
			int bx = 6 + col * 156, by = y;
			button(bx, by, 152, 24, ID_MAP_PLACE + p + 1, u3_place_names[p], font_small,
			       map_view_area() == p + 1 ? C_ACCENT_BG : C_BTN);
			col++;
			if (col == 2) { col = 0; y += 28; }
			if (y > 206) break;
		}
		return;
	}
	bool dungeon = a > 0 && u3_place_kind(a - 1) == 2;
	if (a == 0) snprintf(title, sizeof title, "Sosaria");
	else snprintf(title, sizeof title, "%s", u3_place_names[a - 1]);
	if (dungeon) {
		int L = map_level >= 0 ? map_level : (u3_map_current_area() == a && u3s.map == U3MAP_DUNGEON ? u3s.dlevel : 0);
		u3_draw_dungeon(cv, a, L, 0, 32, 208, 208, true);
		cv.fill(208, 32, 112, 208, C_PANEL);
		cv.text(font_small, 214, 36, fit_str(font_small, title, 100).c_str(), C_DIM);
		char lt[16]; snprintf(lt, sizeof lt, "Level %d", L + 1);
		cv.text(font_bold, 214, 50, lt, C_INFO);
		for (int i = 0; i < 8; i++) {
			char b[4]; snprintf(b, sizeof b, "%d", i + 1);
			bool known = u3_dungeon_level_known(a, i);
			bool here = u3_map_current_area() == a && u3s.map == U3MAP_DUNGEON && u3s.dlevel == i;
			int bx = 214 + (i % 2) * 52, by = 70 + (i / 2) * 32;
			if (!known && !here && i != L) { button_off(bx, by, 48, 28, b, font_big); continue; }   // nothing seen there yet
			uint32_t bg = i == L ? C_ACCENT_BG : C_BTN;
			uint32_t fg = i == L ? C_ACCENT : C_TEXT;
			button(bx, by, 48, 28, ID_MAP_LEVEL + i, b, font_big, bg, fg);
			if (here) cv.disc(bx + 42, by + 6, 3, 0xFFE070);
		}
		button(214, 204, 100, 30, ID_MAP_PLACES, "Places", font_ui);
		return;
	}
	// 64x64 area
	int ts = map_zoom == 0 ? 3 : 8;
	int mw = 236, mh = 208;
	if (map_follow && u3_map_current_area() == a) { map_cx = u3s.x + 0.5f; map_cy = u3s.y + 0.5f; }
	if (map_zoom == 0) { map_cx = 32; map_cy = 32; }
	u3_draw_area(cv, a, 0, 32, mw, mh, map_cx, map_cy, ts, true);
	cv.fill(mw, 32, 320 - mw, 208, C_PANEL);
	cv.text(font_small, mw + 6, 36, fit_str(font_small, title, 320 - mw - 12).c_str(), C_DIM);
	if (u3_map_current_area() == a && u3s.valid) {
		char pos[24]; snprintf(pos, sizeof pos, "%d, %d", u3s.x, u3s.y);
		cv.text(font_small, mw + 6, 50, pos, C_FAINT);
	}
	button(mw + 6, 70, 72, 32, ID_MAP_ZOOM, map_zoom == 0 ? "Zoom in" : "Whole", font_ui);
	if (map_zoom == 0) button_off(mw + 6, 106, 72, 32, "Center", font_ui);   // the whole map is already shown
	else button(mw + 6, 106, 72, 32, ID_MAP_CENTER, "Center", font_ui, map_follow ? C_ACCENT_BG : C_BTN, map_follow ? C_ACCENT : C_TEXT);
	button(mw + 6, 204, 72, 30, ID_MAP_PLACES, "Places", font_ui);
	if (map_zoom == 1 && !u3v_swap) cv.text(font_small, mw + 6, 148, "Drag to pan", C_FAINT);
}

static void map_press(int id) {
	if (id >= ID_MAP_LEVEL && id < ID_MAP_LEVEL + 8) map_level = id - ID_MAP_LEVEL;
	else if (id == ID_MAP_ZOOM) { map_zoom ^= 1; map_follow = true; }
	else if (id == ID_MAP_CENTER) { map_follow = true; map_area = -1; map_level = -1; }
	else if (id == ID_MAP_PLACES) { map_picker = true; focus_id = ID_MAP_BACK; }
	else if (id == ID_MAP_BACK) { map_picker = false; focus_id = ID_MAP_PLACES; }
	else if (id >= ID_MAP_PLACE && id < ID_MAP_PLACE + 20) {
		int a = id - ID_MAP_PLACE;
		map_area = (a == u3_map_current_area()) ? -1 : a;
		map_level = -1; map_picker = false; map_follow = map_area < 0;
		if (!map_follow) { map_cx = 32; map_cy = 32; }
		focus_id = ID_MAP_PLACES;
	}
	dirty = true;
}

// ------------------------------------------------------------------ log tab
static bool log_pinned_only = false;
static int log_scroll = 0;            // lines scrolled up from the newest
static std::vector<int> log_view;     // indices into u3_log

static void build_log_view() {
	log_view.clear();
	for (int i = 0; i < (int)u3_log.size(); i++)
		if (!log_pinned_only || u3_log[i].pinned) log_view.push_back(i);
}

static void draw_log() {
	build_log_view();
	cv.fill(0, 32, 320, 30, C_PANEL);
	button(6, 35, 92, 24, ID_LOG_FILTER, log_pinned_only ? "Show all" : "Pinned only", font_small);
	cv.text(font_small, 106, 41, "Tap a line to pin it as a clue", C_FAINT);
	const int lh = 17, top = 64, rows = (240 - top) / lh;
	int n = (int)log_view.size();
	if (log_scroll > n - rows) log_scroll = n - rows;
	if (log_scroll < 0) log_scroll = 0;
	int first = n - rows - log_scroll;
	if (first < 0) first = 0;
	if (n == 0) {
		cv.text_center(font_ui, 160, 120, log_pinned_only ? "No pinned clues yet" : "Messages will collect here", C_FAINT);
		return;
	}
	for (int r = 0; r < rows && first + r < n; r++) {
		const U3Line &l = u3_log[log_view[first + r]];
		int y = top + r * lh;
		int id = ID_LOG_LINE + log_view[first + r];
		int look = u3_press_look(id, held_on(id));
		int off = 0;
		if (look) off = u3_btn_bg(cv, 0, y, 320, lh, 3, l.pinned ? 0x221D12 : C_BG, look);
		else if (l.pinned) cv.fill(0, y, 320, lh, 0x221D12);
		y += off;
		if (l.pinned) cv.text(font_bold, 6, y + 1, "*", C_ACCENT);
		const u3_font &lf = l.pinned ? font_bold : font_ui;
		std::string txt = fit_str(lf, l.text, 296);
		cv.text(lf, 18, y + 1, txt.c_str(), l.pinned ? 0xF4E2B8 : C_TEXT);
		if (l.pinned || log_pinned_only) {
			int tw = u3_text_width(lf, txt.c_str());
			std::string wh = fit_str(font_small, l.where, 314 - (18 + tw + 8));
			int ww = u3_text_width(font_small, wh.c_str());
			if (!wh.empty() && wh != "...") cv.text(font_small, 314 - ww, y + 3, wh.c_str(), C_FAINT);
		}
		y -= off;
		reg(0, y, 320, lh, id);
	}
	// scrollbar
	if (n > rows) {
		const int sbh = rows * lh;
		int sh = sbh * rows / n; if (sh < 12) sh = 12;
		int sy = top + (sbh - sh) * (n - rows - log_scroll) / (n - rows);
		cv.fill(316, sy, 3, sh, C_FAINT);
	}
}

// ------------------------------------------------------------------ settings tab
static int confirm = 0;   // 1 = reset map
static void save_settings();
static void apply_swap();

static void toggle_row(int y, int id, const char *label, const char *desc, bool on) {
	const int h = 25;   // rule 11: rows 4 px apart, 6 px in from the edges
	int off = u3_btn_bg(cv, 6, y, 308, h, 6, C_BTN, u3_press_look(id, held_on(id)));
	int y0 = y;
	y += off;
	cv.text(font_bold, 12, y, fit_str(font_bold, label, 250).c_str(), C_TEXT);
	cv.text(font_small, 12, y + 12, fit_str(font_small, desc, 250).c_str(), C_DIM);
	cv.fill_round(266, y + 3, 38, 18, 9, on ? 0x3E7A4A : 0x3A3F4C);
	cv.disc(on ? 295 : 275, y + 12, 7, on ? 0xD8F5DC : 0x9AA0AC);
	reg(6, y0, 308, h, id);
}

static void draw_settings() {
	if (confirm) {
		cv.text_center(font_bold, 160, 70, "Forget everything on the auto-map?", C_TEXT);
		cv.text_center(font_small, 160, 92, "Use this when you start a new game.", C_DIM);
		button(40, 130, 110, 40, ID_CONFIRM_YES, "Reset map", font_bold, 0x4A2222, 0xFFB0B0);
		button(170, 130, 110, 40, ID_CONFIRM_NO, "Keep it", font_bold);
		return;
	}
	toggle_row(34, ID_SET + 4, "Game on the touchscreen", "Swap the screens. ZL or SELECT swaps any time", u3_settings.swapped);
	toggle_row(63, ID_SET + 0, "Info strips", "Moons, wind, party health, food, Marks and Cards", u3_settings.strips);
	toggle_row(92, ID_SET + 1, "Fast-forward toggles", "Off: hold ZR. On: tap ZR to switch it on and off", u3_settings.turbo_toggle);
	toggle_row(121, ID_SET + 2, "Swap A and B", "Off: A = Enter, B = Esc", u3_settings.swap_ab);
	toggle_row(150, ID_SET + 5, "Left-handed", "D-Pad and A/B/X/Y swap places", u3_settings.lefty);
	toggle_row(179, ID_SET + 3, "Show the cluebook", "Adds the original cluebook (spoilers!) to Book", u3_settings.cluebook);
	button(6, 208, 150, 26, ID_SET + 10, "Reset auto-map", font_ui);
	cv.text(font_small, 164, 208, "START: save, manual,", C_FAINT);
	cv.text(font_small, 164, 221, "exit the game", C_FAINT);
}

static void settings_press(int id) {
	switch (id) {
	case ID_SET + 0: u3_settings.strips = !u3_settings.strips; u3_top_update(true); u3v_present(); break;
	case ID_SET + 1: u3_settings.turbo_toggle = !u3_settings.turbo_toggle; set_turbo(false); break;
	case ID_SET + 2: u3_settings.swap_ab = !u3_settings.swap_ab; break;
	case ID_SET + 5: u3_settings.lefty = !u3_settings.lefty; break;
	case ID_SET + 3: u3_settings.cluebook = !u3_settings.cluebook; break;
	case ID_SET + 4: u3_settings.swapped = !u3_settings.swapped; apply_swap(); break;
	case ID_SET + 10: confirm = 1; focus_id = ID_CONFIRM_NO; break;
	case ID_CONFIRM_YES: u3_map_reset(); confirm = 0; focus_id = ID_SET + 10; break;
	case ID_CONFIRM_NO: confirm = 0; focus_id = ID_SET + 10; break;
	}
	save_settings();
	dirty = true;
}

// ------------------------------------------------------------------ START menu drawing
static void draw_smenu() {
	cv.fill(0, 0, 320, 240, C_BG);
	cv.fill(0, 0, 320, 24, C_PANEL);
	cv.text(font_bold, 10, 4, "Ultima III", C_ACCENT);
	const char *hint = "B or START closes";
	cv.text(font_small, 312 - u3_text_width(font_small, hint), 7, hint, C_DIM);
	if (smenu_confirm) {
		cv.text_center(font_bold, 160, 52, "Exit Ultima III?", C_TEXT);
		cv.text_center(font_small, 160, 76, "Anything since your last save is lost.", C_DIM);
		cv.text_center(font_small, 160, 92, "Save game (outdoors) first to keep it.", C_DIM);
		button(30, 124, 124, 44, ID_SM_EXIT_YES, "Exit game", font_bold, 0x4A2222, 0xFFB0B0);
		button(166, 124, 124, 44, ID_SM_EXIT_NO, "Keep playing", font_bold);
		return;
	}
	const int x = 50, w = 220, h = 36, step = 41;
	int y = 32;
	button(x, y, w, h, ID_SM_RESUME, "Resume", font_bold, C_ACCENT_BG, C_ACCENT); y += step;
	button(x, y, w, h, ID_SM_MANUAL, "Manual", font_bold); y += step;
	if (can_save()) button(x, y, w, h, ID_SM_SAVE, "Save game", font_bold);
	else {
		cv.fill_round(x, y, w, h, 5, C_OFF_BG);
		cv.text(font_bold, x + 14, y + (h - font_bold.height) / 2, "Save game", C_OFF_FG);
		const char *why = u3s.valid && u3s.nparty > 0 ? "outdoors only" : "start a game first";
		cv.text(font_small, x + w - 10 - u3_text_width(font_small, why), y + (h - font_small.height) / 2 + 1, why, C_OFF_FG);
	}
	y += step;
	button(x, y, w, h, ID_SM_SETTINGS, "Settings", font_bold); y += step;
	button(x, y, w, h, ID_SM_EXIT, "Exit game", font_bold, 0x3A2626, 0xFFC0C0);
}

static void smenu_press(int id) {
	switch (id) {
	case ID_SM_RESUME: close_smenu(); break;
	case ID_SM_MANUAL: close_smenu(); u3_book_open_guide(); set_tab(TAB_BOOK); break;
	case ID_SM_SAVE: if (can_save() && u3_key_tap('Q')) close_smenu(); break;
	case ID_SM_SETTINGS: close_smenu(); set_tab(TAB_SET); break;
	case ID_SM_EXIT: smenu_confirm = 1; focus_id = ID_SM_EXIT_NO; break;
	case ID_SM_EXIT_NO: smenu_confirm = 0; focus_id = ID_SM_EXIT; break;
	case ID_SM_EXIT_YES:
		u3_ui_save_all();
		u3w_flush(8000);
		throw(0);
	}
	dirty = true;
}

// ------------------------------------------------------------------ settings file
static void save_settings() {
	char buf[200];
	int n = snprintf(buf, sizeof buf, "strips=%d\nturbo_toggle=%d\nswap_ab=%d\ncluebook=%d\nswapped=%d\nlefty=%d\n",
	                 u3_settings.strips, u3_settings.turbo_toggle, u3_settings.swap_ab, u3_settings.cluebook, u3_settings.swapped, u3_settings.lefty);
	u3w_put(U3_DIR "/settings.cfg", buf, (size_t)n);
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
		else if (sscanf(line, "swapped=%d", &v) == 1) u3_settings.swapped = v;
		else if (sscanf(line, "lefty=%d", &v) == 1) u3_settings.lefty = v;
	}
	fclose(f);
}

void u3_ui_save_all() {
	u3_map_save(true);
	u3_pins_save(U3_DIR "/clues.txt");
	save_settings();
}

// ------------------------------------------------------------------ screen swap
// Normal: game on top (4:3, full height, strips beside it), this panel on the touchscreen.
// Swapped: the game fills the touchscreen; this panel sits between the strips on top,
// worked with the D-Pad and A (tabs with L/R), and the Circle Pad moves the party.
static void apply_swap() {
	int want = (u3_settings.swapped && !loading) ? 1 : 0;
	if (want != u3v_swap) {
		u3v_swap = want;
		u3_top_set_panel(want ? bottom_px : 0);
		if (want && focus_id < 0) focus_id = ID_TAB + tab;
	}
	dirty = true;
	u3_top_update(true);
	u3v_present();
}
static void toggle_swap() {
	u3_settings.swapped = !u3_settings.swapped;
	save_settings();
	apply_swap();
}

// ------------------------------------------------------------------ D-Pad highlight
static bool focus_nav() { return smenu || u3v_swap; }

static void focus_fix() {
	if (find_btn(focus_id) || btns.empty()) return;
	// first button below the tab bar, else the first one
	for (size_t i = 0; i < btns.size(); i++) if (btns[i].y >= 24 || smenu) { focus_id = btns[i].id; return; }
	focus_id = btns[0].id;
}
static void focus_move(int dx, int dy) {
	const Btn *c = find_btn(focus_id);
	if (!c) { focus_fix(); return; }
	float cx = c->x + c->w * 0.5f, cy = c->y + c->h * 0.5f;
	int best = -1; float best_s = 1e9f;
	for (size_t i = 0; i < btns.size(); i++) {
		const Btn &b = btns[i];
		if (b.id == focus_id) continue;
		float bx = b.x + b.w * 0.5f, by = b.y + b.h * 0.5f;
		float along = (bx - cx) * dx + (by - cy) * dy;
		if (along <= 2) continue;
		float side = fabsf(dx ? (by - cy) : (bx - cx));
		float s = along + side * 2.5f;
		if (s < best_s) { best_s = s; best = (int)i; }
	}
	if (best >= 0) { focus_id = btns[best].id; dirty = true; }
}
static void draw_focus() {
	const Btn *b = find_btn(focus_id);
	if (!b) return;
	cv.frame_round(b->x, b->y, b->w, b->h, 5, C_ACCENT);
	cv.frame_round(b->x + 1, b->y + 1, b->w - 2, b->h - 2, 4, C_ACCENT, 160);
}

// ------------------------------------------------------------------ draw
static void draw_all() {
	btns.clear();
	cv.clear(C_BG);
	cur_prompt = prompt_kind();
	if (smenu) draw_smenu();
	else if (kb_open) { draw_tabs(); draw_keyboard(); }
	else {
		draw_tabs();
		switch (tab) {
		case TAB_PAD: draw_pad(); break;
		case TAB_MAP: draw_map(); break;
		case TAB_BOOK: u3_book_draw(cv, 32); break;
		case TAB_LOG: draw_log(); break;
		case TAB_SET: draw_settings(); break;
		}
	}
	bool book_open = !smenu && !kb_open && tab == TAB_BOOK;   // the manual has its own highlight
	if (focus_nav() && !book_open) { focus_fix(); draw_focus(); }
	if (turbo_on && !smenu) {
		cv.fill_round(250, 228, 66, 12, 4, 0x3A3320);
		cv.text_center(font_small, 283, 228, "fast-forward", 0xFFE070);
	}
	if (u3v_swap) u3_top_panel_changed();
	else cv.present_bottom();
	last_draw = u3_ticks_ms();
	dirty = false;
}

// ------------------------------------------------------------------ actions
static void press(int id) {
	if (smenu) { smenu_press(id); return; }
	if (id >= ID_TAB && id < ID_TAB + TAB_COUNT) {
		if (kb_open) close_keyboard();
		set_tab(id - ID_TAB);
		focus_id = id;
		return;
	}
	if (kb_open) { kb_press(id); return; }
	switch (tab) {
	case TAB_PAD:
		if (id >= ID_CMD && id < ID_CMD + 26) {
			char k = CMDS[id - ID_CMD].key;
			if (u3_key_tap(k) && (k == 'O' || k == 'Y')) open_keyboard(0, true);
		} else if (id >= ID_NUM + 1 && id <= ID_NUM + 4) u3_key_tap((char)('0' + id - ID_NUM));
		else if (id >= ID_DIR && id < ID_DIR + 4) key_special(ARROW_KEYS[id - ID_DIR]);
		else if (id == ID_YES) u3_key_tap('Y');
		else if (id == ID_NO) u3_key_tap('N');
		else if (id == ID_PASS) u3_key_tap(' ');
		else if (id == ID_ESC) u3_key_tap(27);
		else if (id == ID_ENTER) u3_key_tap('\n');
		else if (id == ID_KBD) open_keyboard(0);
		else if (id == ID_DIGITS) { open_keyboard(1); focus_id = ID_KEY + '1'; }
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
	case TAB_SET: settings_press(id); break;
	}
}
// A button pressed with A (D-Pad highlight): same click and flash as a tap.
static void press_focused() {
	if (!find_btn(focus_id)) return;
	u3_click();
	u3_press_up(focus_id);
	press(focus_id);
}

// ------------------------------------------------------------------ init
static void ui_init() {
	cv.init(bottom_px, 320, 240, U3_RGB565);
	keyb_typematic = false;
	u3w_init();
	load_settings();
	u3_text_init(U3_GAME_DIR "/CHARSET.VGA");
	u3_map_init(U3_GAME_DIR "/SHAPES.VGA", U3_GAME_DIR "/U3VGA.PAL", U3_DIR "/automap.dat");
	u3_pins_load(U3_DIR "/clues.txt");
	u3_top_loading(true, 0);
	inited = true;
	dirty = true;
	u3_top_update(true);
}

// ------------------------------------------------------------------ frame hook
static uint32_t last_scan = 0;
static uint32_t start_t = 0;

void u3_ui_on_frame(const uint16_t *pix, int pitch, int w, int h) {
	uint32_t now = u3_ticks_ms();
	if (loading && w == 320 && h == 200) {
		// the game has drawn its first picture once a fair part of the screen is lit
		int lit = 0;
		for (int y = 0; y < 200; y += 4)
			for (int x = 0; x < 320; x += 4) if (pix[y * pitch + x]) lit++;
		if (lit > 200) { loading = false; u3_top_loading(false, 0); apply_swap(); }
	}
	if (now - last_scan >= 90) {
		last_scan = now;
		u3_text_scan(pix, pitch, w, h);
	}
}

// ------------------------------------------------------------------ input
static uint32_t last_state_changes = 0, last_log_changes = 0, last_map_changes = 0;
static uint32_t last_slow = 0;
static int last_prompt = -1;

// D-Pad repeat for moving the highlight: first step at once, then after 350 ms every 90 ms
static int dpad_dir(u32 kh, uint32_t now) {
	static u32 last = 0; static uint32_t t0 = 0, tr = 0;
	u32 d = kh & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT);
	if (!d) { last = 0; return 0; }
	bool fire = false;
	if (d != last) { last = d; t0 = tr = now; fire = true; }
	else if (now - t0 >= 350 && now - tr >= 90) { tr = now; fire = true; }
	if (!fire) return 0;
	return d;
}

// Left-handed play: the D-pad and the face buttons swap places
// (Y<->Up, A<->Right, B<->Down, X<->Left), applied where the pad is read.
static u32 lefty_remap(u32 m) {
	static const u32 pr[4][2] = { { KEY_Y, KEY_DUP }, { KEY_A, KEY_DRIGHT }, { KEY_B, KEY_DDOWN }, { KEY_X, KEY_DLEFT } };
	if (!u3_settings.lefty) return m;
	u32 o = m;
	for (int i = 0; i < 4; i++) {
		o &= ~(pr[i][0] | pr[i][1]);
		if (m & pr[i][0]) o |= pr[i][1];
		if (m & pr[i][1]) o |= pr[i][0];
	}
	return o;
}

void u3_ui_input() {
	uint32_t now = u3_ticks_ms();
	if (!inited) { ui_init(); start_t = now; }

	key_pump(now);

	// SDL's event pump also calls hidScanInput(), so hidKeysDown() is unreliable here:
	// derive presses/releases from the held state ourselves.
	static u32 prev_held = 0;
	u32 kh = lefty_remap(hidKeysHeld());
	u32 kd = kh & ~prev_held;
	prev_held = kh;

	bool reading = tab == TAB_BOOK && !kb_open && !smenu;   // the manual is open
	bool nav = focus_nav() && !reading;                      // D-Pad moves the highlight

	// --- movement. Reading or in the START menu nothing moves the game. Swapped, the
	// D-Pad moves the highlight on the top screen and the Circle Pad moves the party.
	circlePosition cp; hidCircleRead(&cp);
	bool move_ok = !reading && !smenu;
	int cx = move_ok ? cp.dx : 0, cy = move_ok ? cp.dy : 0;
	u32 dk = (move_ok && !nav) ? kh : 0;
	arrow(0, (dk & KEY_DUP) || cy > 70, now);
	arrow(1, (dk & KEY_DDOWN) || cy < -70, now);
	arrow(2, (dk & KEY_DLEFT) || cx < -70, now);
	arrow(3, (dk & KEY_DRIGHT) || cx > 70, now);
	if (reading) {
		static uint32_t book_tick_t = 0;
		if (now - book_tick_t >= 33) { book_tick_t = now; u3_book_tick(); if (u3_book_dirty()) dirty = true; }
	}
	if (nav) {
		u32 d = dpad_dir(kh, now);
		if (d & KEY_DUP) focus_move(0, -1);
		else if (d & KEY_DDOWN) focus_move(0, 1);
		else if (d & KEY_DLEFT) focus_move(-1, 0);
		else if (d & KEY_DRIGHT) focus_move(1, 0);
	}
	// C-Stick scrolls the Log tab
	if (tab == TAB_LOG && !kb_open && !smenu) {
		circlePosition cs; hidCstickRead(&cs);
		static uint32_t cs_t = 0;
		if ((cs.dy > 40 || cs.dy < -40) && now - cs_t >= 70) { cs_t = now; log_scroll += cs.dy > 0 ? 1 : -1; dirty = true; }
	}

	// --- fast-forward
	if (u3_settings.turbo_toggle) { if (kd & KEY_ZR) set_turbo(!turbo_on); }
	else set_turbo((kh & KEY_ZR) != 0);

	// --- buttons
	u32 kA = u3_settings.swap_ab ? KEY_B : KEY_A, kB = u3_settings.swap_ab ? KEY_A : KEY_B;
	if (kd & (KEY_ZL | KEY_SELECT)) toggle_swap();   // swap the screens, any time
	kd &= ~(u32)(KEY_ZL | KEY_SELECT | KEY_ZR);
	if (smenu) {
		if (kd & kA) press_focused();
		else if (kd & kB) { if (smenu_confirm) { smenu_confirm = 0; focus_id = ID_SM_EXIT; } else close_smenu(); }
		else if (kd & KEY_START) close_smenu();
		if (kd) dirty = true;
	} else if (reading) {
		// The manual takes every button: nothing pressed while reading reaches the game.
		int r = u3_book_buttons(kd, kh);
		if (r == 2) set_tab(book_return_tab);
		if (r || kd) dirty = true;
	} else if (kd) {
		if (kd & kA) { if (nav) press_focused(); else u3_key_tap('\n'); }
		if (kd & kB) u3_key_tap(27);
		if (kd & KEY_X) u3_key_tap(' ');
		if (kd & KEY_Y) { if (kb_open) close_keyboard(); else open_keyboard(0); }
		if (kd & KEY_L) { if (kb_open) close_keyboard(); set_tab(tab - 1); if (nav) focus_id = ID_TAB + tab; }
		if (kd & KEY_R) { if (kb_open) close_keyboard(); set_tab(tab + 1); if (nav) focus_id = ID_TAB + tab; }
		if (kd & KEY_START) open_smenu();
		dirty = true;
	}

	// --- touch
	if (u3v_swap) {
		// The game is on the touchscreen: a tap in the map window takes one step toward it
		// (the party is in the middle of that window).
		static bool tdown = false;
		if ((kh & KEY_TOUCH) && !tdown) {
			touchPosition t; hidTouchRead(&t);
			if (!(t.px == 0 && t.py == 0)) {
				tdown = true;
				int gx = t.px, gy = t.py * 200 / 240;
				if (!smenu && gx < 192 && gy < 192) {
					int dx = gx - 96, dy = gy - 96;
					if (abs(dx) > 8 || abs(dy) > 8) {
						int d = abs(dx) > abs(dy) ? (dx < 0 ? 2 : 3) : (dy < 0 ? 0 : 1);
						key_special(ARROW_KEYS[d]);
					}
				}
			}
		} else if (!(kh & KEY_TOUCH)) tdown = false;
		touching = false;
	} else if ((kh & KEY_TOUCH) && !touching) {
		touchPosition t; hidTouchRead(&t);
		if (t.px == 0 && t.py == 0) goto touch_done;   // first sample can be empty
		touching = true; touch_x0 = touch_x = t.px; touch_y0 = touch_y = t.py; touch_t0 = now;
		touch_id = hit(t.px, t.py);
		bool book_area = reading && t.py >= 32;
		bool log_line = touch_id >= ID_LOG_LINE && !smenu;   // log lines click on release (they also scroll)
		if (!book_area && !log_line) u3_press_down(touch_id);
		if (book_area) u3_book_touch(t.px, t.py, 0);
		dirty = true;
	} else if ((kh & KEY_TOUCH) && touching) {
		touchPosition t; hidTouchRead(&t);
		int dx = t.px - touch_x, dy = t.py - touch_y;
		if (dx || dy) {
			if (reading && touch_y0 >= 32) { u3_book_touch(t.px, t.py, 1); }
			else if (!smenu && tab == TAB_MAP && !kb_open && !map_picker && map_zoom == 1 && touch_x0 < 236 && touch_y0 > 32) {
				map_follow = false; map_cx -= dx / 8.0f; map_cy -= dy / 8.0f;
				if (map_cx < 0) map_cx += 64; if (map_cx >= 64) map_cx -= 64;
				if (map_cy < 0) map_cy += 64; if (map_cy >= 64) map_cy -= 64;
				touch_id = ID_NONE;
			} else if (!smenu && tab == TAB_LOG && !kb_open && touch_y0 > 48) {
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
		if (reading && touch_y0 >= 32) u3_book_touch(touch_x, touch_y, 2);
		else if (id != ID_NONE && id == touch_id) {
			if (id >= ID_LOG_LINE && !smenu) u3_click();
			u3_press_up(id);
			focus_id = id;
			press(id);
		}
		touch_id = -1;
		dirty = true;
	}
touch_done:

	// --- never look dead: "Working..." when a press hasn't been taken within 0.35 s,
	// "Saving..." while the SD card writer has been busy 0.5 s. A key the game still
	// hasn't taken after 1.5 s is dropped (presses are never kept for later).
	{
		static uint32_t busy_t = 0, pill_t = 0;
		bool waiting = KEYBOARD_Busy() || !keys_idle();
		if (!waiting) press_t = 0;
		if (waiting) { if (!busy_t) busy_t = now; } else busy_t = 0;
		if (busy_t && keys_idle() && now - busy_t > 1500) { KEYBOARD_DropTyped(); busy_t = 0; press_t = 0; }
		int kind = 0;
		if (press_t && now - press_t >= 350) kind = 1;
		if (u3w_busy_ms() >= 500) kind = 2;
		if (loading) kind = 0;
		if (kind) { if (now - pill_t >= 40) { pill_t = now; u3_top_working(kind, now); } }
		else u3_top_working(0, now);
	}

	// --- loading screen until the game draws (animated; files read so far)
	if (loading) {
		static uint32_t load_t = 0;
		if (now - start_t > 120000) { loading = false; u3_top_loading(false, 0); apply_swap(); }
		else if (now - load_t >= 50) {
			load_t = now;
			u3_top_loading(true, (int)u3_files_opened);
			u3_top_update(true);
			u3v_present();
		}
	}

	// --- performance log (read back over FTP when tuning)
	{
		extern unsigned int u3_emu_ticks;
		extern unsigned int u3_mixer_underruns;
		static uint32_t perf_t = 0; static unsigned int perf_ticks = 0, perf_under = 0;
		if (!perf_t) { perf_t = now; perf_ticks = u3_emu_ticks; perf_under = u3_mixer_underruns; }
		else if (now - perf_t >= 10000) {
			int speed = (int)((u3_emu_ticks - perf_ticks) * 100 / (now - perf_t));
			dbg("speed %d%% of real time, audio underruns %d", speed, (int)(u3_mixer_underruns - perf_under));
			perf_t = now; perf_ticks = u3_emu_ticks; perf_under = u3_mixer_underruns;
		}
	}

	// --- slow work (~12 Hz)
	if (now - last_slow >= 80) {
		last_slow = now;
		u3_state_update();
		u3_map_update();
		u3_top_update(false);
		if (u3s.changes != last_state_changes && (tab == TAB_PAD || tab == TAB_MAP || smenu)) { last_state_changes = u3s.changes; dirty = true; }
		if (u3_log_changes != last_log_changes && tab == TAB_LOG) { last_log_changes = u3_log_changes; dirty = true; }
		if (u3_map_changes != last_map_changes && tab == TAB_MAP) { last_map_changes = u3_map_changes; dirty = true; }
		if (tab == TAB_BOOK && u3_book_dirty()) dirty = true;
		if (tab == TAB_PAD) { int p = prompt_kind(); if (p != last_prompt) { last_prompt = p; dirty = true; } }
	}

	// --- redraw (cap ~30 fps)
	if (u3_press_animating()) dirty = true;
	if (dirty && now - last_draw >= 33) draw_all();
}
