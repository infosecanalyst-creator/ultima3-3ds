// u3_gfx.cpp - software 2D canvas (see u3_gfx.h)
#include "u3_gfx.h"
#include <string.h>
#include <stdlib.h>
#include <3ds.h>

static inline uint16_t to565(uint32_t c) {
	return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}
static inline uint32_t from565(uint16_t p) {
	uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
	return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

void U3Canvas::init(void *pixels, int width, int height, U3Fmt f) {
	px = pixels; w = width; h = height; fmt = f;
	noclip();
}
void U3Canvas::clip(int x, int y, int cw, int ch) {
	cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
	cx1 = x + cw > w ? w : x + cw; cy1 = y + ch > h ? h : y + ch;
}
void U3Canvas::noclip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }

void U3Canvas::pixel(int x, int y, uint32_t rgb, int a) {
	if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || a <= 0) return;
	if (fmt == U3_RGB565) {
		uint16_t *p = (uint16_t *)px + y * w + x;
		if (a >= 255) *p = to565(rgb);
		else *p = to565(u3_mix(from565(*p), rgb, a));
	} else {
		uint32_t *p = (uint32_t *)px + y * w + x;
		if (a >= 255) { *p = (rgb << 8) | 0xFF; return; }
		uint32_t d = *p;
		int da = d & 255;
		int oa = a + (da * (255 - a)) / 255;
		if (oa <= 0) { *p = 0; return; }
		uint32_t drgb = d >> 8;
		int t = (a * 255) / oa; // weight of new color
		*p = (u3_mix(drgb, rgb, t) << 8) | (uint32_t)oa;
	}
}

void U3Canvas::fill(int x, int y, int fw, int fh, uint32_t rgb, int a) {
	int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
	int x1 = x + fw > cx1 ? cx1 : x + fw, y1 = y + fh > cy1 ? cy1 : y + fh;
	if (x0 >= x1 || y0 >= y1 || a <= 0) return;
	if (a >= 255) {
		if (fmt == U3_RGB565) {
			uint16_t c = to565(rgb);
			for (int yy = y0; yy < y1; yy++) {
				uint16_t *p = (uint16_t *)px + yy * w + x0;
				for (int xx = x0; xx < x1; xx++) *p++ = c;
			}
		} else {
			uint32_t c = (rgb << 8) | 0xFF;
			for (int yy = y0; yy < y1; yy++) {
				uint32_t *p = (uint32_t *)px + yy * w + x0;
				for (int xx = x0; xx < x1; xx++) *p++ = c;
			}
		}
		return;
	}
	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++) pixel(xx, yy, rgb, a);
}

void U3Canvas::clear(uint32_t rgb, int a) {
	if (fmt == U3_RGBA8 && a == 0) { memset(px, 0, w * h * 4); return; }
	int sx0 = cx0, sy0 = cy0, sx1 = cx1, sy1 = cy1;
	noclip();
	fill(0, 0, w, h, rgb, 255);
	cx0 = sx0; cy0 = sy0; cx1 = sx1; cy1 = sy1;
}

void U3Canvas::hline(int x, int y, int lw, uint32_t rgb, int a) { fill(x, y, lw, 1, rgb, a); }
void U3Canvas::vline(int x, int y, int lh, uint32_t rgb, int a) { fill(x, y, 1, lh, rgb, a); }
void U3Canvas::frame(int x, int y, int fw, int fh, uint32_t rgb, int a) {
	hline(x, y, fw, rgb, a); hline(x, y + fh - 1, fw, rgb, a);
	vline(x, y + 1, fh - 2, rgb, a); vline(x + fw - 1, y + 1, fh - 2, rgb, a);
}

// coverage of a corner pixel for rounded rectangles (simple AA)
static int corner_cov(int dx, int dy, int r) {
	// dx,dy: distance from the corner circle center, pixel center offset
	float fx = dx + 0.5f, fy = dy + 0.5f;
	float d2 = fx * fx + fy * fy;
	float r0 = (r - 0.5f) * (r - 0.5f), r1 = (r + 0.5f) * (r + 0.5f);
	if (d2 <= r0) return 255;
	if (d2 >= r1) return 0;
	return (int)(255 * (r1 - d2) / (r1 - r0));
}

void U3Canvas::fill_round(int x, int y, int fw, int fh, int r, uint32_t rgb, int a) {
	if (r <= 0) { fill(x, y, fw, fh, rgb, a); return; }
	if (r * 2 > fw) r = fw / 2;
	if (r * 2 > fh) r = fh / 2;
	fill(x, y + r, fw, fh - 2 * r, rgb, a);
	for (int j = 0; j < r; j++) {
		// rows in the rounded top/bottom bands
		int dy = r - 1 - j;
		int inner = 0;
		for (int i = 0; i < r; i++) {
			int dx = r - 1 - i;
			int c = corner_cov(dx, dy, r);
			if (c >= 255) { inner = i; break; }
			if (c > 0) {
				int ca = c * a / 255;
				pixel(x + i, y + j, rgb, ca); pixel(x + fw - 1 - i, y + j, rgb, ca);
				pixel(x + i, y + fh - 1 - j, rgb, ca); pixel(x + fw - 1 - i, y + fh - 1 - j, rgb, ca);
			}
			inner = i + 1;
		}
		fill(x + inner, y + j, fw - 2 * inner, 1, rgb, a);
		fill(x + inner, y + fh - 1 - j, fw - 2 * inner, 1, rgb, a);
	}
}

void U3Canvas::frame_round(int x, int y, int fw, int fh, int r, uint32_t rgb, int a) {
	if (r <= 1) { frame(x, y, fw, fh, rgb, a); return; }
	hline(x + r, y, fw - 2 * r, rgb, a); hline(x + r, y + fh - 1, fw - 2 * r, rgb, a);
	vline(x, y + r, fh - 2 * r, rgb, a); vline(x + fw - 1, y + r, fh - 2 * r, rgb, a);
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++) {
			float fx = (r - 1 - i) + 0.5f, fy = (r - 1 - j) + 0.5f;
			float d = fx * fx + fy * fy;
			float lo = (r - 1.0f) * (r - 1.0f), hi = (float)r * r;
			if (d < lo - 2 * r || d > hi + 0.5f) continue;
			// distance from the ring centerline
			float dist = d - (r - 0.5f) * (r - 0.5f);
			if (dist < 0) dist = -dist;
			int c = dist >= r ? 0 : (int)(255 * (1.0f - dist / r));
			int ca = c * a / 255;
			if (ca <= 0) continue;
			pixel(x + i, y + j, rgb, ca); pixel(x + fw - 1 - i, y + j, rgb, ca);
			pixel(x + i, y + fh - 1 - j, rgb, ca); pixel(x + fw - 1 - i, y + fh - 1 - j, rgb, ca);
		}
}

void U3Canvas::line(int x0, int y0, int x1, int y1, uint32_t rgb, int a) {
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		pixel(x0, y0, rgb, a);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void U3Canvas::disc(int cx, int cy, int r, uint32_t rgb, int a) {
	for (int y = -r - 1; y <= r + 1; y++)
		for (int x = -r - 1; x <= r + 1; x++) {
			float d2 = (x + 0.0f) * x + (y + 0.0f) * y;
			float r0 = (r - 0.5f) * (r - 0.5f), r1 = (r + 0.5f) * (r + 0.5f);
			int c = d2 <= r0 ? 255 : d2 >= r1 ? 0 : (int)(255 * (r1 - d2) / (r1 - r0));
			if (c) pixel(cx + x, cy + y, rgb, c * a / 255);
		}
}

void U3Canvas::ring(int cx, int cy, int r, uint32_t rgb, int a) {
	for (int y = -r - 1; y <= r + 1; y++)
		for (int x = -r - 1; x <= r + 1; x++) {
			float d = __builtin_sqrtf((float)(x * x + y * y)) - r;
			if (d < 0) d = -d;
			if (d < 1.0f) pixel(cx + x, cy + y, rgb, (int)((1.0f - d) * a));
		}
}

void U3Canvas::tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb, int a) {
	int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
	int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
	int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
	int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
	for (int y = miny; y <= maxy; y++)
		for (int x = minx; x <= maxx; x++) {
			int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
			int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
			int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
			if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) pixel(x, y, rgb, a);
		}
}

static inline int glyph_index(unsigned char c) {
	if (c >= 32 && c < 127) return c - 32;
	if (c == 0x95 || c == 7) return 95; // bullet
	return '?' - 32;
}

int U3Canvas::textn(const u3_font &f, int x, int y, const char *s, int n, uint32_t rgb, int a) {
	int x0 = x;
	for (int k = 0; k < n && s[k]; k++) {
		const u3_glyph &g = f.glyphs[glyph_index((unsigned char)s[k])];
		const unsigned char *d = f.data + g.off;
		int gx = x + g.lsb;
		if (g.w && gx + g.w > cx0 && gx < cx1 && y + f.height > cy0 && y < cy1) {
			int idx = 0;
			for (int yy = 0; yy < f.height; yy++)
				for (int xx = 0; xx < g.w; xx++, idx++) {
					int v = (d[idx >> 1] >> ((idx & 1) * 4)) & 15;
					if (v) pixel(gx + xx, y + yy, rgb, (v * 17) * a / 255);
				}
		}
		x += g.adv;
	}
	return x - x0;
}
int U3Canvas::text(const u3_font &f, int x, int y, const char *s, uint32_t rgb, int a) {
	return textn(f, x, y, s, 1 << 30, rgb, a);
}
void U3Canvas::text_center(const u3_font &f, int cx, int y, const char *s, uint32_t rgb, int a) {
	text(f, cx - u3_text_width(f, s) / 2, y, s, rgb, a);
}

int u3_text_widthn(const u3_font &f, const char *s, int n) {
	int wsum = 0;
	for (int k = 0; k < n && s[k]; k++) wsum += f.glyphs[glyph_index((unsigned char)s[k])].adv;
	return wsum;
}
int u3_text_width(const u3_font &f, const char *s) { return u3_text_widthn(f, s, 1 << 30); }

int u3_wrap(const u3_font &f, const char *s, int maxw) {
	int wsum = 0, last_space = -1, i = 0;
	for (; s[i] && s[i] != '\n'; i++) {
		if (s[i] == ' ') last_space = i;
		wsum += f.glyphs[glyph_index((unsigned char)s[i])].adv;
		if (wsum > maxw) {
			if (last_space > 0) return last_space + 1;
			return i > 0 ? i : 1;
		}
	}
	return i + (s[i] == '\n' ? 1 : 0);
}

void U3Canvas::blit_rgb888(const uint8_t *img, int iw, int ih, float sx, float sy, float sw, float sh,
                           int dx, int dy, int dw, int dh) {
	if (dw <= 0 || dh <= 0) return;
	float stepx = sw / dw, stepy = sh / dh;
	int box = stepx > 1.5f ? (int)stepx : 1; // cheap box filter when shrinking
	if (box > 4) box = 4;
	for (int y = 0; y < dh; y++) {
		int py = dy + y;
		if (py < cy0 || py >= cy1) continue;
		int iy = (int)(sy + y * stepy);
		if (iy < 0 || iy >= ih) { fill(dx, py, dw, 1, 0x202020); continue; }
		for (int x = 0; x < dw; x++) {
			int pxx = dx + x;
			if (pxx < cx0 || pxx >= cx1) continue;
			int ix = (int)(sx + x * stepx);
			if (ix < 0 || ix >= iw) { pixel(pxx, py, 0x202020); continue; }
			uint32_t r = 0, g = 0, b = 0, n = 0;
			for (int by = 0; by < box; by++) {
				int yy = iy + by; if (yy >= ih) break;
				const uint8_t *p = img + (yy * iw + ix) * 3;
				for (int bx = 0; bx < box && ix + bx < iw; bx++, p += 3) { r += p[0]; g += p[1]; b += p[2]; n++; }
			}
			if (!n) n = 1;
			pixel(pxx, py, ((r / n) << 16) | ((g / n) << 8) | (b / n));
		}
	}
}

void U3Canvas::blit_rgb565(const uint16_t *src, int pitch_px, int sx, int sy, int sw, int sh,
                           int dx, int dy, int dw, int dh) {
	if (fmt != U3_RGB565 || dw <= 0 || dh <= 0) return;
	for (int y = 0; y < dh; y++) {
		int py = dy + y;
		if (py < cy0 || py >= cy1) continue;
		const uint16_t *row = src + (sy + y * sh / dh) * pitch_px + sx;
		uint16_t *out = (uint16_t *)px + py * w;
		for (int x = 0; x < dw; x++) {
			int pxx = dx + x;
			if (pxx < cx0 || pxx >= cx1) continue;
			out[pxx] = row[x * sw / dw];
		}
	}
}

void U3Canvas::present_bottom() {
	if (fmt != U3_RGB565 || w != 320 || h != 240) return;
	u16 fbw, fbh;
	uint16_t *fb = (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fbw, &fbh); // fbw=240 rows, fbh=320
	const uint16_t *src = (const uint16_t *)px;
	for (int x = 0; x < 320; x++) {
		uint16_t *col = fb + x * 240;
		for (int y = 0; y < 240; y++) col[239 - y] = src[y * 320 + x];
	}
	gfxFlushBuffers();
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}
