// u3_state.cpp - Ultima III state reader (see u3_state.h)
#include "u3_state.h"
#include <string.h>
#include <stdio.h>
#include "mem.h"

U3State u3s;

// Order matches the coordinate table at EXODUS.BIN 0x16e1
const char *u3_place_names[U3_NUM_PLACES] = {
	"Castle of Lord British", "Castle Death", "Britain", "Moon", "Yew",
	"Montor East", "Montor West", "Grey", "Dawn", "Devil Guard", "Fawn",
	"Death Gulch", "Dungeon of Doom", "Dungeon of Fire", "Dungeon of Time",
	"Dungeon of the Snake", "Perinian Depths", "Mines of Morinia", "Dardin's Pit"
};

int u3_place_kind(int p) {
	if (p < 0) return -1;
	if (p <= 1) return 0;
	if (p <= 11) return 1;
	return 2;
}

// Memory offsets (relative to the EXODUS.BIN segment, i.e. CS:0000)
enum {
	OFF_MAP_DATA   = 0x0100,  // sosaria/town 64x64, or dungeon 8 x 16x16
	OFF_MOON_T     = 0x1324,
	OFF_MOON_F     = 0x1325,
	OFF_PARTY      = 0x14BA,  // PARTY.ULT image
	OFF_MAPID      = 0x14BC,
	OFF_MOVES      = 0x14BD,
	OFF_NPARTY     = 0x14C1,
	OFF_OW_X       = 0x14C2,
	OFF_OW_Y       = 0x14C3,
	OFF_PLAYERS    = 0x14CC,  // 4 x 64 byte records
	OFF_CUR_X      = 0x15CC,
	OFF_CUR_Y      = 0x15CD,
	OFF_DLEVEL     = 0x15CF,
	OFF_FNAME      = 0x15D4,  // "PARTY.ULT"
	OFF_PLACES     = 0x16E1,  // 19 x (x,y)
	OFF_WIND       = 0x2A92,
	OFF_WRONGDISK  = 0x2B19,  // "Wrong Diskette" - signature
	OFF_FACING     = 0x58CC,
};

static const uint32_t MEM_SCAN_END = 0xA0000;
static uint32_t last_scan_tick = 0;

static inline uint8_t rb(uint32_t lin) { return MemBase ? MemBase[lin] : 0; }

uint8_t u3_mem(uint32_t off) { return rb(u3s.base + off); }
uint8_t u3_map_byte(int offset) { return rb(u3s.base + OFF_MAP_DATA + offset); }

static int bcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static int bcd16(uint32_t off) { return bcd(u3_mem(off + 1)) * 100 + bcd(u3_mem(off)); }

static bool signature_ok(uint32_t base) {
	if (base + 0x6000 >= MEM_SCAN_END) return false;
	if (memcmp(MemBase + base + OFF_WRONGDISK, "Wrong Diskette", 14) != 0) return false;
	if (memcmp(MemBase + base + OFF_FNAME, "PARTY.ULT", 9) != 0) return false;
	return true;
}

static bool find_base() {
	if (!MemBase) return false;
	// "Wrong Diskette" string is at segment offset 0x2b19
	const char sig[] = "Wrong Diskette";
	for (uint32_t a = 0x2B19; a + 16 < MEM_SCAN_END; a++) {
		if (MemBase[a] != 'W') continue;
		if (memcmp(MemBase + a, sig, 14) != 0) continue;
		uint32_t base = a - OFF_WRONGDISK;
		if (signature_ok(base)) { u3s.base = base; return true; }
	}
	return false;
}

static uint32_t hash_state() {
	uint32_t h = 2166136261u;
#define H(v) h = (h ^ (uint32_t)(v)) * 16777619u
	H(u3s.map); H(u3s.x); H(u3s.y); H(u3s.dlevel); H(u3s.facing); H(u3s.moon_t); H(u3s.moon_f);
	H(u3s.wind); H(u3s.nparty); H(u3s.marks_all);
	for (int i = 0; i < 4; i++) { H(u3s.m[i].hp); H(u3s.m[i].maxhp); H(u3s.m[i].status); H(u3s.m[i].food); H(u3s.m[i].present); }
#undef H
	return h;
}

bool u3_state_update() {
	static uint32_t lasthash = 0;
	extern uint32_t u3_ticks_ms();
	uint32_t now = u3_ticks_ms();
	if (!u3s.valid || !signature_ok(u3s.base)) {
		u3s.valid = false;
		if (now - last_scan_tick < 1500 && last_scan_tick) return false;
		last_scan_tick = now;
		if (!find_base()) return false;
		u3s.valid = true;
	}
	uint32_t b = u3s.base;
	u3s.map = rb(b + OFF_MAPID);
	u3s.x = rb(b + OFF_CUR_X);
	u3s.y = rb(b + OFF_CUR_Y);
	u3s.ow_x = rb(b + OFF_OW_X);
	u3s.ow_y = rb(b + OFF_OW_Y);
	u3s.dlevel = rb(b + OFF_DLEVEL) & 7;
	u3s.facing = rb(b + OFF_FACING);
	int mt = rb(b + OFF_MOON_T), mf = rb(b + OFF_MOON_F);
	u3s.moon_t = mt < 8 ? mt : -1;
	u3s.moon_f = mf < 8 ? mf : -1;
	int w = rb(b + OFF_WIND);
	u3s.wind = w <= 4 ? w : -1;
	u3s.nparty = rb(b + OFF_NPARTY);
	if (u3s.nparty > 4) u3s.nparty = 0;
	u3s.moves = rb(b + OFF_MOVES) | (rb(b + OFF_MOVES + 1) << 8) | (rb(b + OFF_MOVES + 2) << 16) | (rb(b + OFF_MOVES + 3) << 24);
	u3s.marks_all = 0;
	for (int i = 0; i < 4; i++) {
		U3Member &m = u3s.m[i];
		uint32_t r = OFF_PLAYERS + i * 0x40;
		m.present = i < u3s.nparty && u3_mem(r) >= 'A';
		for (int k = 0; k < 10; k++) { char c = (char)u3_mem(r + k); m.name[k] = (c >= 32 && c < 127) ? c : 0; }
		m.name[10] = 0;
		m.marks = u3_mem(r + 0x0E);
		m.status = (char)u3_mem(r + 0x11);
		m.race = (char)u3_mem(r + 0x16);
		m.cls = (char)u3_mem(r + 0x17);
		m.sex = (char)u3_mem(r + 0x18);
		m.mana = bcd(u3_mem(r + 0x19));
		m.hp = bcd16(r + 0x1A);
		m.maxhp = bcd16(r + 0x1C);
		m.exp = bcd16(r + 0x1E);
		m.food = bcd16(r + 0x21);
		m.gold = bcd16(r + 0x23);
		if (m.status != 'G' && m.status != 'P' && m.status != 'D' && m.status != 'A') m.present = false;
		if (m.present) u3s.marks_all |= m.marks;
	}
	for (int p = 0; p < U3_NUM_PLACES; p++) {
		u3s.place_xy[p][0] = rb(b + OFF_PLACES + p * 2);
		u3s.place_xy[p][1] = rb(b + OFF_PLACES + p * 2 + 1);
	}
	u3s.place = -1;
	if (u3s.map != U3MAP_SOSARIA) {
		for (int p = 0; p < U3_NUM_PLACES; p++)
			if (u3s.place_xy[p][0] == u3s.ow_x && u3s.place_xy[p][1] == u3s.ow_y) { u3s.place = p; break; }
	}
	uint32_t h = hash_state();
	if (h != lasthash) { lasthash = h; u3s.changes++; }
	return true;
}

bool u3_in_game() {
	return u3s.valid && u3s.nparty > 0;
}

const char *u3_location_name(char *buf, int n) {
	if (!u3s.valid) { snprintf(buf, n, "Ultima III"); return buf; }
	switch (u3s.map) {
	case U3MAP_SOSARIA: snprintf(buf, n, "Sosaria"); break;
	case U3MAP_DUNGEON:
		if (u3s.place >= 0) snprintf(buf, n, "%s, level %d", u3_place_names[u3s.place], u3s.dlevel + 1);
		else snprintf(buf, n, "Dungeon, level %d", u3s.dlevel + 1);
		break;
	case U3MAP_TOWN: case U3MAP_CASTLE:
		snprintf(buf, n, "%s", u3s.place >= 0 ? u3_place_names[u3s.place] : "Town"); break;
	case U3MAP_SHRINE: snprintf(buf, n, "Shrine"); break;
	case U3MAP_COMBAT: snprintf(buf, n, "Combat"); break;
	case U3MAP_SHOP: snprintf(buf, n, "%s", u3s.place >= 0 ? u3_place_names[u3s.place] : "Shop"); break;
	case U3MAP_AMBROSIA: snprintf(buf, n, "Ambrosia"); break;
	default: snprintf(buf, n, "Sosaria"); break;
	}
	return buf;
}
