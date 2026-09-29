// RVIP stage 4: map tiles (browser build only). See rvip_tiles.cpp.
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

// Screen cell (sx, sy) shows fg over bg (slots, -1 = none), dimmed when
// remembered; valid while the screen cell keeps char ch in colour rgb.
void be_tile(int sx, int sy, int bg, int fg, bool dim, char ch, unsigned rgb);
