// u3_gfx.h - tiny software 2D canvas used for the 3DS bottom-screen UI and
// the top-screen info strips. Colors are 0xRRGGBB; alpha is 0..255.
#ifndef U3_GFX_H
#define U3_GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t off; uint8_t w; uint8_t adv; int8_t lsb; } u3_glyph;
typedef struct {
	const unsigned char *data;
	const u3_glyph *glyphs;
	int height;
	int ascent;
} u3_font;

extern const u3_font font_small, font_ui, font_bold, font_big, font_read, font_readb;

#ifdef __cplusplus
}

enum U3Fmt { U3_RGB565, U3_RGBA8 };

struct U3Canvas {
	void *px;
	int w, h;
	U3Fmt fmt;
	// clip rect
	int cx0, cy0, cx1, cy1;

	void init(void *pixels, int width, int height, U3Fmt f);
	void clip(int x, int y, int w, int h);
	void noclip();

	void clear(uint32_t rgb, int a = 255);
	void pixel(int x, int y, uint32_t rgb, int a = 255);
	void fill(int x, int y, int w, int h, uint32_t rgb, int a = 255);
	void fill_round(int x, int y, int w, int h, int r, uint32_t rgb, int a = 255);
	void frame(int x, int y, int w, int h, uint32_t rgb, int a = 255);
	void frame_round(int x, int y, int w, int h, int r, uint32_t rgb, int a = 255);
	void hline(int x, int y, int w, uint32_t rgb, int a = 255);
	void vline(int x, int y, int h, uint32_t rgb, int a = 255);
	void line(int x0, int y0, int x1, int y1, uint32_t rgb, int a = 255);
	void disc(int cx, int cy, int r, uint32_t rgb, int a = 255);
	void ring(int cx, int cy, int r, uint32_t rgb, int a = 255);
	void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb, int a = 255);

	// text; returns advance width. y is the top of the text line.
	int text(const u3_font &f, int x, int y, const char *s, uint32_t rgb, int a = 255);
	int textn(const u3_font &f, int x, int y, const char *s, int n, uint32_t rgb, int a = 255);
	void text_center(const u3_font &f, int cx, int y, const char *s, uint32_t rgb, int a = 255);

	// Blit a region of an RGB888 image scaled into dst rect (nearest; box filter when shrinking)
	void blit_rgb888(const uint8_t *img, int iw, int ih, float sx, float sy, float sw, float sh,
	                 int dx, int dy, int dw, int dh);
	// Blit RGB565 source with integer-free scaling (nearest)
	void blit_rgb565(const uint16_t *src, int pitch_px, int sx, int sy, int sw, int sh,
	                 int dx, int dy, int dw, int dh);

	// Present this (320x240 RGB565) canvas to the 3DS bottom framebuffer
	void present_bottom();
};

int u3_text_width(const u3_font &f, const char *s);
int u3_text_widthn(const u3_font &f, const char *s, int n);
// Word-wrap: returns number of chars that fit on a line of width maxw (breaking at spaces)
int u3_wrap(const u3_font &f, const char *s, int maxw);

static inline uint32_t u3_mix(uint32_t a, uint32_t b, int t) { // t 0..255 toward b
	int r = ((a >> 16) & 255) + ((((b >> 16) & 255) - ((a >> 16) & 255)) * t) / 255;
	int g = ((a >> 8) & 255) + ((((b >> 8) & 255) - ((a >> 8) & 255)) * t) / 255;
	int bl = (a & 255) + (((b & 255) - (a & 255)) * t) / 255;
	return (r << 16) | (g << 8) | bl;
}
#endif

#endif
