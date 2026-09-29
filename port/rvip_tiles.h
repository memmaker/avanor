// RVIP: map tiles and side windows (browser build only). See rvip_tiles.cpp.
#pragma once

class XMap;
class XMapObject;
class XItem;
class XCreature;

// Sprite slots in web/tiles-dawn.png, -1 = none (the cell stays text).
int RvipTerrainTile(const XMap* map, int x, int y);
int RvipObjectTile(XMapObject* o);
int RvipItemTile(const XItem* item);
int RvipCreatureTile(XCreature* cr);

// The Map window: the whole level, cell (mx, my) shows fg over bg (slots,
// -1 = none; both -1 = text ch in rgb), dimmed when remembered. XMap::Put
// sends it between be_map_begin and be_map_end; (wx, wy) = the game's own
// viewport origin (for the cursor), (hx, hy) = the hero.
void be_map_begin(int len, int hgt, int wx, int wy, int hx, int hy);
void be_mapcell(int mx, int my, int bg, int fg, bool dim, char ch, unsigned rgb);
void be_map_end();

// Inventory and Visible windows from the hero's data.
void RvipSidePanes(XCreature* hero, const XMap* map);
