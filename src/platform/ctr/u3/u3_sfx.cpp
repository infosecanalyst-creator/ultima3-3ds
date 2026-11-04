// u3_sfx.cpp - the quiet UI click for bottom-screen buttons and keys.
// Plays on its own ndsp channel (SDL/DOSBox audio uses channel 0), so it never
// waits behind game audio. Kept very soft on purpose (see project UI rules).
#include "u3_ui.h"
#include <3ds.h>
#include <math.h>
#include <string.h>

enum { CLICK_CH = 23, CLICK_RATE = 22050, CLICK_LEN = 330 };   // 15 ms

static ndspWaveBuf wbuf;
static int16_t *pcm = 0;
static bool ready = false;

static void init_click() {
	pcm = (int16_t *)linearAlloc(CLICK_LEN * sizeof(int16_t));
	if (!pcm) return;
	// short decaying 2.6 kHz tick with a little noise: reads as a soft "tk"
	uint32_t seed = 12345;
	for (int i = 0; i < CLICK_LEN; i++) {
		float t = (float)i / CLICK_RATE;
		float env = expf(-t / 0.0022f);
		if (i < 6) env *= i / 6.0f;   // no hard edge at the start
		seed = seed * 1664525u + 1013904223u;
		float noise = ((int)(seed >> 16 & 0xFFFF) - 32768) / 32768.0f;
		float v = 0.8f * sinf(2.0f * 3.14159265f * 2600.0f * t) + 0.2f * noise;
		pcm[i] = (int16_t)(v * env * 1400.0f);   // about -27 dBFS before the channel mix
	}
	DSP_FlushDataCache(pcm, CLICK_LEN * sizeof(int16_t));
	ndspChnReset(CLICK_CH);
	ndspChnSetInterp(CLICK_CH, NDSP_INTERP_LINEAR);
	ndspChnSetRate(CLICK_CH, CLICK_RATE);
	ndspChnSetFormat(CLICK_CH, NDSP_FORMAT_MONO_PCM16);
	float mix[12];
	memset(mix, 0, sizeof mix);
	mix[0] = mix[1] = 0.5f;
	ndspChnSetMix(CLICK_CH, mix);
	memset(&wbuf, 0, sizeof wbuf);
	wbuf.data_vaddr = pcm;
	wbuf.nsamples = CLICK_LEN;
	ready = true;
}

void u3_click() {
	if (!ready) init_click();
	if (!ready) return;
	if (wbuf.status == NDSP_WBUF_QUEUED || wbuf.status == NDSP_WBUF_PLAYING) ndspChnWaveBufClear(CLICK_CH);
	wbuf.status = NDSP_WBUF_FREE;
	ndspChnWaveBufAdd(CLICK_CH, &wbuf);
}
