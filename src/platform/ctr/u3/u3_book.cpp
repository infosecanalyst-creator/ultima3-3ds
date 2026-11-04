// u3_book.cpp - built-in manual: text books (reflowed) and page scans (pan/zoom).
// Controls follow the standard shared by all of Ryan's 3DS ports:
//   Circle Pad and C-Stick scroll (hold to keep scrolling) and never move the game,
//   D-Pad moves the focus between the section buttons (A opens the focused one),
//   L/R go to the previous/next section, tapping a section opens it,
//   B or START closes the manual.
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
#include <math.h>

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
	{ "Reference card", "All 26 commands, A to Z", U3_BOOK_DIR "/refcard.txt", false, false },
	{ "Reference card - pages", "Your scan of the original card", U3_BOOK_DIR "/refcard", true, false },
	{ "Book of Play", "The original manual, as large text", U3_BOOK_DIR "/manual.txt", false, false },
	{ "Book of Play - pages", "The original manual's pages", U3_BOOK_DIR "/manual", true, false },
	{ "Wizard spellbook", "Original pages", U3_BOOK_DIR "/wizard", true, false },
	{ "Cleric spellbook", "Original pages", U3_BOOK_DIR "/cleric", true, false },
	{ "Cloth map of Sosaria", "The map from the box", U3_BOOK_DIR "/map", true, false },
	{ "Cluebook", "Spoilers! Hints and full solutions", U3_BOOK_DIR "/cluebook", true, true },
};
enum { NBOOKS = sizeof(BOOKS) / sizeof(BOOKS[0]) };
enum { BK_SPELLS = 1, BK_REFCARD_PAGES = 3, BK_PLAY = 4, BK_PLAY_PAGES = 5 };

static int cur_book = -1;     // -1 = shelf
static bool dirty = true;
static bool avail[NBOOKS];
static bool avail_checked = false;

// ---- layout
enum {
	TOOL_Y = 24, TOOL_H = 26, BODY_Y = 50,
	RAIL_W = 94, RAIL_BTN_H = 21, RAIL_STEP = 23, RAIL_ROWS = 8,
	VIEW_X = 96, VIEW_W = 320 - 96, VIEW_H = 240 - 50,
	TEXT_X = VIEW_X + 6, TEXT_W = VIEW_W - 6 - 10,   // room for the scrollbar
	SHELF_ROWS_MAX = 10,
};

// ---- hits and press feedback (ids offset so they never collide with the tab bar's)
struct Hit { int x, y, w, h, id; };
static std::vector<Hit> hits;
static int down_id = 0;
static int drag_x, drag_y, start_x, start_y;
static bool dragging = false;
enum { FX_BASE = 20000 };
enum { B_SHELF = 1, B_ZOOMIN, B_ZOOMOUT, B_SCAN, B_BOOK = 100, B_SEC = 1000 };

static void hreg(int x, int y, int w, int h, int id) { Hit t = { x, y, w, h, id }; hits.push_back(t); }
static int hfind(int x, int y) {
	for (int i = (int)hits.size() - 1; i >= 0; i--)
		if (x >= hits[i].x && y >= hits[i].y && x < hits[i].x + hits[i].w && y < hits[i].y + hits[i].h) return hits[i].id;
	return 0;
}
static int look_of(int id) { return u3_press_look(FX_BASE + id, down_id == id && !dragging); }

// Text that would not fit is shortened with "..." (never cut off mid-glyph).
static std::string fit(const u3_font &f, const std::string &s, int maxw) {
	if (u3_text_width(f, s.c_str()) <= maxw) return s;
	int dots = u3_text_width(f, "...");
	std::string t = s;
	while (!t.empty() && u3_text_width(f, t.c_str()) + dots > maxw) t.erase(t.size() - 1);
	while (!t.empty() && t[t.size() - 1] == ' ') t.erase(t.size() - 1);
	return t + "...";
}

static void bbutton(U3Canvas &c, int x, int y, int w, int h, int id, const char *lbl, uint32_t bg = C_BTN, uint32_t fg = C_TEXT) {
	int off = u3_btn_bg(c, x, y, w, h, 5, bg, look_of(id));
	std::string t = fit(font_ui, lbl, w - 6);
	int tw = u3_text_width(font_ui, t.c_str());
	c.text(font_ui, x + (w - tw) / 2, y + (h - font_ui.height) / 2 + off, t.c_str(), fg);
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

static bool on_shelf(int i) {
	if (BOOKS[i].spoiler && !u3_settings.cluebook) return false;
	if (i == BK_REFCARD_PAGES && !avail[i]) return false;   // only listed if a scan was added
	return true;
}

// =================================================================== sections
struct Sec { std::string label; int target; int level; };   // target: text line, or page index
static std::vector<Sec> secs;
static int focus = 0;        // section (or shelf row) with the D-Pad focus
static int rail_top = 0;     // first section shown in the rail

// =================================================================== text books
struct TLine { const u3_font *f; std::string s; int x; int h; uint32_t col; int page; bool rule; };
static std::vector<TLine> tlines;
static std::vector<std::pair<std::string, int> > toc;   // heading -> line index (filled by layout_text)
static int tscroll = 0;       // pixels
static int ttotal = 0;

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
	std::string pending;   // text that followed a heading line in the same paragraph
	while (pos < all.size() || !pending.empty()) {
		std::string para;
		if (!pending.empty()) { para.swap(pending); }
		else {
			size_t end = all.find("\n\n", pos);
			if (end == std::string::npos) end = all.size();
			para = all.substr(pos, end - pos);
			pos = end + 2;
		}
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
		// a "#" heading is always one line; anything after it is its own paragraph
		if (para[0] == '#') {
			size_t nl = para.find('\n');
			if (nl != std::string::npos) { pending = para.substr(nl + 1); para.erase(nl); }
		}
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
	int maxs = ttotal - VIEW_H + 20;
	if (tscroll > maxs) tscroll = maxs;
	if (tscroll < 0) tscroll = 0;
}

// =================================================================== scans
static std::vector<std::string> pages;
static int page_i = 0;
static uint8_t *img = 0;
static int iw = 0, ih = 0;
static float zoom = 1.0f;      // multiple of fit-width
static float ox = 0, oy = 0;   // image-space top-left of the view
static bool img_err = false;

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
static float scan_scale() { return iw ? ((float)VIEW_W / iw) * zoom : 1.0f; }
static void clamp_scan() {
	float s = scan_scale();
	float vw = VIEW_W / s, vh = VIEW_H / s;
	if (ox > iw - vw) ox = iw - vw;
	if (oy > ih - vh) oy = ih - vh;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
}
static void set_zoom(float z) {   // keeps the middle of the view where it is
	float fx = VIEW_W / 2.0f, fy = VIEW_H / 2.0f;
	float s0 = scan_scale();
	float ix = ox + fx / s0, iy = oy + fy / s0;
	float maxz = iw ? (iw * 1.25f) / VIEW_W : 4.0f;
	if (z < 1.0f) z = 1.0f;
	if (z > maxz) z = maxz;
	zoom = z;
	float s1 = scan_scale();
	ox = ix - fx / s1; oy = iy - fy / s1;
	clamp_scan();
}

// =================================================================== section helpers
static bool is_scans() { return cur_book >= 0 && BOOKS[cur_book].scans; }

static void build_sections() {
	secs.clear();
	if (cur_book < 0) return;
	if (is_scans()) {
		for (int i = 0; i < (int)pages.size(); i++) {
			char b[24];
			if (pages.size() == 1) snprintf(b, sizeof b, "Whole page");
			else snprintf(b, sizeof b, "Page %d", i + 1);
			Sec s = { b, i, 1 };
			secs.push_back(s);
		}
		return;
	}
	if (toc.empty() || toc[0].second > 2) { Sec s = { "Start", 0, 1 }; secs.push_back(s); }
	for (size_t i = 0; i < toc.size(); i++) {
		const TLine &t = tlines[toc[i].second];
		int level = (t.f == &font_big) ? 1 : 2;
		Sec s = { toc[i].first, toc[i].second, level };
		secs.push_back(s);
	}
	// If no heading is bigger than another, show them all at the same level.
	bool any1 = false;
	for (size_t i = 0; i < secs.size(); i++) if (secs[i].level == 1 && secs[i].label != "Start") any1 = true;
	if (!any1) for (size_t i = 0; i < secs.size(); i++) secs[i].level = 1;
}

static int current_section() {
	if (secs.empty()) return 0;
	if (is_scans()) return page_i;
	int cur = 0;
	for (int i = 0; i < (int)secs.size(); i++)
		if (text_y_of(secs[i].target) <= tscroll + 4) cur = i;
	return cur;
}

static void keep_focus_visible() {
	int n = cur_book < 0 ? 0 : (int)secs.size();
	if (n == 0) { rail_top = 0; return; }
	if (focus < 0) focus = 0;
	if (focus >= n) focus = n - 1;
	if (focus < rail_top) rail_top = focus;
	if (focus >= rail_top + RAIL_ROWS) rail_top = focus - RAIL_ROWS + 1;
	if (rail_top > n - RAIL_ROWS) rail_top = n - RAIL_ROWS;
	if (rail_top < 0) rail_top = 0;
}

static void go_section(int i) {
	if (secs.empty()) return;
	if (i < 0) i = 0;
	if (i >= (int)secs.size()) i = (int)secs.size() - 1;
	if (is_scans()) { if (i != page_i) { page_i = i; zoom = 1.0f; load_page(); } }
	else { tscroll = text_y_of(secs[i].target); clamp_text(); }
	focus = i;
	keep_focus_visible();
	dirty = true;
}

// =================================================================== drawing
static void draw_rail(U3Canvas &c) {
	c.fill(0, BODY_Y, RAIL_W, VIEW_H, C_PANEL);
	int cur = current_section();
	int n = (int)secs.size();
	for (int r = 0; r < RAIL_ROWS && rail_top + r < n; r++) {
		int i = rail_top + r;
		int y = BODY_Y + 2 + r * RAIL_STEP;
		int id = B_SEC + i;
		bool here = i == cur;
		int off = u3_btn_bg(c, 2, y, RAIL_W - 4, RAIL_BTN_H, 4, here ? C_ACCENT_BG : C_BTN, look_of(id));
		int indent = secs[i].level == 2 ? 6 : 0;
		std::string t = fit(font_small, secs[i].label, RAIL_W - 14 - indent);
		c.text(font_small, 7 + indent, y + (RAIL_BTN_H - font_small.height) / 2 + off, t.c_str(), here ? C_ACCENT : C_TEXT);
		if (i == focus) { c.frame_round(2, y, RAIL_W - 4, RAIL_BTN_H, 4, C_ACCENT); c.frame_round(3, y + 1, RAIL_W - 6, RAIL_BTN_H - 2, 3, C_ACCENT, 140); }
		hreg(2, y, RAIL_W - 4, RAIL_BTN_H, id);
	}
	// more above / below
	if (rail_top > 0) c.tri(RAIL_W / 2 - 4, BODY_Y + 1, RAIL_W / 2 + 4, BODY_Y + 1, RAIL_W / 2, BODY_Y - 3 + 1, C_DIM);
	if (rail_top + RAIL_ROWS < n) {
		int yb = 238;
		c.tri(RAIL_W / 2 - 4, yb - 3, RAIL_W / 2 + 4, yb - 3, RAIL_W / 2, yb + 1, C_DIM);
	}
}

static void draw_toolbar(U3Canvas &c, bool scans) {
	const Book &b = BOOKS[cur_book];
	c.fill(0, TOOL_Y, 320, TOOL_H, C_PANEL);
	bbutton(c, 3, TOOL_Y + 3, 60, 20, B_SHELF, "Books");
	int right = 317;
	if (scans) {
		bbutton(c, right - 34, TOOL_Y + 3, 34, 20, B_ZOOMIN, "+");
		bbutton(c, right - 72, TOOL_Y + 3, 34, 20, B_ZOOMOUT, "-");
		right -= 76;
	} else {
		bool has_scans = (cur_book == BK_PLAY && avail[BK_PLAY_PAGES]) || (cur_book == BK_REFCARD_PAGES - 1 && avail[BK_REFCARD_PAGES]);
		if (has_scans) { bbutton(c, right - 74, TOOL_Y + 3, 74, 20, B_SCAN, "See page"); right -= 78; }
	}
	std::string t = fit(font_bold, b.title, right - 70);
	c.text(font_bold, 69, TOOL_Y + (TOOL_H - font_bold.height) / 2, t.c_str(), C_DIM);
}

static void draw_text(U3Canvas &c) {
	draw_toolbar(c, false);
	draw_rail(c);
	c.fill(VIEW_X, BODY_Y, VIEW_W, VIEW_H, C_PAPER);
	c.clip(VIEW_X, BODY_Y, VIEW_W, VIEW_H);
	int y = BODY_Y + 6 - tscroll;
	for (size_t i = 0; i < tlines.size(); i++) {
		const TLine &t = tlines[i];
		if (y + t.h >= BODY_Y && y < 240) {
			if (t.rule) {
				int mid = VIEW_X + VIEW_W / 2;
				c.hline(VIEW_X + 8, y + 7, VIEW_W - 16, 0xD6CBB2);
				int tw = u3_text_width(font_small, t.s.c_str());
				c.fill(mid - tw / 2 - 4, y, tw + 8, 14, C_PAPER);
				c.text_center(font_small, mid, y + 1, t.s.c_str(), 0xA89A7C);
			} else if (!t.s.empty()) c.text(*t.f, TEXT_X + t.x, y, t.s.c_str(), t.col);
		}
		y += t.h;
		if (y > 240) break;
	}
	c.noclip();
	if (ttotal > VIEW_H) {
		int maxs = ttotal - VIEW_H + 20;
		int sh = VIEW_H * VIEW_H / ttotal; if (sh < 14) sh = 14;
		int sy = BODY_Y + (VIEW_H - sh) * tscroll / (maxs > 0 ? maxs : 1);
		c.fill(315, sy, 3, sh, 0xB8AC90);
	}
}

static void draw_scan(U3Canvas &c) {
	draw_toolbar(c, true);
	draw_rail(c);
	c.fill(VIEW_X, BODY_Y, VIEW_W, VIEW_H, 0x0A0B0E);
	if (img_err) {
		c.text_center(font_ui, VIEW_X + VIEW_W / 2, 120, "Couldn't open this page", C_DIM);
		c.text_center(font_small, VIEW_X + VIEW_W / 2, 140, "(not enough memory?)", C_FAINT);
		return;
	}
	if (!img) return;
	float s = scan_scale();
	float vw = VIEW_W / s, vh = VIEW_H / s;
	int dh = (int)(ih * s) < VIEW_H ? (int)(ih * s) : VIEW_H;
	if (vh > ih) vh = (float)ih;
	c.clip(VIEW_X, BODY_Y, VIEW_W, VIEW_H);
	c.blit_rgb888(img, iw, ih, ox, oy, vw, vh, VIEW_X, BODY_Y, VIEW_W, dh);
	c.noclip();
}

static int shelf_rows(int *ids) {
	int n = 0;
	for (int i = 0; i < NBOOKS; i++) if (on_shelf(i)) ids[n++] = i;
	return n;
}

static void draw_shelf(U3Canvas &c) {
	if (!avail_checked) check_avail();
	c.fill(0, TOOL_Y, 320, 240 - TOOL_Y, C_BG);
	int ids[NBOOKS];
	int n = shelf_rows(ids);
	if (focus >= n) focus = n - 1;
	if (focus < 0) focus = 0;
	int step = n > 0 ? 213 / n : 29;
	if (step > 29) step = 29;
	int h = step - 2;
	bool two_lines = h >= 26;
	int y = TOOL_Y + 3;
	for (int k = 0; k < n; k++) {
		int i = ids[k];
		bool ok = avail[i];
		int off = ok ? u3_btn_bg(c, 4, y, 312, h, 5, C_BTN, look_of(B_BOOK + i)) : (c.fill_round(4, y, 312, h, 5, 0x1A1C22), 0);
		uint32_t tc = ok ? (BOOKS[i].spoiler ? 0xFF9C8A : C_TEXT) : C_FAINT;
		const char *sub = ok ? BOOKS[i].sub : "Not installed";
		std::string title = fit(font_bold, BOOKS[i].title, 296);
		if (two_lines) {
			c.text(font_bold, 12, y + 2 + off, title.c_str(), tc);
			std::string st = fit(font_small, sub, 296);
			c.text(font_small, 12, y + 15 + off, st.c_str(), ok ? C_DIM : C_FAINT);
		} else {
			int ty = y + (h - font_bold.height) / 2 + off;
			int tw = c.text(font_bold, 12, ty, title.c_str(), tc);
			std::string st = fit(font_small, sub, 300 - (12 + tw + 10));
			c.text(font_small, 12 + tw + 10, ty + 2, st.c_str(), ok ? C_DIM : C_FAINT);
		}
		if (k == focus) { c.frame_round(4, y, 312, h, 5, C_ACCENT); c.frame_round(5, y + 1, 310, h - 2, 4, C_ACCENT, 140); }
		if (ok) hreg(4, y, 312, h, B_BOOK + i);   // missing books are greyed and do nothing
		y += step;
	}
}

// =================================================================== public api
static void open_book(int i) {
	cur_book = i;
	if (BOOKS[i].scans) { list_pages(BOOKS[i].path, pages); page_i = 0; zoom = 1.0f; load_page(); }
	else layout_text(BOOKS[i].path);
	build_sections();
	focus = 0; rail_top = 0;
	dirty = true;
}
static void to_shelf() {
	int was = cur_book;
	free_img(); tlines.clear(); toc.clear(); secs.clear(); cur_book = -1;
	// put the shelf focus on the book we just left
	int ids[NBOOKS]; int n = shelf_rows(ids);
	focus = 0;
	for (int k = 0; k < n; k++) if (ids[k] == was) focus = k;
	dirty = true;
}

void u3_book_open_spells(char) {
	if (!avail_checked) check_avail();
	if (avail[BK_SPELLS] && cur_book != BK_SPELLS) open_book(BK_SPELLS);
}

void u3_book_draw(U3Canvas &c, int) {
	hits.clear();
	if (cur_book < 0) draw_shelf(c);
	else if (is_scans()) draw_scan(c);
	else draw_text(c);
	dirty = false;
}

bool u3_book_dirty() { return dirty || u3_press_animating(); }

static void action(int id) {
	if (id >= B_BOOK && id < B_BOOK + NBOOKS) { open_book(id - B_BOOK); return; }
	if (id >= B_SEC) { go_section(id - B_SEC); return; }
	switch (id) {
	case B_SHELF: to_shelf(); break;
	case B_ZOOMIN: set_zoom(zoom * 1.5f); break;
	case B_ZOOMOUT: set_zoom(zoom / 1.5f); break;
	case B_SCAN: {
		int line = text_line_at(tscroll);
		int pg = line >= 0 && line < (int)tlines.size() ? tlines[line].page : 1;
		int target = cur_book == BK_PLAY ? BK_PLAY_PAGES : BK_REFCARD_PAGES;
		open_book(target);
		// page files are p-01.jpg ... in order; manual page numbers map 1:1
		if (target == BK_PLAY_PAGES && pg >= 1 && pg <= (int)pages.size()) go_section(pg - 1);
		break;
	}
	}
	dirty = true;
}

void u3_book_touch(int x, int y, int phase) {
	if (phase == 0) {
		down_id = hfind(x, y);
		start_x = drag_x = x; start_y = drag_y = y; dragging = false;
		if (down_id) u3_press_down(FX_BASE + down_id);
		dirty = true;
	} else if (phase == 1) {
		int dx = x - drag_x, dy = y - drag_y;
		if (!dragging && (abs(x - start_x) > 5 || abs(y - start_y) > 5)) dragging = true;
		if (!dragging) return;   // ignore jitter until a real drag starts
		bool in_view = start_x >= VIEW_X && start_y >= BODY_Y;
		if (cur_book >= 0 && in_view && !is_scans()) { tscroll -= dy; clamp_text(); focus = current_section(); keep_focus_visible(); }
		else if (cur_book >= 0 && in_view && is_scans()) { float s = scan_scale(); ox -= dx / s; oy -= dy / s; clamp_scan(); }
		drag_x = x; drag_y = y;
		dirty = true;
	} else {
		int id = hfind(x, y);
		if (id && id == down_id && !dragging) { u3_press_up(FX_BASE + id); action(id); }
		down_id = 0; dragging = false;
		dirty = true;
	}
}

// D-Pad up/down with key repeat (first move at once, then after 350 ms every 90 ms)
static int dpad_step(uint32_t kd, uint32_t kh) {
	static uint32_t t0 = 0, last = 0;
	uint32_t now = u3_ticks_ms();
	int dir = (kh & KEY_DUP) ? -1 : (kh & KEY_DDOWN) ? 1 : 0;
	if (kd & (KEY_DUP | KEY_DDOWN)) { t0 = last = now; return dir; }
	if (!dir) return 0;
	if (now - t0 >= 350 && now - last >= 90) { last = now; return dir; }
	return 0;
}

int u3_book_buttons(uint32_t kd, uint32_t kh) {
	if (kd & (KEY_B | KEY_START)) return 2;   // close the manual
	int used = kd ? 1 : 0;
	int step = dpad_step(kd, kh);
	if (cur_book < 0) {
		int ids[NBOOKS]; int n = shelf_rows(ids);
		if (step) { focus += step; if (focus < 0) focus = 0; if (focus >= n) focus = n - 1; dirty = true; used = 1; }
		if (kd & KEY_L) { if (focus > 0) focus--; dirty = true; }
		if (kd & KEY_R) { if (focus < n - 1) focus++; dirty = true; }
		if ((kd & KEY_A) && focus >= 0 && focus < n && avail[ids[focus]]) open_book(ids[focus]);
		return used;
	}
	int n = (int)secs.size();
	if (step && n) { focus += step; keep_focus_visible(); dirty = true; used = 1; }
	if (kd & KEY_A) go_section(focus);
	if (kd & KEY_L) go_section(current_section() - 1);
	if (kd & KEY_R) go_section(current_section() + 1);
	if (is_scans()) {
		if (kd & KEY_X) set_zoom(zoom * 1.5f);
		if (kd & KEY_Y) set_zoom(zoom / 1.5f);
	}
	if (kd) dirty = true;
	return used;
}

// Circle Pad and C-Stick scroll; holding keeps scrolling, faster the further it's pushed.
static float stick_speed(int v) {
	int a = v < 0 ? -v : v;
	if (a < 20) return 0;
	float d = (float)(a - 20);
	float sp = d * sqrtf(d) / 60.0f;
	return v < 0 ? -sp : sp;
}

void u3_book_tick() {
	if (cur_book < 0) return;
	circlePosition cp, cs;
	hidCircleRead(&cp);
	hidCstickRead(&cs);
	float vy = stick_speed(cp.dy) + stick_speed(cs.dy);
	float vx = stick_speed(cp.dx) + stick_speed(cs.dx);
	if (vx == 0 && vy == 0) return;
	if (!is_scans()) {
		int before = tscroll;
		tscroll -= (int)vy;
		clamp_text();
		if (tscroll != before) { focus = current_section(); keep_focus_visible(); dirty = true; }
	} else {
		float s = scan_scale();
		ox += vx / s; oy -= vy / s;
		clamp_scan();
		dirty = true;
	}
}
