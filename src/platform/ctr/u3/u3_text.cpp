// u3_text.cpp - message window reader / transcript (see u3_text.h)
#include "u3_text.h"
#include "u3_io.h"
#include "u3_state.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <map>
#include <algorithm>

std::vector<U3Line> u3_log;
uint32_t u3_log_changes = 0;

// The text window is columns 0x18-0x27, rows 0x11-0x17 (8x8 character cells)
enum { TW_COL = 24, TW_ROW = 17, TW_COLS = 16, TW_ROWS = 7, LOG_MAX = 1500 };

static std::map<uint64_t, char> glyphs;
static bool glyphs_ok = false;
static char cur[TW_ROWS][TW_COLS + 1];
static char prev[TW_ROWS][TW_COLS + 1];
static std::vector<std::string> typed_words;

bool u3_text_init(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	uint8_t d[8192];
	size_t n = fread(d, 1, sizeof d, f);
	fclose(f);
	if (n < 128 * 64) return false;
	glyphs.clear();
	for (int c = 127; c >= 32; c--) {  // lower codes win on collisions
		uint64_t m = 0;
		for (int i = 0; i < 64; i++) if (d[c * 64 + i]) m |= 1ull << i;
		if (m) glyphs[m] = (char)c;
	}
	// ',' and '.' share a shape in some fonts; a period is far more common in game text
	{ uint64_t m = 0; for (int i = 0; i < 64; i++) if (d['.' * 64 + i]) m |= 1ull << i; if (m) glyphs[m] = '.'; }
	glyphs_ok = true;
	memset(cur, 0, sizeof cur);
	memset(prev, 0, sizeof prev);
	return true;
}

static char decode_cell(const uint16_t *pix, int pitch, int cx, int cy) {
	uint64_t m = 0;
	const uint16_t *p = pix + (cy * 8) * pitch + cx * 8;
	for (int y = 0; y < 8; y++, p += pitch)
		for (int x = 0; x < 8; x++) if (p[x]) m |= 1ull << (y * 8 + x);
	if (!m) return ' ';
	std::map<uint64_t, char>::iterator it = glyphs.find(m);
	if (it != glyphs.end()) return it->second;
	return 0; // unknown (graphic, cursor, partially drawn)
}

static std::string trim(const char *s) {
	std::string r(s);
	size_t a = r.find_first_not_of(' ');
	if (a == std::string::npos) return "";
	size_t b = r.find_last_not_of(' ');
	return r.substr(a, b - a + 1);
}

static void commit(const char *line) {
	std::string t = trim(line);
	if (t.empty()) return;
	U3Line l;
	l.text = t;
	char where[64];
	l.where = u3_location_name(where, sizeof where);
	l.pinned = false;
	u3_log.push_back(l);
	if (u3_log.size() > LOG_MAX) {
		// drop oldest unpinned
		for (size_t i = 0; i < u3_log.size(); i++)
			if (!u3_log[i].pinned) { u3_log.erase(u3_log.begin() + i); break; }
	}
	u3_log_changes++;
}

void u3_text_scan(const uint16_t *pix, int pitch, int w, int h) {
	if (!glyphs_ok || !pix || w != 320 || h != 200) return;
	char now[TW_ROWS][TW_COLS + 1];
	for (int r = 0; r < TW_ROWS; r++) {
		for (int c = 0; c < TW_COLS; c++) {
			char ch = decode_cell(pix, pitch, TW_COL + c, TW_ROW + r);
			if (!ch) {
				// Unknown glyph: if this isn't the cursor cell, the window isn't plain text
				ch = ' ';
			}
			now[r][c] = ch;
		}
		now[r][TW_COLS] = 0;
	}
	if (!memcmp(now, cur, sizeof now)) return;
	memcpy(prev, cur, sizeof cur);
	memcpy(cur, now, sizeof cur);

	// Only the bottom line changed (text still being typed): nothing to commit
	if (!memcmp(prev, cur, sizeof(cur[0]) * (TW_ROWS - 1))) return;

	// Find how far the window scrolled: prev[k..6] == cur[0..6-k]
	int k;
	for (k = 1; k < TW_ROWS; k++) {
		bool ok = true;
		for (int r = k; r < TW_ROWS - 1 && ok; r++)  // ignore bottom (may still be typing)
			if (memcmp(prev[r], cur[r - k], TW_COLS)) ok = false;
		if (ok) break;
	}
	if (k < TW_ROWS) {
		// lines that moved up out of the bottom row are now final
		for (int r = TW_ROWS - 1 - k; r < TW_ROWS - 1; r++) commit(cur[r]);
	} else {
		// wholesale change (screen cleared / redrawn): keep what the old window showed
		bool any_common = false;
		for (int r = 0; r < TW_ROWS; r++) if (!memcmp(prev[r], cur[r], TW_COLS) && trim(cur[r]).size()) any_common = true;
		if (!any_common) {
			// commit the old bottom line which never scrolled
			commit(prev[TW_ROWS - 1]);
		}
	}
}

const char *u3_text_window_line(int i) { return (i >= 0 && i < TW_ROWS) ? cur[i] : ""; }

bool u3_text_window_has(const char *needle) {
	for (int r = 0; r < TW_ROWS; r++) {
		char low[TW_COLS + 1];
		for (int c = 0; c <= TW_COLS; c++) low[c] = (char)tolower((unsigned char)cur[r][c]);
		if (strstr(low, needle)) return true;
	}
	return false;
}

// Pins file: one per line "where\ttext"; typed words: "#word"
void u3_pins_load(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return;
	char buf[512];
	while (fgets(buf, sizeof buf, f)) {
		size_t n = strlen(buf);
		while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
		if (!n) continue;
		if (buf[0] == '#') { typed_words.push_back(buf + 1); continue; }
		char *tab = strchr(buf, '\t');
		U3Line l;
		l.pinned = true;
		if (tab) { *tab = 0; l.where = buf; l.text = tab + 1; }
		else l.text = buf;
		u3_log.push_back(l);
	}
	fclose(f);
	u3_log_changes++;
}

void u3_pins_save(const char *path) {
	std::string out;
	for (size_t i = 0; i < u3_log.size(); i++)
		if (u3_log[i].pinned) out += u3_log[i].where + "\t" + u3_log[i].text + "\n";
	for (size_t i = 0; i < typed_words.size(); i++) out += "#" + typed_words[i] + "\n";
	u3w_put(path, out.data(), out.size());   // background writer
}

static bool stopword(const std::string &w) {
	static const char *sw[] = { "the", "and", "you", "thou", "thee", "thy", "for", "with", "that", "this",
		"are", "art", "not", "but", "have", "hast", "was", "his", "her", "who", "from", "will", "shalt",
		"can", "all", "one", "out", "into", "what", "when", "where", "there", "here", "them", "they", 0 };
	for (int i = 0; sw[i]; i++) if (w == sw[i]) return true;
	return false;
}

void u3_words(std::vector<std::string> &out) {
	out.clear();
	for (int i = (int)typed_words.size() - 1; i >= 0; i--)
		if (std::find(out.begin(), out.end(), typed_words[i]) == out.end()) out.push_back(typed_words[i]);
	for (int i = (int)u3_log.size() - 1; i >= 0 && out.size() < 40; i--) {
		if (!u3_log[i].pinned) continue;
		const std::string &t = u3_log[i].text;
		std::string w;
		for (size_t k = 0; k <= t.size(); k++) {
			char c = k < t.size() ? t[k] : ' ';
			if (isalpha((unsigned char)c)) w += (char)toupper((unsigned char)c);
			else {
				if (w.size() >= 3) {
					std::string low = w;
					for (size_t q = 0; q < low.size(); q++) low[q] = (char)tolower((unsigned char)low[q]);
					if (!stopword(low) && std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
				}
				w.clear();
			}
		}
	}
}

void u3_word_typed(const std::string &w) {
	if (w.size() < 2) return;
	std::vector<std::string>::iterator it = std::find(typed_words.begin(), typed_words.end(), w);
	if (it != typed_words.end()) typed_words.erase(it);
	typed_words.push_back(w);
	if (typed_words.size() > 24) typed_words.erase(typed_words.begin());
}
