// RVIP browser frontend for Avanor (see be_web.h). Keys come from the page
// through be_pushkey(); the screen goes to the page as one cols x rows grid
// of characters and 0xRRGGBB colours per present.
#include "port/be_web.h"
#include "port/rvip_tiles.h"

#include <emscripten.h>

#include <deque>
#include <vector>

namespace {
std::deque<int> keys;

// Map tiles per screen cell (be_tile), sent while the cell still shows
// the char+colour they were set with.
struct TileCell {
    int packed = -1;
    bool set = false;
    char ch = 0;
    unsigned rgb = 0;
};
std::vector<TileCell> tiles;
std::vector<int> sent;
int tile_cols = 0;
bool suspended = false;
}

void be_tiles_suspend(bool on) { suspended = on; }

void be_tile(int sx, int sy, int bg, int fg, bool dim, char ch, unsigned rgb)
{
    if (tile_cols <= 0 || sx < 0 || sy < 0 || sx >= tile_cols || static_cast<size_t>(sy * tile_cols + sx) >= tiles.size()) {
        return;
    }

    TileCell& c = tiles[sy * tile_cols + sx];
    c.set = true;
    c.packed = (bg < 0 && fg < 0) ? -1 : ((bg < 0 ? 0xFFF : bg) | ((fg < 0 ? 0xFFF : fg) << 12) | (dim ? 1 << 24 : 0));
    c.ch = ch < ' ' ? ' ' : ch;
    c.rgb = rgb & 0xFFFFFF;
}

EM_JS(void, be_js_init, (int cols, int rows), {
    if (Module.rvipInit) Module.rvipInit(cols, rows);
});

// tiles: per cell -1 (not map), -2 (map cell, text) or bg | fg << 12 | dim << 24 (0xFFF = none),
// slots in tiles-dawn.png.
EM_JS(void, be_js_present, (const char* chars, const unsigned* rgbs, const int* tl, int cols, int rows), {
    if (Module.rvipPresent)
        Module.rvipPresent(HEAPU8.subarray(chars, chars + cols * rows),
                           HEAPU32.subarray(rgbs >> 2, (rgbs >> 2) + cols * rows), cols, rows,
                           HEAP32.subarray(tl >> 2, (tl >> 2) + cols * rows));
});

EM_JS(void, be_js_cursor, (int x, int y), {
    if (Module.rvipCursor) Module.rvipCursor(x, y);
});

EM_JS(void, be_js_finit, (), {
    if (Module.rvipExit) Module.rvipExit();
});

extern "C" EMSCRIPTEN_KEEPALIVE void be_pushkey(int key)
{
    keys.push_back(key);
}

void be_init(int cols, int rows)
{
    tile_cols = cols;
    tiles.assign(static_cast<size_t>(cols) * rows, TileCell());
    be_js_init(cols, rows);
}
void be_finit() { be_js_finit(); }
void be_cursor(int x, int y) { be_js_cursor(x, y); }

void be_present(const char* chars, const unsigned* rgbs, int cols, int rows)
{
    const size_t n = static_cast<size_t>(cols) * rows;
    sent.assign(n, -1);
    if (!suspended && cols == tile_cols && tiles.size() == n) {
        for (size_t i = 0; i < n; i++) {
            if (!tiles[i].set) {
                continue;
            }
            // -2: a map cell showing text
            sent[i] = -2;
            if (tiles[i].packed >= 0 && tiles[i].ch == chars[i] && tiles[i].rgb == (rgbs[i] & 0xFFFFFF)) {
                sent[i] = tiles[i].packed;
            }
        }
    }
    be_js_present(chars, rgbs, sent.data(), cols, rows);
}

int be_kbhit()
{
    emscripten_sleep(0);
    return keys.empty() ? 0 : 1;
}

int be_getkey()
{
    while (keys.empty()) {
        emscripten_sleep(10);
    }

    const int k = keys.front();
    keys.pop_front();
    return k;
}

void be_delay(int ms)
{
    emscripten_sleep(ms);
}
