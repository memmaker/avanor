// RVIP browser frontend for Avanor (see be_web.h). Keys come from the page
// through be_pushkey(); the screen goes to the page as one cols x rows grid
// of characters and 0xRRGGBB colours per present.
#include "port/be_web.h"

#include <emscripten.h>

#include <deque>

namespace {
std::deque<int> keys;
}

EM_JS(void, be_js_init, (int cols, int rows), {
    if (Module.rvipInit) Module.rvipInit(cols, rows);
});

EM_JS(void, be_js_present, (const char* chars, const unsigned* rgbs, int cols, int rows), {
    if (Module.rvipPresent)
        Module.rvipPresent(HEAPU8.subarray(chars, chars + cols * rows),
                           HEAPU32.subarray(rgbs >> 2, (rgbs >> 2) + cols * rows), cols, rows);
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

void be_init(int cols, int rows) { be_js_init(cols, rows); }
void be_finit() { be_js_finit(); }
void be_cursor(int x, int y) { be_js_cursor(x, y); }

void be_present(const char* chars, const unsigned* rgbs, int cols, int rows)
{
    be_js_present(chars, rgbs, cols, rows);
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
