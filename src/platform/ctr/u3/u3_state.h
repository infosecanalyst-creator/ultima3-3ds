// u3_state.h - reads Ultima III game state out of emulated DOS memory.
// Offsets come from Michael C. Maggio's EXODUS.BIN disassembly notes
// (Exodus Project, bitbucket.org/mcmagi/ultima-exodus, doc/u3tech).
#ifndef U3_STATE_H
#define U3_STATE_H

#include <stdint.h>

enum U3Map {
	U3MAP_SOSARIA = 0x00, U3MAP_DUNGEON = 0x01, U3MAP_TOWN = 0x02, U3MAP_CASTLE = 0x03,
	U3MAP_SHRINE = 0x04, U3MAP_GEM = 0x11, U3MAP_COMBAT = 0x80, U3MAP_SHOP = 0xF0, U3MAP_AMBROSIA = 0xFF
};

struct U3Member {
	bool present;
	char name[11];
	char status;   // 'G' good, 'P' poisoned, 'D' dead, 'A' ashes
	char race, cls, sex;
	int hp, maxhp, food, gold, mana, exp;
	uint8_t marks;
};

#define U3_NUM_PLACES 19

struct U3State {
	bool valid;          // EXODUS.BIN found in memory and sane
	uint32_t base;       // linear address of segment offset 0
	int map;             // U3Map
	int x, y;            // position on current map
	int ow_x, ow_y;      // saved overworld position (entrance of current town/dungeon)
	int dlevel;          // dungeon level 0-7
	int facing;          // raw facing byte
	int moon_t, moon_f;  // 0-7, -1 if not valid
	int wind;            // 0 calm, 1 N, 2 S, 3 E, 4 W, -1 unknown
	int nparty;
	U3Member m[4];
	uint8_t marks_all;
	uint32_t moves;
	int place;           // index into place table for ow_x/ow_y, or -1
	uint8_t place_xy[U3_NUM_PLACES][2];
	uint32_t changes;    // bumps whenever something displayed changes
};

extern U3State u3s;
extern const char *u3_place_names[U3_NUM_PLACES];

// Call regularly (cheap). Returns true if state is valid.
bool u3_state_update();
// Read the live map memory (overworld/town 64x64, or dungeon 8x16x16)
uint8_t u3_map_byte(int offset);
uint8_t u3_mem(uint32_t off);
// Is the game currently showing a mode where the 320x200 text window is meaningful
bool u3_in_game();

// Place helpers
int u3_place_kind(int place);  // 0 castle, 1 town, 2 dungeon
const char *u3_location_name(char *buf, int n);

#endif
