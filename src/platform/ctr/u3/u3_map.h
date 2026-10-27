// u3_map.h - explored-area tracking and map drawing (overworld, towns, dungeons)
#ifndef U3_MAP_H
#define U3_MAP_H

#include <stdint.h>
#include "u3_gfx.h"

enum { U3_AREAS = 20 };   // 0 = Sosaria, 1..19 = places from the coordinate table

struct U3Area {
	uint8_t tiles[4096];     // cached map bytes (towns/sosaria 64x64, dungeons 8 x 256)
	uint8_t seen[512];       // 1 bit per cell
	uint8_t visited[512];    // cells the party stood on
};

extern U3Area *u3_areas;
extern uint32_t u3_map_changes;

bool u3_map_init(const char *shapes_path, const char *pal_path, const char *save_path);
void u3_map_update();                 // call ~10x/sec while in game
void u3_map_save(bool force);
void u3_map_reset();                  // forget everything explored
int  u3_map_current_area();           // area index for where the party is, -1 if none
bool u3_area_known(int area);         // anything explored there?
bool u3_dungeon_level_known(int area, int level);

// Draw a 64x64 area (sosaria/town) centered on (cx,cy) with tile size ts
void u3_draw_area(U3Canvas &c, int area, int x, int y, int w, int h, float cx, float cy, int ts, bool show_party);
// Draw one dungeon level (16x16) into the rect
void u3_draw_dungeon(U3Canvas &c, int area, int level, int x, int y, int w, int h, bool show_party);

uint32_t u3_tile_color(int tile);

#endif
