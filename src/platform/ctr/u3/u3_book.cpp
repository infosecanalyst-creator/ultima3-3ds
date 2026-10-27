// u3_book.cpp - built-in manual: text books (reflowed) and page scans (pan/zoom)
#include "u3_ui.h"
#include "u3_gfx.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <string>
#include <vector>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

enum : uint32_t {
	C_BG = 0x111318, C_PANEL = 0x1B1E25, C_BTN = 0x262A33, C_BTN_DOWN = 0x4A5263, C_TEXT = 0xECE8DE,
	C_DIM = 0x8C919D, C_FAINT = 0x5A6070, C_ACCENT = 0xE8B04A, C_ACCENT_BG = 0x3A2F1A, C_PAPER = 0xF3EBD9, C_INK = 0x2B2418
};

struct Book {
	const char *title;
	const char *sub;
	const char *path;    // .txt file, or directory of page images
	bool scans;
	bool spoiler;
};

static const Book BOOKS[] = {
	{ "3DS player's guide", "Controls, commands, the auto-map, first steps", U3_BOOK_DIR "/guide.txt", false, false },
	{ "Spell list", "Every wizard and cleric spell at a glance", U3_BOOK_DIR "/spells.txt", false, false },
	{ "Book of Play", "The original manual, as large text", U3_BOOK_DIR "/manual.txt", false, false },
	{ "Book of Play - pages", "The original manual's pages", U3_BOOK_DIR "/manual", true, false },
	{ "Wizard spellbook", "Original pages", U3_BOOK_DIR "/wizard", true, false },
	{ "Cleric spellbook", "Original pages", U3_BOOK_DIR "/cleric", true, false },
	{ "Cloth map of Sosaria", "The map from the box", U3_BOOK_DIR "/map", true, false },
	{ "Cluebook", "Spoilers! Hints and full solutions", U3_BOOK_DIR "/cluebook", true, true },
};
enum { NBOOKS = sizeof(BOOKS) / sizeof(BOOKS[0]) };

static int cur_book = -1;     // -1 = shelf
static bool dirty = true;
static bool avail[NBOOKS];
static bool avail_checked = false;

// ---- shared ui
struct Hit { int x, y, w, h, id; };
static std::vector<Hit> hits;
static int down_id = -1;
static int drag_x, drag_y, start_x, start_y;
static bool dragging = false;

enum { B_BACK = 1, B_PREV, B_NEXT, B_ZOOMIN, B_ZOOMOUT, B_CONTENTS, B_SCAN, B_BOOK = 100, B_TOC = 200 };

static void hreg(int x, int y, int w, int h, int id) { Hit t = { x, y, w, h, id }; hits.push_back(t); }
static int hfind(int x, int y) {
	for (int i = (int)hits.size() - 1; i >= 0; i--)
		if (x >= hits[i].x && y >= hits[i].y && x < hits[i].x + hits[i].w && y < hits[i].y + hits[i].h) return hits[i].id;
	return 0;
}
static void bbutton(U3Canvas &c, int x, int y, int w, int h, int id, const char *lbl, uint32_t bg = C_BTN, uint32_t fg = C_TEXT) {
	c.fill_round(x, y, w, h, 5, down_id == id ? C_BTN_DOWN : bg);
	int tw = u3_text_width(font_ui, lbl);
	c.text(font_ui, x + (w - tw) / 2, y + (h - font_ui.height) / 2, lbl, fg);
	hreg(x, y, w, h, id);
}

static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }

static void list_pages(const char *dir, std::vector<std::string> &out) {
	out.clear();
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		const char *n = e->d_name;
		size_t l = strlen(n);
		if (l > 4 && (!strcasecmp(n + l - 4, ".jpg") || !strcasecmp(n + l - 4, ".png")))
			out.push_back(std::string(dir) + "/" + n);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
}

static void check_avail() {
	for (int i = 0; i < NBOOKS; i++) {
		if (BOOKS[i].scans) { std::vector<std::string> p; list_pages(BOOKS[i].path, p); avail[i] = !p.empty(); }
		else avail[i] = file_exists(BOOKS[i].path);
	}
	avail_checked = true;
}

// =================================================================== text books
struct TLine { const u3_font *f; std::string s; int x; int h; uint32_t col; int page; bool rule; };
static std::vector<TLine> tlines;
static std::vector<std::pair<std::string, int> > toc;   // heading -> line index
static int tscroll = 0;       // pixels
static int ttotal = 0;
static bool show_toc = false;
enum { TEXT_TOP = 50, TEXT_W = 300 };

static void layout_text(const char *path) {
	tlines.clear(); toc.clear(); tscroll = 0; ttotal = 0;
	FILE *f = fopen(path, "r");
	if (!f) return;
	std::string all;
	char buf[1024];
	while (fgets(buf, sizeof buf, f)) all += buf;
	fclose(f);
	// split into paragraphs on blank lines
	int page = 0;
	size_t pos = 0;
	while (pos < all.size()) {
		size_t end = all.find("\n\n", pos);
		if (end == std::string::npos) end = all.size();
		std::string para = all.substr(pos, end - pos);
		pos = end + 2;
		while (!para.empty() && (para[0] == '\n' || para[0] == ' ')) para.erase(0, 1);
		while (!para.empty() && (para[para.size() - 1] == '\n' || para[para.size() - 1] == ' ')) para.erase(para.size() - 1);
		if (para.empty()) continue;
		// page anchor lines
		while (para.compare(0, 6, "@page ") == 0) {
			int p = 0, h = 0;
			sscanf(para.c_str() + 6, "%d %d", &p, &h);
			if (p != page) {
				page = p;
				TLine r = { &font_small, "", 0, 14, C_FAINT, page, true };
				char lbl[32]; snprintf(lbl, sizeof lbl, "page %d", p); r.s = lbl;
				tlines.push_back(r);
			}
			size_t nl = para.find('\n');
			para = nl == std::string::npos ? "" : para.substr(nl + 1);
		}
		if (para.empty()) continue;
		const u3_font *fnt = &font_read;
		uint32_t col = C_INK;
		int indent = 0, gap = 6;
		bool heading = false;
		if (para.compare(0, 2, "# ") == 0) { para.erase(0, 2); fnt = &font_big; col = 0x6B2F12; heading = true; gap = 10; }
		else if (para.compare(0, 3, "## ") == 0) { para.erase(0, 3); fnt = &font_readb; col = 0x4A2A10; heading = true; gap = 8; }
		else if (para.size() < 40 && para.find('\n') == std::string::npos) {
			// short ALL CAPS lines in the original manual are headings
			bool caps = true; int letters = 0;
			for (size_t k = 0; k < para.size(); k++) { char ch = para[k]; if (ch >= 'a' && ch <= 'z') caps = false; if (ch >= 'A' && ch <= 'Z') letters++; }
			if (caps && letters >= 4) { fnt = &font_readb; col = 0x6B2F12; heading = true; gap = 8; }
		}
		if (heading) toc.push_back(std::make_pair(para, (int)tlines.size()));
		// preserve explicit line breaks for lists/tables; wrap each source line
		size_t lp = 0;
		bool first = true;
		while (lp <= para.size()) {
			size_t le = para.find('\n', lp);
			if (le == std::string::npos) le = para.size();
			std::string src = para.substr(lp, le - lp);
			lp = le + 1;
			int ind = indent;
			bool bullet = src.compare(0, 4, "\xE2\x80\xA2 ") == 0 || src.compare(0, 2, "- ") == 0;
			if (src.compare(0, 4, "\xE2\x80\xA2 ") == 0) src = std::string("\x95 ") + src.substr(4);
			else if (src.compare(0, 2, "- ") == 0) src = std::string("\x95 ") + src.substr(2);
			const char *s = src.c_str();
			int hang = bullet ? u3_text_width(*fnt, "\x95 ") : 0;
			bool firstseg = true;
			while (*s) {
				int w = TEXT_W - ind - (firstseg ? 0 : hang);
				int n = u3_wrap(*fnt, s, w);
				std::string seg(s, n);
				while (!seg.empty() && seg[seg.size() - 1] == ' ') seg.erase(seg.size() - 1);
				TLine t = { fnt, seg, ind + (firstseg ? 0 : hang), fnt->height + 1, col, page, false };
				if (first) { t.h += 0; first = false; }
				tlines.push_back(t);
				s += n;
				while (*s == ' ') s++;
				firstseg = false;
			}
			if (src.empty()) { TLine t = { fnt, "", 0, fnt->height / 2, col, page, false }; tlines.push_back(t); }
			if (le >= para.size()) break;
		}
		TLine sp = { fnt, "", 0, gap, col, page, false };
		tlines.push_back(sp);
	}
	for (size_t i = 0; i < tlines.size(); i++) ttotal += tlines[i].h;
}

static int text_line_at(int scroll) {
	int y = 0;
	for (size_t i = 0; i < tlines.size(); i++) { if (y + tlines[i].h > scroll) return (int)i; y += tlines[i].h; }
	return (int)tlines.size() - 1;
}
static int text_y_of(int line) {
	int y = 0;
	for (int i = 0; i < line && i < (int)tlines.size(); i++) y += tlines[i].h;
	return y;
}

static void clamp_text() {
	int view = 240 - TEXT_TOP;
	if (tscroll > ttotal - view + 20) tscroll = ttotal - view + 20;
	if (tscroll < 0) tscroll = 0;
}

static void draw_text(U3Canvas &c) {
	const Book &b = BOOKS[cur_book];
	c.fill(0, 24, 320, 26, C_PANEL);
	bbutton(c, 4, 27, 50, 20, B_BACK, "Back");
	bbutton(c, 58, 27, 74, 20, B_CONTENTS, show_toc ? "Text" : "Contents");
	bool has_scans = cur_book == 2 && avail[3];
	if (has_scans && !show_toc) bbutton(c, 136, 27, 74, 20, B_SCAN, "See page");
	c.text(font_small, has_scans ? 216 : 140, 31, b.title, C_DIM);
	if (show_toc) {
		c.fill(0, TEXT_TOP, 320, 240 - TEXT_TOP, C_BG);
		int y = TEXT_TOP + 4 - tscroll;
		for (size_t i = 0; i < toc.size(); i++, y += 22) {
			if (y < TEXT_TOP - 22 || y > 240) continue;
			c.clip(0, TEXT_TOP, 320, 240 - TEXT_TOP);
			bool d = down_id == (int)(B_TOC + i);
			c.fill_round(6, y, 308, 20, 4, d ? C_BTN_DOWN : C_BTN);
			c.text(font_ui, 14, y + 3, toc[i].first.c_str(), C_TEXT);
			c.noclip();
			if (y >= TEXT_TOP) hreg(6, y, 308, 20, B_TOC + (int)i);
		}
		return;
	}
	c.fill(0, TEXT_TOP, 320, 240 - TEXT_TOP, C_PAPER);
	c.clip(0, TEXT_TOP, 320, 240 - TEXT_TOP);
	int y = TEXT_TOP + 6 - tscroll;
	for (size_t i = 0; i < tlines.size(); i++) {
		const TLine &t = tlines[i];
		if (y + t.h >= TEXT_TOP && y < 240) {
			if (t.rule) {
				c.hline(10, y + 7, 300, 0xD6CBB2);
				int tw = u3_text_width(font_small, t.s.c_str());
				c.fill(160 - tw / 2 - 4, y, tw + 8, 14, C_PAPER);
				c.text_center(font_small, 160, y + 1, t.s.c_str(), 0xA89A7C);
			} else if (!t.s.empty()) c.text(*t.f, 10 + t.x, y, t.s.c_str(), t.col);
		}
		y += t.h;
		if (y > 240) break;
	}
	c.noclip();
	// scrollbar
	int view = 240 - TEXT_TOP;
	if (ttotal > view) {
		int sh = view * view / ttotal; if (sh < 14) sh = 14;
		int sy = TEXT_TOP + (view - sh) * tscroll / (ttotal - view + 20 > 0 ? ttotal - view + 20 : 1);
		c.fill(315, sy, 3, sh, 0xB8AC90);
	}
}

// =================================================================== scans
static std::vector<std::string> pages;
static int page_i = 0;
static uint8_t *img = 0;
static int iw = 0, ih = 0;
static float zoom = 1.0f;      // multiple of fit-width
static float ox = 0, oy = 0;   // image-space top-left of the view
static bool img_err = false;
enum { SCAN_TOP = 50, SCAN_H = 190 };

static void free_img() { if (img) stbi_image_free(img); img = 0; iw = ih = 0; }

static void load_page() {
	free_img();
	img_err = false;
	if (page_i < 0 || page_i >= (int)pages.size()) return;
	int n;
	img = stbi_load(pages[page_i].c_str(), &iw, &ih, &n, 3);
	if (!img) img_err = true;
	ox = 0; oy = 0;
}

static float scan_scale() { return iw ? (320.0f / iw) * zoom : 1.0f; }
static void clamp_scan() {
	float s = scan_scale();
	float vw = 320 / s, vh = SCAN_H / s;
	if (ox > iw - vw) ox = iw - vw;
	if (oy > ih - vh) oy = ih - vh;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
}
static void set_zoom(float z, float fx, float fy) {   // keep focus point (screen coords) stable
	float s0 = scan_scale();
	float ix = ox + fx / s0, iy = oy + (fy - SCAN_TOP) / s0;
	float maxz = iw ? (iw * 1.25f) / 320.0f : 4.0f;
	if (z < 1.0f) z = 1.0f;
	if (z > maxz) z = maxz;
	zoom = z;
	float s1 = scan_scale();
	ox = ix - fx / s1; oy = iy - (fy - SCAN_TOP) / s1;
	clamp_scan();
}

static void draw_scan(U3Canvas &c) {
	const Book &b = BOOKS[cur_book];
	c.fill(0, 24, 320, 26, C_PANEL);
	bbutton(c, 4, 27, 50, 20, B_BACK, "Back");
	bbutton(c, 58, 27, 28, 20, B_PREV, "<");
	char pg[32]; snprintf(pg, sizeof pg, "%d / %d", page_i + 1, (int)pages.size());
	c.text_center(font_small, 112, 31, pg, C_DIM);
	bbutton(c, 138, 27, 28, 20, B_NEXT, ">");
	bbutton(c, 236, 27, 38, 20, B_ZOOMOUT, "-");
	bbutton(c, 278, 27, 38, 20, B_ZOOMIN, "+");
	(void)b;
	c.fill(0, SCAN_TOP, 320, SCAN_H, 0x0A0B0E);
	if (img_err) { c.text_center(font_ui, 160, 130, "Couldn't open this page (out of memory?)", C_DIM); return; }
	if (!img) return;
	float s = scan_scale();
	float vw = 320 / s, vh = SCAN_H / s;
	int dh = (int)(ih * s) < SCAN_H ? (int)(ih * s) : SCAN_H;
	if (vh > ih) vh = (float)ih;
	c.clip(0, SCAN_TOP, 320, SCAN_H);
	c.blit_rgb888(img, iw, ih, ox, oy, vw, vh, 0, SCAN_TOP, 320, dh);
	c.noclip();
	if (zoom <= 1.01f) c.text(font_small, 6, 228, "+ / circle pad to zoom, drag to move", C_FAINT);
}

// =================================================================== shelf
static void draw_shelf(U3Canvas &c) {
	if (!avail_checked) check_avail();
	c.fill(0, 24, 320, 216, C_BG);
	int y = 28;
	for (int i = 0; i < NBOOKS; i++) {
		if (BOOKS[i].spoiler && !u3_settings.cluebook) continue;
		bool ok = avail[i];
		bool d = down_id == B_BOOK + i;
		c.fill_round(4, y, 312, 27, 5, d ? C_BTN_DOWN : (ok ? C_BTN : 0x1A1C22));
		c.text(font_bold, 12, y + 2, BOOKS[i].title, ok ? (BOOKS[i].spoiler ? 0xFF9C8A : C_TEXT) : C_FAINT);
		c.text(font_small, 12, y + 15, ok ? BOOKS[i].sub : "Not installed - see the guide", ok ? C_DIM : C_FAINT);
		if (ok) hreg(4, y, 312, 27, B_BOOK + i);
		y += 29;
	}
}

// =================================================================== public api
static void open_book(int i) {
	cur_book = i; show_toc = false;
	if (BOOKS[i].scans) { list_pages(BOOKS[i].path, pages); page_i = 0; zoom = 1.0f; load_page(); }
	else layout_text(BOOKS[i].path);
	dirty = true;
}
static void close_book() {
	free_img(); tlines.clear(); toc.clear(); cur_book = -1; dirty = true;
}

void u3_book_open_spells(char) {
	if (!avail_checked) check_avail();
	if (avail[1]) open_book(1);
}

void u3_book_draw(U3Canvas &c, int) {
	hits.clear();
	if (cur_book < 0) draw_shelf(c);
	else if (BOOKS[cur_book].scans) draw_scan(c);
	else draw_text(c);
	dirty = false;
}

bool u3_book_dirty() { return dirty; }

static void action(int id) {
	if (id >= B_BOOK && id < B_BOOK + NBOOKS) { open_book(id - B_BOOK); return; }
	if (id >= B_TOC && id < B_TOC + 500) {
		int k = id - B_TOC;
		if (k < (int)toc.size()) { tscroll = text_y_of(toc[k].second); clamp_text(); show_toc = false; }
		dirty = true; return;
	}
	switch (id) {
	case B_BACK: if (show_toc) show_toc = false; else close_book(); break;
	case B_PREV: if (page_i > 0) { page_i--; load_page(); } break;
	case B_NEXT: if (page_i + 1 < (int)pages.size()) { page_i++; load_page(); } break;
	case B_ZOOMIN: set_zoom(zoom * 1.5f, 160, SCAN_TOP + SCAN_H / 2); break;
	case B_ZOOMOUT: set_zoom(zoom / 1.5f, 160, SCAN_TOP + SCAN_H / 2); break;
	case B_CONTENTS: show_toc = !show_toc; tscroll = 0; if (!show_toc) tscroll = 0; break;
	case B_SCAN: {
		int line = text_line_at(tscroll);
		int pg = line >= 0 && line < (int)tlines.size() ? tlines[line].page : 1;
		open_book(3);
		// page files are p-01.jpg ... in order; manual page numbers map 1:1
		if (pg >= 1 && pg <= (int)pages.size()) { page_i = pg - 1; load_page(); }
		break;
	}
	}
	dirty = true;
}

void u3_book_touch(int x, int y, int phase) {
	if (phase == 0) {
		down_id = hfind(x, y);
		start_x = drag_x = x; start_y = drag_y = y; dragging = false;
		dirty = true;
	} else if (phase == 1) {
		int dx = x - drag_x, dy = y - drag_y;
		if (abs(x - start_x) > 5 || abs(y - start_y) > 5) {
			if (!dragging && down_id && (cur_book < 0 || show_toc || down_id >= B_TOC)) down_id = 0;
			dragging = true;
		}
		if (!dragging) { dirty = true; return; }   // ignore jitter until a real drag starts (drag_x/y stay at the start point)
		if (cur_book >= 0 && !BOOKS[cur_book].scans) { if (start_y >= TEXT_TOP) { tscroll -= dy; clamp_text(); } }
		else if (cur_book >= 0 && start_y >= SCAN_TOP && down_id == 0) {
			float s = scan_scale();
			ox -= dx / s; oy -= dy / s; clamp_scan();
		}
		drag_x = x; drag_y = y;
		dirty = true;
	} else {
		int id = hfind(x, y);
		if (id && id == down_id && !(dragging && id >= B_TOC)) action(id);
		else if (id && id == down_id && !dragging) action(id);
		down_id = -1; dragging = false;
		dirty = true;
	}
}

bool u3_book_button(uint32_t kd) {
	if (cur_book < 0) return false;
	if (BOOKS[cur_book].scans) {
		if (kd & KEY_B) { close_book(); return true; }
		if (kd & KEY_X) { action(B_NEXT); return true; }
		if (kd & KEY_Y) { action(B_PREV); return true; }
	} else {
		if (kd & KEY_B) { if (show_toc) show_toc = false; else close_book(); dirty = true; return true; }
	}
	return false;
}

void u3_book_tick() {
	if (cur_book < 0) return;
	circlePosition cp; hidCircleRead(&cp);
	if (abs(cp.dy) < 25 && abs(cp.dx) < 25) return;
	if (!BOOKS[cur_book].scans) { tscroll -= cp.dy / 6; clamp_text(); dirty = true; }
	else {
		if (abs(cp.dy) > 100 && abs(cp.dx) < 40) {
			// strong push up/down zooms
			set_zoom(zoom * (cp.dy > 0 ? 1.08f : 0.93f), 160, SCAN_TOP + SCAN_H / 2);
		} else {
			float s = scan_scale();
			ox += cp.dx / 10.0f / s * 4; oy -= cp.dy / 10.0f / s * 4; clamp_scan();
		}
		dirty = true;
	}
}
