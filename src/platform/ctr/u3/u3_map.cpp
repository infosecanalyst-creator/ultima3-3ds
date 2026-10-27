// u3_map.cpp - explored-area tracking and map drawing (see u3_map.h)
#include "u3_map.h"
#include "u3_state.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

U3Area *u3_areas = 0;
uint32_t u3_map_changes = 0;

static uint32_t tile_avg[128];
static uint16_t tile_mini8[128][64];   // 8x8 RGB565 downsample of each 16x16 tile
static bool shapes_ok = false;
static char save_file[256];
static bool dirty = false;
static uint32_t last_save = 0;
extern uint32_t u3_ticks_ms();

// facing byte -> (dx,dy). Order from the disassembly notes: 0 north, 1 east, 2 south, 3 west.
static const int DIRX[4] = { 0, 1, 0, -1 };
static const int DIRY[4] = { -1, 0, 1, 0 };

static inline bool getbit(const uint8_t *b, int i) { return (b[i >> 3] >> (i & 7)) & 1; }
static inline void setbit(uint8_t *b, int i) { b[i >> 3] |= (uint8_t)(1 << (i & 7)); }

uint32_t u3_tile_color(int t) { return tile_avg[t & 127]; }

bool u3_map_init(const char *shapes_path, const char *pal_path, const char *save_path) {
	if (!u3_areas) u3_areas = (U3Area *)calloc(U3_AREAS, sizeof(U3Area));
	snprintf(save_file, sizeof save_file, "%s", save_path);
	// palette
	uint8_t pal[768];
	FILE *f = fopen(pal_path, "rb");
	bool pal_ok = f && fread(pal, 1, 768, f) == 768;
	if (f) fclose(f);
	if (pal_ok) {
		int mx = 0;
		for (int i = 0; i < 768; i++) if (pal[i] > mx) mx = pal[i];
		if (mx < 64) for (int i = 0; i < 768; i++) pal[i] = (uint8_t)(pal[i] * 255 / 63);
	}
	static uint8_t shapes[128 * 256];
	memset(shapes, 0, sizeof shapes);
	f = fopen(shapes_path, "rb");
	size_t n = f ? fread(shapes, 1, sizeof shapes, f) : 0;
	if (f) fclose(f);
	shapes_ok = pal_ok && n >= 64 * 256;
	for (int t = 0; t < 128; t++) {
		uint32_t r = 0, g = 0, b = 0;
		for (int i = 0; i < 256; i++) {
			const uint8_t *p = pal + shapes[t * 256 + i] * 3;
			r += p[0]; g += p[1]; b += p[2];
		}
		tile_avg[t] = shapes_ok ? (((r / 256) << 16) | ((g / 256) << 8) | (b / 256)) : 0x404040;
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++) {
				uint32_t rr = 0, gg = 0, bb = 0;
				for (int k = 0; k < 4; k++) {
					int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
					const uint8_t *p = pal + shapes[t * 256 + sy * 16 + sx] * 3;
					rr += p[0]; gg += p[1]; bb += p[2];
				}
				rr /= 4; gg /= 4; bb /= 4;
				tile_mini8[t][y * 8 + x] = (uint16_t)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
			}
	}
	// saved exploration
	f = fopen(save_file, "rb");
	if (f) {
		char magic[8];
		if (fread(magic, 1, 8, f) == 8 && !memcmp(magic, "U3MAP001", 8))
			fread(u3_areas, sizeof(U3Area), U3_AREAS, f);
		fclose(f);
	}
	return shapes_ok;
}

void u3_map_save(bool force) {
	if (!u3_areas || !dirty) return;
	uint32_t now = u3_ticks_ms();
	if (!force && now - last_save < 20000) return;
	FILE *f = fopen(save_file, "wb");
	if (!f) return;
	fwrite("U3MAP001", 1, 8, f);
	fwrite(u3_areas, sizeof(U3Area), U3_AREAS, f);
	fclose(f);
	dirty = false;
	last_save = now;
}

void u3_map_reset() {
	if (!u3_areas) return;
	memset(u3_areas, 0, sizeof(U3Area) * U3_AREAS);
	dirty = true;
	u3_map_changes++;
	u3_map_save(true);
}

int u3_map_current_area() {
	if (!u3s.valid) return -1;
	if (u3s.map == U3MAP_SOSARIA) return 0;
	if ((u3s.map == U3MAP_TOWN || u3s.map == U3MAP_CASTLE || u3s.map == U3MAP_DUNGEON) && u3s.place >= 0)
		return u3s.place + 1;
	return -1;
}

bool u3_area_known(int a) {
	if (!u3_areas || a < 0 || a >= U3_AREAS) return false;
	for (int i = 0; i < 512; i++) if (u3_areas[a].seen[i]) return true;
	return false;
}

bool u3_dungeon_level_known(int a, int level) {
	if (!u3_areas || a < 0 || a >= U3_AREAS) return false;
	for (int i = level * 32; i < level * 32 + 32; i++) if (u3_areas[a].seen[i]) return true;
	return false;
}

static void reveal(U3Area &A, int idx, uint8_t byte) {
	bool changed = false;
	if (!getbit(A.seen, idx)) { setbit(A.seen, idx); changed = true; }
	if (A.tiles[idx] != byte) { A.tiles[idx] = byte; changed = true; }
	if (changed) { dirty = true; u3_map_changes++; }
}

void u3_map_update() {
	if (!u3_areas || !u3s.valid || u3s.nparty == 0) return;
	int a = u3_map_current_area();
	if (a < 0) return;
	U3Area &A = u3_areas[a];
	if (u3s.map == U3MAP_DUNGEON) {
		int L = u3s.dlevel, x = u3s.x & 15, y = u3s.y & 15;
		if (u3s.x > 15 || u3s.y > 15) return;
		int f = u3s.facing & 3;
		int base = L * 256;
		int here = base + y * 16 + x;
		if (!getbit(A.visited, here)) { setbit(A.visited, here); dirty = true; u3_map_changes++; }
		reveal(A, here, u3_map_byte(here));
		// neighbors are always visible
		for (int d = 0; d < 4; d++) {
			int nx = (x + DIRX[d]) & 15, ny = (y + DIRY[d]) & 15;
			int i = base + ny * 16 + nx;
			reveal(A, i, u3_map_byte(i));
		}
		// line of sight down the corridor (the 3-D view shows ~4 cells deep)
		int lx = DIRY[f] ? -DIRY[f] : 0, ly = DIRX[f];  // left-hand perpendicular
		for (int d = 1; d <= 4; d++) {
			int cx = (x + DIRX[f] * d) & 15, cy = (y + DIRY[f] * d) & 15;
			int i = base + cy * 16 + cx;
			uint8_t b = u3_map_byte(i);
			reveal(A, i, b);
			if (b & 0x80) break;   // wall or door blocks the view
			for (int s = -1; s <= 1; s += 2) {
				int sx = (cx + lx * s) & 15, sy = (cy + ly * s) & 15;
				int j = base + sy * 16 + sx;
				reveal(A, j, u3_map_byte(j));
			}
		}
	} else if (u3s.map == U3MAP_SOSARIA || u3s.map == U3MAP_TOWN || u3s.map == U3MAP_CASTLE) {
		bool wrap = u3s.map == U3MAP_SOSARIA;
		int x = u3s.x, y = u3s.y;
		if (x > 63 || y > 63) return;
		int here = y * 64 + x;
		if (!getbit(A.visited, here)) { setbit(A.visited, here); dirty = true; }
		for (int dy = -5; dy <= 5; dy++)
			for (int dx = -5; dx <= 5; dx++) {
				int cx = x + dx, cy = y + dy;
				if (wrap) { cx &= 63; cy &= 63; }
				else if (cx < 0 || cy < 0 || cx > 63 || cy > 63) continue;
				int i = cy * 64 + cx;
				reveal(A, i, u3_map_byte(i));
			}
	}
	u3_map_save(false);
}

// ---------------------------------------------------------------- drawing
void u3_draw_area(U3Canvas &c, int a, int x, int y, int w, int h, float cx, float cy, int ts, bool show_party) {
	c.clip(x, y, w, h);
	c.fill(x, y, w, h, 0x101418);
	if (!u3_areas || a < 0) { c.noclip(); return; }
	U3Area &A = u3_areas[a];
	bool wrap = a == 0;
	int cols = w / ts + 2, rows = h / ts + 2;
	float ox = cx - (float)w / ts / 2, oy = cy - (float)h / ts / 2;
	int tx0 = (int)(ox < 0 ? ox - 1 : ox), ty0 = (int)(oy < 0 ? oy - 1 : oy);
	int px0 = x + (int)((tx0 - ox) * ts), py0 = y + (int)((ty0 - oy) * ts);
	for (int j = 0; j < rows; j++)
		for (int i = 0; i < cols; i++) {
			int mx = tx0 + i, my = ty0 + j;
			if (wrap) { mx &= 63; my &= 63; }
			else if (mx < 0 || my < 0 || mx > 63 || my > 63) continue;
			int idx = my * 64 + mx;
			int sx = px0 + i * ts, sy = py0 + j * ts;
			if (!getbit(A.seen, idx)) continue;
			int t = (A.tiles[idx] >> 2) & 63;
			if (ts >= 8 && shapes_ok) {
				// mini tile art (8x8, scaled by ts/8)
				for (int yy = 0; yy < ts; yy++)
					for (int xx = 0; xx < ts; xx++) {
						uint16_t p = tile_mini8[t][(yy * 8 / ts) * 8 + (xx * 8 / ts)];
						uint32_t rgb = (((p >> 11) & 31) << 19) | (((p >> 5) & 63) << 10) | ((p & 31) << 3);
						c.pixel(sx + xx, sy + yy, rgb);
					}
			} else {
				c.fill(sx, sy, ts, ts, tile_avg[t]);
			}
		}
	if (show_party && u3_map_current_area() == a) {
		float px = u3s.x - ox, py = u3s.y - oy;
		if (wrap) {
			while (px < 0) px += 64; while (px >= 64) px -= 64;
			while (py < 0) py += 64; while (py >= 64) py -= 64;
		}
		int pcx = x + (int)(px * ts) + ts / 2, pcy = y + (int)(py * ts) + ts / 2;
		int r = ts < 6 ? 4 : ts / 2 + 2;
		c.ring(pcx, pcy, r + 1, 0x000000, 200);
		c.ring(pcx, pcy, r, 0xFFE070);
		c.disc(pcx, pcy, ts < 6 ? 1 : 2, 0xFFE070);
	}
	c.noclip();
}

static void draw_arrow(U3Canvas &c, int cx, int cy, int s, int dir, uint32_t col) {
	// dir: 0 up, 1 right, 2 down, 3 left
	int h = s / 2;
	switch (dir & 3) {
	case 0: c.tri(cx, cy - h, cx - h, cy + h, cx + h, cy + h, col); break;
	case 1: c.tri(cx + h, cy, cx - h, cy - h, cx - h, cy + h, col); break;
	case 2: c.tri(cx, cy + h, cx - h, cy - h, cx + h, cy - h, col); break;
	case 3: c.tri(cx - h, cy, cx + h, cy - h, cx + h, cy + h, col); break;
	}
}

void u3_draw_dungeon(U3Canvas &c, int a, int L, int x, int y, int w, int h, bool show_party) {
	c.fill(x, y, w, h, 0x0C0E12);
	if (!u3_areas || a < 0) return;
	U3Area &A = u3_areas[a];
	int s = (w < h ? w : h) / 16;
	int ox = x + (w - s * 16) / 2, oy = y + (h - s * 16) / 2;
	const uint32_t FLOOR = 0x2E3440, WALL = 0x8C8170, WALL_EDGE = 0x5E5648, DOOR = 0xB07A3C;
	// faint grid of the full level so the player sees its size
	for (int j = 0; j < 16; j++)
		for (int i = 0; i < 16; i++)
			c.frame(ox + i * s, oy + j * s, s, s, 0x1A1E26);
	for (int j = 0; j < 16; j++)
		for (int i = 0; i < 16; i++) {
			int idx = L * 256 + j * 16 + i;
			if (!getbit(A.seen, idx)) continue;
			uint8_t b = A.tiles[idx];
			int sx = ox + i * s, sy = oy + j * s;
			bool visited = getbit(A.visited, idx);
			if (b & 0x80) {
				bool door = (b & 0xE0) == 0xC0 || ((b & 0xE0) == 0xA0 && visited);
				c.fill(sx, sy, s, s, WALL);
				c.frame(sx, sy, s, s, WALL_EDGE);
				if (door) {
					c.fill(sx + s / 4, sy + s / 5, s - s / 2, s - s / 5, DOOR);
					c.pixel(sx + s - s / 4 - 2, sy + s / 2 + 1, 0xFFE0A0);
				}
				continue;
			}
			c.fill(sx, sy, s, s, visited ? 0x343B49 : FLOOR);
			int cx = sx + s / 2, cy = sy + s / 2;
			int hi = b & 0x70, lo = b & 0x0F;
			if (hi & 0x40) {  // chest
				c.fill(cx - s / 4, cy - s / 5, s / 2, s / 3 + 1, 0xC8A040);
				c.hline(cx - s / 4, cy - s / 5 + s / 6, s / 2, 0x6A4A10);
			}
			if ((hi & 0x30) == 0x10) draw_arrow(c, cx, cy, s - 4, 0, 0x7FD4FF);
			else if ((hi & 0x30) == 0x20) draw_arrow(c, cx, cy, s - 4, 2, 0x7FD4FF);
			else if ((hi & 0x30) == 0x30) {
				draw_arrow(c, cx - s / 5, cy, s / 2, 0, 0x7FD4FF);
				draw_arrow(c, cx + s / 5, cy, s / 2, 2, 0x7FD4FF);
			}
			// visible specials only (traps, winds and gremlins stay hidden)
			if (lo == 2) c.disc(cx, cy, s / 4, 0x4FA8FF);            // fountain
			else if (lo == 8) { c.frame(cx - s / 4, cy - s / 4, s / 2, s / 2, 0xC890FF); c.hline(cx - s / 6, cy, s / 3, 0xC890FF); } // writing
			else if (lo == 5) { c.tri(cx, cy - s / 3, cx - s / 3, cy + s / 4, cx + s / 3, cy + s / 4, 0xFF6A50); } // mark
			else if (lo == 1) { c.ring(cx, cy, s / 3, 0xF0F0A0); }     // time lord
		}
	if (show_party && u3_map_current_area() == a && u3s.map == U3MAP_DUNGEON && u3s.dlevel == L) {
		int px = ox + (u3s.x & 15) * s + s / 2, py = oy + (u3s.y & 15) * s + s / 2;
		c.disc(px, py, s / 2, 0x000000, 140);
		draw_arrow(c, px, py, s - 2, u3s.facing & 3, 0xFFE070);
	}
}
