// RVIP browser frontend for Avanor (see be_web.h). Keys come from the page
// through be_pushkey(). Each present sends the page its windows, each as
// its own content (RVIP W0 rule 6): the map as a whole-level grid of
// sprites/glyphs, the status rows and the message rows as text lines, and
// anything else on screen (menus, lists, full screens) as a pop-up.
// Text lines carry colour runs "\x05#rrggbb" in the game's own colours.
#include "port/be_web.h"
#include "port/rvip_tiles.h"

#include <emscripten.h>

#include <deque>
#include <string>
#include <vector>

#include "engine/global.h"

namespace {
std::deque<int> keys;
int scr_cols = 0, scr_rows = 0;

// Map window
struct MapCell {
    int packed = -1; // bg | fg << 12 | dim << 24 (0xFFF = none), -1 = text
    unsigned char ch = ' ';
    unsigned rgb = 0;
};
std::vector<MapCell> mcells;
std::vector<unsigned char> m_ch;
std::vector<unsigned> m_rgb;
std::vector<int> m_tl;
int m_len = 0, m_hgt = 0, m_wx = 0, m_wy = 0, m_hx = 0, m_hy = 0;
bool map_dirty = false;

// What the screen shows: the map (map_live, set by XMap::Put, cleared by
// vClrScr) and over it, while vStore'd, a menu (depth > 0): the pop-up is
// what differs from the screen at the outermost vStore.
bool map_live = false;
std::vector<bool> live_stack;
std::vector<char> snap_ch;
std::vector<unsigned> snap_rgb;
int status_y0 = -1, status_n = 0;
int cur_x = -1, cur_y = -1;
bool at_cmd = false;

std::string sent_status = "\x01", sent_prompt = "\x01", sent_pop = "\x01";

void Hex(std::string& out, unsigned rgb)
{
    static const char* d = "0123456789abcdef";
    out += "\x05#";
    for (int s = 20; s >= 0; s -= 4) out += d[(rgb >> s) & 15];
}

// Row y, columns x0 .. x1-1, trailing blanks trimmed, as text with colour runs.
std::string Row(const char* ch, const unsigned* rgb, int y, int x0, int x1)
{
    int end = x1;
    while (end > x0 && (ch[y * scr_cols + end - 1] == ' ' || ch[y * scr_cols + end - 1] == 0)) end--;
    std::string out;
    unsigned col = 0xFFFFFFFF;
    for (int x = x0; x < end; x++) {
        const int i = y * scr_cols + x;
        const char c = ch[i] ? ch[i] : ' ';
        if (c != ' ' && (rgb[i] & 0xFFFFFF) != col) {
            col = rgb[i] & 0xFFFFFF;
            Hex(out, col);
        }
        out += c;
    }
    return out;
}
}

EM_JS(void, be_js_map, (const unsigned char* ch, const unsigned* rgb, const int* tl, int len, int hgt, int hx, int hy), {
    var n = len * hgt;
    Module.av.map(HEAPU8.slice(ch, ch + n), HEAPU32.slice(rgb >> 2, (rgb >> 2) + n),
                  HEAP32.slice(tl >> 2, (tl >> 2) + n), len, hgt, hx, hy);
});
EM_JS(void, be_js_text, (int which, const char* s, int cy, int cx), {
    Module.av.text(which, UTF8ToString(s), cy, cx);
});
EM_JS(void, be_js_mapcur, (int x, int y), { Module.av.mapCursor(x, y); });
EM_JS(void, be_js_atcmd, (int on), { Module.av.atCmd(on); });
EM_JS(void, be_js_saved, (), { Module.av.saved(); });
EM_JS(void, be_js_finit, (), { Module.av.exit(); });

// which: 0 status, 1 prompt, 2 pop-up, 3 message, 4 inventory, 5 visible
static void Text(int which, const std::string& s, int cy = -1, int cx = -1)
{
    be_js_text(which, s.c_str(), cy, cx);
}

extern "C" EMSCRIPTEN_KEEPALIVE void be_pushkey(int key)
{
    keys.push_back(key);
}

void be_init(int cols, int rows)
{
    scr_cols = cols;
    scr_rows = rows;
}
void be_finit() { be_js_finit(); }
void be_cursor(int x, int y)
{
    cur_x = x;
    cur_y = y;
}

void be_map_begin(int len, int hgt, int wx, int wy, int hx, int hy)
{
    m_len = len;
    m_hgt = hgt;
    m_wx = wx;
    m_wy = wy;
    m_hx = hx;
    m_hy = hy;
    mcells.assign(static_cast<size_t>(len) * hgt, MapCell());
    map_live = true;
}

void be_mapcell(int mx, int my, int bg, int fg, bool dim, char ch, unsigned rgb)
{
    if (mx < 0 || my < 0 || mx >= m_len || my >= m_hgt) return;
    MapCell& c = mcells[static_cast<size_t>(my) * m_len + mx];
    c.packed = (bg < 0 && fg < 0) ? -1 : ((bg < 0 ? 0xFFF : bg) | ((fg < 0 ? 0xFFF : fg) << 12) | (dim ? 1 << 24 : 0));
    c.ch = static_cast<unsigned char>(ch < ' ' ? ' ' : ch);
    c.rgb = rgb & 0xFFFFFF;
}

void be_map_end() { map_dirty = true; }

void be_store(const char* chars, const unsigned* rgbs, int n)
{
    if (live_stack.empty()) {
        snap_ch.assign(chars, chars + n);
        snap_rgb.assign(rgbs, rgbs + n);
    }
    live_stack.push_back(map_live);
}

void be_restore()
{
    if (!live_stack.empty()) {
        map_live = live_stack.back();
        live_stack.pop_back();
    }
}

void be_cleared() { map_live = false; }

void be_status_rows(int y0, int n)
{
    status_y0 = y0;
    status_n = n;
}

void be_at_cmd(bool on)
{
    if (on) {
        live_stack.clear(); // nothing is open over the map at a command
    }
    if (on != at_cmd) {
        at_cmd = on;
        be_js_atcmd(on ? 1 : 0);
    }
}

void be_saved() { be_js_saved(); }

void be_msg(const std::string& line)
{
    // Colour escapes as vPutS reads them (ExpandMarkup's output).
    std::string out;
    for (size_t i = 0; i < line.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c == 31 && i + 1 < line.size()) {
            Hex(out, ResolveColour(static_cast<unsigned char>(line[++i])));
        } else if (c == static_cast<unsigned char>(RGB_ESCAPE) && i + 6 < line.size()) {
            out += "\x05#";
            out += line.substr(i + 1, 6);
            i += 6;
        } else if (c >= ' ') {
            out += static_cast<char>(c);
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (!out.empty()) Text(3, out);
}

void be_present(const char* chars, const unsigned* rgbs, int cols, int rows)
{
    scr_cols = cols;
    scr_rows = rows;
    const int n = cols * rows;

    if (map_dirty) {
        map_dirty = false;
        const size_t mn = mcells.size();
        m_ch.resize(mn);
        m_rgb.resize(mn);
        m_tl.resize(mn);
        for (size_t i = 0; i < mn; i++) {
            m_ch[i] = mcells[i].ch;
            m_rgb[i] = mcells[i].rgb;
            m_tl[i] = mcells[i].packed;
        }
        be_js_map(m_ch.data(), m_rgb.data(), m_tl.data(), m_len, m_hgt, m_hx, m_hy);
    }

    // Pop-up: a cleared screen (no map) = everything on it; a menu over
    // the map = the cells that differ from the screen before it opened.
    int y0 = rows, y1 = -1, x0 = cols, x1 = -1;
    const bool full = !map_live;
    const bool over = map_live && !live_stack.empty() && static_cast<int>(snap_ch.size()) == n;
    if (full || over) {
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                const int i = y * cols + x;
                const bool show = full ? (chars[i] != ' ' && chars[i] != 0)
                                       : (chars[i] != snap_ch[i] || (rgbs[i] & 0xFFFFFF) != (snap_rgb[i] & 0xFFFFFF));
                if (show) {
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                }
            }
        }
        // the text cursor (a question inside the box) belongs to it
        if (y1 >= 0 && cur_y >= y0 && cur_y <= y1 + 1 && cur_y < rows && cur_x >= 0) {
            if (cur_y > y1) y1 = cur_y;
            if (cur_x > x1) x1 = cur_x;
            if (cur_x < x0) x0 = cur_x;
        }
    }
    std::string pop;
    int pcy = -1, pcx = -1;
    if (y1 >= 0) {
        for (int y = y0; y <= y1; y++) {
            pop += Row(chars, rgbs, y, x0, x1 + 1);
            pop += '\n';
        }
        if (cur_y >= y0 && cur_y <= y1 && cur_x >= x0) {
            pcy = cur_y - y0;
            pcx = cur_x - x0;
        }
    }
    const std::string popkey = pop + std::to_string(pcy) + "," + std::to_string(pcx);
    if (popkey != sent_pop) {
        sent_pop = popkey;
        Text(2, pop, pcy, pcx);
    }

    if (map_live && live_stack.empty()) {
        // message rows (0, 1) = the prompt line over the map
        std::string pr = Row(chars, rgbs, 0, 0, cols);
        const std::string r1 = Row(chars, rgbs, 1, 0, cols);
        if (!r1.empty()) pr += "\n" + r1;
        if (pr != sent_prompt) {
            sent_prompt = pr;
            Text(1, pr);
        }
        if (status_y0 >= 0) {
            std::string st;
            for (int y = status_y0; y < status_y0 + status_n && y < rows; y++) {
                st += Row(chars, rgbs, y, 0, cols);
                st += '\n';
            }
            while (!st.empty() && st.back() == '\n') st.pop_back();
            if (st != sent_status) {
                sent_status = st;
                Text(0, st);
            }
        }
        // cursor on the map (look/target); none on the hero
        const int mx = cur_x - 0 + m_wx, my = cur_y - 2 + m_wy;
        const bool on = cur_y >= 2 && cur_y < rows - 3 && cur_x >= 0 && !(mx == m_hx && my == m_hy);
        be_js_mapcur(on ? mx : -1, on ? my : -1);
    } else {
        be_js_mapcur(-1, -1);
    }
}

void RvipSendSide(int which, const std::string& s)
{
    static std::string last[2] = {"\x01", "\x01"};
    if (last[which - 4] != s) {
        last[which - 4] = s;
        Text(which, s);
    }
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

// A game action's sound event (port/rvip_sound.h); the page plays it.
EM_JS(void, be_js_sound, (const char* e), { if (Module.av.sound) Module.av.sound(UTF8ToString(e)); });
void be_sound(const char* event) { be_js_sound(event); }

// Graveyard/leaderboard report (RVIP stage 9); errors swallowed.
EM_JS(void, be_js_beacon, (const char* ev, const char* name, const char* killer, double score, int turns, int lvl), {
    try {
        var p = [['g', 'avanor'], ['ev', UTF8ToString(ev)], ['name', name ? UTF8ToString(name) : ''],
                 ['killer', killer ? UTF8ToString(killer) : ''], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== ''; })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        if (window.RvipWM && RvipWM.report) RvipWM.report(q); else fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});
void be_run_end(const char* ev, const char* name, const char* killer, long score, int turns, int lvl)
{
    be_js_beacon(ev, name, killer, (double)score, turns, lvl);
}
