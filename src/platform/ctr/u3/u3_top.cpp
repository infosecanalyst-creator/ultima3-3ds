// u3_top.cpp - top-screen info strips (40px either side of the 4:3 game image)
#include "u3_ui.h"
#include "u3_state.h"
#include "u3_gfx.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <3ds.h>

extern "C" {
	u32 *u3v_overlay_pixels(void);
	void u3v_overlay_commit(void);
	void u3v_overlay_enable(int on);
}

static U3Canvas ov;
static bool ov_init = false;
static uint32_t last_hash = 0xFFFFFFFF;

enum { STRIP = 40, RX = 360 };
static const uint32_t PANEL = 0x15171D, PANEL_EDGE = 0x2A2E38, LABEL = 0x8A8F9C, TEXT = 0xE8E4DA;

static void moon(U3Canvas &c, int cx, int cy, int r, int phase, uint32_t lit) {
	c.disc(cx, cy, r, 0x2A2D36);
	if (phase < 0) return;
	float k = cosf(phase * 3.14159265f / 4.0f);
	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++) {
			float u = (x + 0.5f) / r, v = (y + 0.5f) / r;
			if (u * u + v * v > 1.0f) continue;
			float ut = k * sqrtf(1.0f - v * v);
			bool on = phase <= 4 ? (u > ut) : (u < -ut);
			if (phase == 4) on = true;
			if (on) c.pixel(cx + x, cy + y, lit);
		}
	c.ring(cx, cy, r, 0x000000, 120);
}

static void wind_icon(U3Canvas &c, int cx, int cy, int wind) {
	c.ring(cx, cy, 11, 0x50566A);
	c.text_center(font_small, cx, cy - 11 - 1 - 9, "N", LABEL);
	if (wind <= 0) { c.disc(cx, cy, 2, 0xA0C8FF); return; }
	// wind named for where it comes FROM; arrow shows where it blows
	int dx = 0, dy = 0;
	switch (wind) { case 1: dy = 1; break; case 2: dy = -1; break; case 3: dx = -1; break; case 4: dx = 1; break; }
	int tx = cx + dx * 9, ty = cy + dy * 9, bx = cx - dx * 8, by = cy - dy * 8;
	c.line(bx, by, tx, ty, 0xA0C8FF); c.line(bx + dy, by + dx, tx + dy, ty + dx, 0xA0C8FF);
	int px = -dy, py = dx;
	c.tri(tx + dx * 2, ty + dy * 2, tx - dx * 4 + px * 4, ty - dy * 4 + py * 4, tx - dx * 4 - px * 4, ty - dy * 4 - py * 4, 0xA0C8FF);
}

static uint32_t hp_color(const U3Member &m) {
	if (m.status == 'D' || m.status == 'A') return 0x6A6A70;
	if (m.status == 'P') return 0xB46CFF;
	int pct = m.maxhp ? m.hp * 100 / m.maxhp : 0;
	if (pct > 50) return 0x5CCB6A;
	if (pct > 25) return 0xE8C547;
	return 0xE5534B;
}

// --- mark / card icons (14x14 box centered at cx,cy)
static void icon(U3Canvas &c, int which, int cx, int cy, bool have) {
	if (!have) { c.frame_round(cx - 8, cy - 8, 17, 17, 4, 0x3A3F4C); c.text_center(font_small, cx, cy - 6, "?", 0x3F4554); return; }
	c.fill_round(cx - 8, cy - 8, 17, 17, 4, 0x262A34);
	switch (which) {
	case 0: // Kings - crown
		c.fill(cx - 6, cy + 1, 13, 4, 0xF2C94C);
		c.tri(cx - 6, cy + 1, cx - 6, cy - 5, cx - 2, cy + 1, 0xF2C94C);
		c.tri(cx - 3, cy + 1, cx, cy - 6, cx + 3, cy + 1, 0xF2C94C);
		c.tri(cx + 2, cy + 1, cx + 6, cy - 5, cx + 6, cy + 1, 0xF2C94C);
		break;
	case 1: // Snake - S curve
		for (int t = 0; t < 40; t++) {
			float a = t / 40.0f;
			int x = cx + (int)(4.5f * sinf(a * 6.283f)), y = cy - 6 + (int)(a * 12);
			c.disc(x, y, 1, 0x6FCF6F);
		}
		break;
	case 2: // Fire - flame
		c.disc(cx, cy + 2, 4, 0xF06A2A);
		c.tri(cx - 4, cy + 1, cx, cy - 7, cx + 4, cy + 1, 0xF06A2A);
		c.disc(cx, cy + 3, 2, 0xFFD24A);
		break;
	case 3: // Force - lightning bolt
		c.tri(cx + 2, cy - 7, cx - 4, cy + 1, cx, cy + 1, 0x7FB8FF);
		c.tri(cx - 2, cy + 7, cx + 4, cy - 1, cx, cy - 1, 0x7FB8FF);
		break;
	case 4: // Death - skull
		c.disc(cx, cy - 1, 5, 0xE8E4DA);
		c.fill(cx - 3, cy + 3, 7, 3, 0xE8E4DA);
		c.disc(cx - 2, cy - 1, 1, 0x262A34); c.disc(cx + 2, cy - 1, 1, 0x262A34);
		break;
	case 5: // Sol - sun
		for (int k = 0; k < 8; k++) {
			float a = k * 0.785f;
			c.line(cx, cy, cx + (int)(7 * cosf(a)), cy + (int)(7 * sinf(a)), 0xFFC23A);
		}
		c.disc(cx, cy, 4, 0xFFD966);
		break;
	case 6: // Love - heart
		c.disc(cx - 3, cy - 2, 3, 0xF0709A); c.disc(cx + 3, cy - 2, 3, 0xF0709A);
		c.tri(cx - 6, cy - 1, cx + 6, cy - 1, cx, cy + 6, 0xF0709A);
		break;
	case 7: // Moons - crescent
		c.disc(cx, cy, 6, 0xD8DDE8);
		c.disc(cx + 3, cy - 2, 5, 0x262A34);
		break;
	}
}

static const char *MARK_NAMES[8] = { "Kings", "Snake", "Fire", "Force", "Death", "Sol", "Love", "Moons" };

void u3_top_update(bool force) {
	if (!ov_init) {
		u32 *p = u3v_overlay_pixels();
		if (!p) return;
		ov.init(p, 512, 256, U3_RGBA8);
		ov_init = true;
	}
	static uint32_t blink_t = 0;
	uint32_t now = u3_ticks_ms();
	bool low_food = false;
	int minfood = -1;
	for (int i = 0; i < 4; i++)
		if (u3s.m[i].present && u3s.m[i].status != 'D' && u3s.m[i].status != 'A')
			if (minfood < 0 || u3s.m[i].food < minfood) minfood = u3s.m[i].food;
	low_food = minfood >= 0 && minfood < 50;
	bool blink = low_food && ((now / 500) & 1);
	uint32_t h = u3s.changes * 31 + (blink ? 7 : 0) + (u3s.valid ? 1 : 0) + (u3_settings.strips ? 3 : 0);
	if (!force && h == last_hash && now - blink_t < 2000) return;
	last_hash = h; blink_t = now;

	if (!u3_settings.strips) {
		ov.clear(0, 0);
		u3v_overlay_commit();
		u3v_overlay_enable(0);
		return;
	}
	ov.clear(0, 0);
	// panels
	ov.fill(0, 0, STRIP, 240, PANEL);
	ov.fill(RX, 0, STRIP, 240, PANEL);
	ov.vline(STRIP - 1, 0, 240, PANEL_EDGE);
	ov.vline(RX, 0, 240, PANEL_EDGE);

	bool live = u3s.valid && u3s.nparty > 0;

	// ---- left: moons
	ov.text_center(font_small, 20, 2, "MOONS", LABEL);
	moon(ov, 11, 22, 7, live ? u3s.moon_t : -1, 0xC9D6EA);
	moon(ov, 29, 22, 7, live ? u3s.moon_f : -1, 0xF2B266);
	char b[16];
	if (live && u3s.moon_t >= 0) { snprintf(b, sizeof b, "%d", u3s.moon_t); ov.text_center(font_bold, 11, 31, b, TEXT); }
	if (live && u3s.moon_f >= 0) { snprintf(b, sizeof b, "%d", u3s.moon_f); ov.text_center(font_bold, 29, 31, b, TEXT); }

	// ---- wind (meaningful on the surface)
	ov.text_center(font_small, 20, 50, "WIND", LABEL);
	bool show_wind = live && (u3s.map == U3MAP_SOSARIA) && u3s.wind >= 0;
	wind_icon(ov, 20, 78, show_wind ? u3s.wind : 0);
	if (show_wind) {
		static const char *wn[5] = { "Calm", "North", "South", "East", "West" };
		ov.text_center(font_small, 20, 92, wn[u3s.wind], TEXT);
	}

	// ---- party HP bars
	ov.text_center(font_small, 20, 108, "PARTY", LABEL);
	const int by = 122, bh = 78, bw = 7;
	for (int i = 0; i < 4; i++) {
		int x = 3 + i * 9;
		ov.fill(x, by, bw, bh, 0x23262E);
		const U3Member &m = u3s.m[i];
		if (live && m.present) {
			uint32_t col = hp_color(m);
			int fh = m.maxhp ? (m.hp * bh) / m.maxhp : 0;
			if (fh > bh) fh = bh;
			if (m.status == 'D' || m.status == 'A') fh = bh;
			ov.fill(x, by + bh - fh, bw, fh, col, m.status == 'D' || m.status == 'A' ? 110 : 255);
			if (m.status == 'D' || m.status == 'A') { ov.line(x, by + bh / 2 - 3, x + bw - 1, by + bh / 2 + 3, 0xE8E4DA); }
		}
		snprintf(b, sizeof b, "%d", i + 1);
		ov.text_center(font_small, x + bw / 2, by + bh + 1, b, (live && m.present) ? TEXT : 0x4A4F5C);
	}

	// ---- food
	ov.text_center(font_small, 20, 214, "FOOD", LABEL);
	if (live && minfood >= 0) {
		snprintf(b, sizeof b, "%d", minfood);
		uint32_t col = minfood < 100 ? 0xF0A040 : TEXT;
		if (low_food) col = blink ? 0xFF5050 : 0x802020;
		ov.text_center(font_bold, 20, 225, b, col);
	}

	// ---- right: marks & cards
	ov.text_center(font_small, RX + 20, 2, "MARKS", LABEL);
	for (int i = 0; i < 4; i++) {
		bool have = live && (u3s.marks_all & (0x80 >> i));
		icon(ov, i, RX + 20, 22 + i * 22, have);
	}
	ov.text_center(font_small, RX + 20, 110, "CARDS", LABEL);
	for (int i = 0; i < 4; i++) {
		bool have = live && (u3s.marks_all & (0x08 >> i));
		icon(ov, 4 + i, RX + 20, 130 + i * 22, have);
	}
	(void)MARK_NAMES;

	// ---- dungeon level + facing
	if (live && u3s.map == U3MAP_DUNGEON) {
		static const char *fn[4] = { "N", "E", "S", "W" };
		snprintf(b, sizeof b, "L%d", u3s.dlevel + 1);
		ov.text_center(font_big, RX + 20, 210, b, 0x7FD4FF);
		snprintf(b, sizeof b, "face %s", fn[u3s.facing & 3]);
		ov.text_center(font_small, RX + 20, 228, b, LABEL);
	} else if (u3_turbo_active()) {
		ov.text_center(font_bold, RX + 20, 220, ">>", 0xFFE070);
	}
	if (u3_turbo_active() && live && u3s.map == U3MAP_DUNGEON) ov.text_center(font_small, RX + 20, 196, ">>", 0xFFE070);

	u3v_overlay_commit();
}
