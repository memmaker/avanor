/*
This file is part of "Avanor, the Land of Mystery" roguelike game

Copyright (C) 2000-2006 Vadim Gaidukevich
Copyright (C) 2025,2026 Joachim de Groot

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include <sol/sol.hpp>

// Asking the system where the running executable is, so the game can find
// what was installed beside it. Each platform answers differently, and
// only Windows needs a header the backends below may not have pulled in.
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#else
    #include <unistd.h>
#endif

// Two interchangeable terminal backends, chosen at compile time. The
// default is a plain-ANSI one built on the header-only stc.hpp: it asks
// nothing of the system but a terminal that understands escape sequences,
// which is what keeps the game simple to build and to hand to somebody.
//
// Building with notcurses=1 defines USE_NOTCURSES and takes the other
// path instead, at the cost of a shared library to find, package and
// ship. What it brings is its own capability negotiation, and a live
// resize on Windows - see vUpdateScreenSize() for why the plain backend
// cannot hear about one there, and why everywhere else it can.
// See engine/global.h and the Makefile's `notcurses` variable.
#ifndef USE_NOTCURSES
    #ifdef _WIN32
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
        #include <windows.h>
        #include <conio.h>
    #else
        // The same backend on anything unixoid.
        #include <csignal>
        #include <cerrno>

        #include <poll.h>
        #include <sys/ioctl.h>
        #include <termios.h>
        #include <unistd.h>
    #endif

    #include <iostream>
    #include <vector>

    #include <stc.hpp>

    #ifdef __EMSCRIPTEN__
        // RVIP: the browser frontend (port/be_web.cpp) stands in for the terminal.
        #include "port/be_web.h"
    #endif
#else
    #include <ncpp/NotCurses.hh>
    #include <ncpp/Plane.hh>
#endif

#include "engine/global.h"

void RegisterColorEnum(sol::state_view& lua)
{
    lua.new_enum("xColor",
        "xBLACK", xBLACK,
        "xBLUE", xBLUE,
        "xGREEN", xGREEN,
        "xCYAN", xCYAN,
        "xRED", xRED,
        "xMAGENTA", xMAGENTA,
        "xBROWN", xBROWN,
        "xLIGHTGRAY", xLIGHTGRAY,
        "xDARKGRAY", xDARKGRAY,
        "xLIGHTBLUE", xLIGHTBLUE,
        "xLIGHTGREEN", xLIGHTGREEN,
        "xLIGHTCYAN", xLIGHTCYAN,
        "xLIGHTRED", xLIGHTRED,
        "xLIGHTMAGENTA", xLIGHTMAGENTA,
        "xYELLOW", xYELLOW,
        "xWHITE", xWHITE
    );
}

unsigned long total_cr = 0;
unsigned long total_it = 0;
unsigned long cr_kiled = 0;
unsigned long cr_died = 0;

std::string SCOLOR(const unsigned rgb)
{
    return fmt::format("<#{:06x}>", rgb & 0xFFFFFF);
}

int __animation_flag = 100;

int size_x = 80;
int size_y = 25;

int cursor_pos_x = 0;
int cursor_pos_y = 0;
unsigned current_attr = xLIGHTGRAY;

// Out of sight, and drawn that way - see SetRememberedBrightness().
int remembered_brightness = 100;

// How much ground colour varies from cell to cell - see SetTileJitter().
int tile_jitter = 10;
int tile_hue_jitter = 12;
int tile_saturation_jitter = 15;

#ifndef USE_NOTCURSES

namespace {

struct STCCell {
    char ch = ' ';
    unsigned rgb = xLIGHTGRAY;
};

// The whole screen, redrawn in full by every vRefresh() - simplest thing
// that works for a turn-based game with no real-time animation beyond the
// occasional vDelay()'d effect, and it sidesteps ever having to diff
// against what the terminal last actually showed.
std::vector<STCCell> stc_screen;

// Re-reads the terminal's size into size_x/size_y and matches the buffer to
// it. Falls back to a fixed 80x25 when the size cannot be had - on Windows
// because there is no real console behind stdout (mintty never gives a
// native Win32 program one, so GetConsoleScreenBufferInfo() simply fails),
// and anywhere else because stdout is not a terminal at all.
void QueryScreenSize()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hstdout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hstdout, &csbi)) {
        size_x = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        size_y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        size_x = 80;
        size_y = 25;
    }
#elif defined(__EMSCRIPTEN__)
    size_x = 80;
    size_y = 25;
#else
    winsize ws{};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        size_x = ws.ws_col;
        size_y = ws.ws_row;
    } else {
        size_x = 80;
        size_y = 25;
    }
#endif

    stc_screen.assign(static_cast<size_t>(size_x) * static_cast<size_t>(size_y), STCCell{});
}

#ifndef _WIN32

// The terminal as it was before the game started, put back by vFinit().
termios saved_termios{};
bool termios_saved = false;

// Set from the SIGWINCH handler and read by vGetch(), which is the one
// place the game is ever waiting on the terminal. Only a flag is set here:
// nothing else is safe to do from a signal handler.
volatile std::sig_atomic_t window_resized = 0;

extern "C" void OnWindowResize(int)
{
    window_resized = 1;
}

void LeaveRawMode();

// Keys have to arrive one at a time, unechoed, for any of this to work: the
// game reads a key per turn and draws the screen itself. This is what
// notcurses does for its own backend, and what conio.h's _getch() does on
// Windows without being asked.
void EnterRawMode()
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return;
    }

    termios raw = saved_termios;

    // No line discipline, no echo, no signals or flow control eaten on the
    // way in - but leave OPOST alone, so that a "\n" the game prints still
    // does what it means.
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | IEXTEN | ISIG);
    raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | INLCR | BRKINT | ISTRIP);

    // Blocking, one byte at a time: vGetch() does its own waiting where it
    // needs to, and vKbhit() asks poll() rather than the terminal.
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
        termios_saved = true;

        // vFinit() puts it back on the way out, but the game also leaves
        // through std::exit() in places, and a terminal left raw is a
        // terminal the player has to fix by hand.
        std::atexit(LeaveRawMode);
    }
}

void LeaveRawMode()
{
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
        termios_saved = false;
    }
}

#endif // !_WIN32

} // namespace

void vInit()
{
    std::filesystem::create_directory(vMakePath(HOME_DIR, ""));

#ifdef __EMSCRIPTEN__
    QueryScreenSize();
    be_init(size_x, size_y);
    vClrScr();
    return;
#endif

#ifdef _WIN32
    // The classic Windows console (conhost, behind cmd.exe/Windows Terminal)
    // only interprets the ANSI escapes below once this is turned on; mintty
    // (MSYS2's own terminal) already speaks them natively and simply leaves
    // a mode alone that it doesn't recognise on a handle it doesn't own.
    if (HANDLE hstdout = GetStdHandle(STD_OUTPUT_HANDLE); hstdout != INVALID_HANDLE_VALUE) {
        DWORD out_mode = 0;

        if (GetConsoleMode(hstdout, &out_mode)) {
            SetConsoleMode(hstdout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#else
    // Nothing to turn on: a unixoid terminal speaks ANSI already. What it
    // does need is to stop cooking the input, and to say when it is
    // resized - which conio.h had no way of reporting, so this backend
    // never handled a resize before.
    EnterRawMode();

    // Deliberately without SA_RESTART: a resize has to interrupt the read
    // the game is sitting in, or nothing would notice until the next key.
    struct sigaction sa{};
    sa.sa_handler = OnWindowResize;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, nullptr);
#endif

    QueryScreenSize();

    std::cout << stc::true_color;
    // Alternate screen buffer, so quitting leaves the terminal's own
    // scrollback exactly as it was - notcurses does the same - and hide the
    // hardware cursor; vXGotoXY() shows it again for text entry.
    std::cout << "\x1b[?1049h\x1b[?25l" << std::flush;

    vClrScr();
}

void vUpdateScreenSize()
{
    // On Windows nothing detects a live resize - conio.h has no such event
    // - so this only matters there if something calls it directly. Anywhere
    // else SIGWINCH brings us here through vGetch().
    QueryScreenSize();
}

void vClrScr()
{
    std::fill(stc_screen.begin(), stc_screen.end(), STCCell{});
}

void vFinit()
{
#ifdef __EMSCRIPTEN__
    be_finit();
    return;
#endif
    std::cout << stc::reset;
    std::cout << "\x1b[?25h\x1b[?1049l" << std::flush;

#ifndef _WIN32
    LeaveRawMode();
#endif
}

void vRefresh()
{
#ifdef __EMSCRIPTEN__
    {
        static std::vector<char> chars;
        static std::vector<unsigned> rgbs;
        chars.resize(stc_screen.size());
        rgbs.resize(stc_screen.size());

        for (size_t i = 0; i < stc_screen.size(); i++) {
            chars[i] = stc_screen[i].ch;
            rgbs[i] = stc_screen[i].rgb & 0xFFFFFF;
        }

        be_present(chars.data(), rgbs.data(), size_x, size_y);
        return;
    }
#endif
    std::ostringstream oss;
    oss << stc::true_color;
    oss << "\x1b[H";

    // A colour is only worth re-emitting when it actually changes - a run
    // of same-coloured cells (most of any given row, usually) costs one
    // escape sequence instead of one per cell.
    unsigned last_rgb = ~0u;

    for (int y = 0; y < size_y; y++) {
        oss << "\x1b[" << (y + 1) << ";1H";

        for (int x = 0; x < size_x; x++) {
            const STCCell& cell = stc_screen[static_cast<size_t>(y) * size_x + x];

            if (cell.rgb != last_rgb) {
                oss << stc::rgb_fg(static_cast<int>((cell.rgb >> 16) & 0xFF),
                                    static_cast<int>((cell.rgb >> 8) & 0xFF),
                                    static_cast<int>(cell.rgb & 0xFF));
                last_rgb = cell.rgb;
            }

            oss << cell.ch;
        }
    }

    oss << stc::reset;
    std::cout << oss.str() << std::flush;
}

void vPutCh(const int x, const int y, char ch, unsigned rgb)
{
    assert(x >= 0 && y >= 0 && x <= size_x && y <= size_y);

    if (x < 0 || y < 0 || x >= size_x || y >= size_y) {
        return;
    }

    STCCell& cell = stc_screen[static_cast<size_t>(y) * size_x + x];
    cell.ch = ch < ' ' ? ' ' : ch;
    cell.rgb = rgb;
};

char vTestCh(const int x, const int y)
{
    if (x < 0 || y < 0 || x >= size_x || y >= size_y) {
        return ' ';
    }

    return stc_screen[static_cast<size_t>(y) * size_x + x].ch;
}

void vPutCh(int x, int y, char ch)
{
    if (x < 0 || y < 0 || x >= size_x || y >= size_y) {
        return;
    }

    // No colour given: the cell keeps the one it already has.
    stc_screen[static_cast<size_t>(y) * size_x + x].ch = ch < ' ' ? ' ' : ch;
};

#else // notcurses

// The terminal. Owned here, created by vInit() and stopped by vFinit().
ncpp::NotCurses* nc = nullptr;
ncpp::Plane* screen = nullptr;

// vKbhit() has to take a key out of notcurses to know one is there, so
// it keeps it for the vGetch() that follows.
ncinput pending_key{};
bool has_pending_key = false;

void vInit()
{
    std::filesystem::create_directory(vMakePath(HOME_DIR, ""));

    notcurses_options opts{};
    opts.loglevel = NCLOGLEVEL_SILENT;
    opts.flags = NCOPTION_SUPPRESS_BANNERS;

    nc = new ncpp::NotCurses(opts);
    screen = nc->get_stdplane();

    unsigned rows = 0;
    unsigned cols = 0;
    screen->get_dim(&rows, &cols);

    size_x = static_cast<int>(cols);
    size_y = static_cast<int>(rows);

    // Every cell of the screen paints, including the blank ones: planes
    // parked underneath it (see vStore()) must never show through.
    uint64_t opaque_black = 0;
    ncchannels_set_fg_rgb(&opaque_black, xLIGHTGRAY);
    ncchannels_set_bg_rgb(&opaque_black, xBLACK);
    screen->set_base(" ", 0, opaque_black);

    nc->cursor_disable();

    vClrScr();
}

void vUpdateScreenSize()
{
    unsigned rows = 0;
    unsigned cols = 0;

    // refresh() repaints the terminal from the planes as they now stand
    // and reports the size it found; get_dim() is asked afterwards
    // because the standard plane is the one the game draws into and it
    // is that which has to agree with size_x/size_y.
    nc->refresh(&rows, &cols);
    screen->get_dim(&rows, &cols);

    size_x = static_cast<int>(cols);
    size_y = static_cast<int>(rows);
}

void vClrScr()
{
    screen->erase();
    screen->set_fg_rgb(xLIGHTGRAY);
    screen->set_bg_rgb(xBLACK);
}

void vFinit()
{
    if (nc) {
        nc->stop();
        delete nc;
        nc = nullptr;
        screen = nullptr;
    }
}

void vRefresh()
{
    nc->render();
}

void vPutCh(const int x, const int y, char ch, unsigned rgb)
{
    assert(x >= 0 && y >= 0 && x <= size_x && y <= size_y);
    // The plane holds what the screen will look like; notcurses works
    // out what actually has to be sent when vRefresh() renders it.
    screen->set_fg_rgb(rgb);
    screen->putc(y, x, ch < ' ' ? ' ' : ch);
};

char vTestCh(const int x, const int y)
{
    uint16_t stylemask = 0;
    uint64_t channels = 0;
    char ch = ' ';

    if (char* egc = screen->get_at(y, x, &stylemask, &channels)) {
        ch = egc[0];
        free(egc);
    }

    return ch;
}

void vPutCh(int x, int y, char ch)
{
    // No colour given: the cell keeps the one it already has.
    uint16_t stylemask = 0;
    uint64_t channels = 0;

    if (char* egc = screen->get_at(y, x, &stylemask, &channels)) {
        free(egc);
        screen->set_channels(channels);
    }

    screen->putc(y, x, ch < ' ' ? ' ' : ch);
};

#endif // !USE_NOTCURSES

void vGotoXY(int x, int y)
{
    cursor_pos_x = x;
    cursor_pos_y = y;
}

void vGetCursorPos(int* x, int* y)
{
    *x = cursor_pos_x;
    *y = cursor_pos_y;
}

int vGetS(char* s, const int buffer_size)
{
    int cx;
    int cy;
    vGetCursorPos(&cx, &cy);
    int buffer_pos = 0;
    strcpy(s, "");

    do {
        vXGotoXY(cx + strlen(s), cy);
        const int ch = vGetch();

        s[buffer_pos] = ch;

        if (ch == 13 || ch == 27) {
            s[buffer_pos] = 0;
            vHideCursor();

            if (ch == 13) {
                return 1;
            } else {
                return 0;
            }
        }

        // Terminals vary in which code their Backspace key actually sends
        // (ASCII BS or DEL) - accept both rather than just one, otherwise
        // the code the terminal doesn't happen to use gets inserted into
        // the string as a literal control character instead of deleting.
        if ((ch == KEY_BACKSPACE || ch == KEY_DEL) && buffer_pos > 0) {
            s[buffer_pos - 1] = ' ';
            s[buffer_pos] = 0;
            vGotoXY(cx, cy);
            vPutS(s);
            buffer_pos--;
        } else if (ch != KEY_BACKSPACE && ch != KEY_DEL) {
            buffer_pos++;
        }

        s[buffer_pos] = 0;
        vGotoXY(cx, cy);
        vPutS(s);
        vRefresh();
    } while (buffer_pos < buffer_size - 1);

    return 1;
}

void vDelay(const int n)
{
    // One animation step: the caller has already drawn the frame and called
    // vRefresh(), and this is how long it stays on screen before the next one
    // replaces it. n is milliseconds, as it was for the DOS build's delay()
    // - the only platform where this was ever implemented; the Windows branch
    // was empty from the 2003 import onwards, and Linux inherited that
    // emptiness. std::this_thread needs no #ifdef and behaves the same on every
    // platform this builds for.
    if (n <= 0) {
        return;
    }

#ifdef __EMSCRIPTEN__
    be_delay(n);
    return;
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}

#ifndef USE_NOTCURSES
#ifdef _WIN32

int vKbhit()
{
    return _kbhit();
}

int vGetch()
{
    // Same decoding the pre-notcurses Windows build used: an extended key
    // (arrows, Home/End, ...) arrives as a 0 or 0xE0 prefix byte followed by
    // a scancode, which is exactly what KEY_UP and its siblings in
    // engine/global.h are defined in terms of.
    int ch = _getch();

    if (ch == 0 || ch == 224) {
        ch = KEY_EXTENDED_CODE | _getch();
    }

    return ch;
}

#elif defined(__EMSCRIPTEN__)

int vKbhit()
{
    return be_kbhit();
}

int vGetch()
{
    return be_getkey();
}

#else // unixoid

namespace {

// How long to wait for the rest of an escape sequence before deciding that
// an Escape was simply pressed. A terminal sends the whole of a sequence at
// once, so anything still to come is already on its way; a person reaching
// for the next key cannot get there in this long.
constexpr int kEscapeDelayMs = 40;

// One byte read ahead and not yet handed out - see the Alt case in
// ReadKey() below.
int pending_byte = -1;

// Whether anything is waiting to be read. A negative wait blocks.
bool InputWaiting(const int timeout_ms)
{
    pollfd fd{};
    fd.fd = STDIN_FILENO;
    fd.events = POLLIN;

    while (true) {
        const int n = poll(&fd, 1, timeout_ms);

        if (n < 0 && errno == EINTR) {
            // A resize arrived. The caller checks for that itself, and
            // there may still be a key behind it.
            return false;
        }

        return n > 0;
    }
}

// One byte, or -1 if the terminal has nothing more to give.
int ReadByte()
{
    if (pending_byte >= 0) {
        const int b = pending_byte;
        pending_byte = -1;

        return b;
    }

    unsigned char c = 0;

    while (true) {
        const ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n == 1) {
            return c;
        }

        if (n < 0 && errno == EINTR) {
            // Interrupted by SIGWINCH; let the caller see the resize.
            return -1;
        }

        // End of input: nothing is ever coming again.
        return -1;
    }
}

// What the tail of an escape sequence means. `final` is the letter it ends
// on and `param` the number before it, if there was one - together they
// name the key in every sequence this game cares about.
int DecodeSequence(const int final, const int param)
{
    switch (final) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'E': return KEY_CENTER;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        default: break;
    }

    if (final == '~') {
        switch (param) {
            case 1: case 7: return KEY_HOME;
            case 4: case 8: return KEY_END;
            case 3:         return KEY_DEL;
            case 5:         return KEY_PGUP;
            case 6:         return KEY_PGDOWN;
            default: break;
        }
    }

    // Something this game has no use for - a function key, a mouse report.
    // Swallow it rather than handing back a byte of it as if it had been
    // typed.
    return -1;
}

} // namespace

int vKbhit()
{
    return (pending_byte >= 0 || InputWaiting(0)) ? 1 : 0;
}

int vGetch()
{
    while (true) {
        // Checked here because this is the one place the game ever waits
        // on the terminal, so it is the only place a resize can be noticed.
        if (window_resized) {
            window_resized = 0;
            vUpdateScreenSize();

            return KEY_RESIZE;
        }

        const int c = ReadByte();

        if (c < 0) {
            // Interrupted, or the input is finished. Loop round: either the
            // resize above is waiting, or there is nothing left and Escape
            // is the least surprising thing to keep answering with.
            if (window_resized) {
                continue;
            }

            return KEY_ESC;
        }

        if (c != 27) {
            // Some terminals send a line feed for Return; the game knows
            // only the carriage return.
            return c == 10 ? KEY_ENTER : c;
        }

        // An Escape, or the start of a sequence that stands for some other
        // key. Which it is depends on whether anything follows it at once.
        if (!InputWaiting(kEscapeDelayMs)) {
            return KEY_ESC;
        }

        const int lead = ReadByte();

        if (lead < 0) {
            return KEY_ESC;
        }

        if (lead != '[' && lead != 'O') {
            // A terminal writes Alt+key as an Escape and then the key.
            // Nothing here is bound to Alt, so this is somebody who pressed
            // Escape and reached for the next key before the terminal had
            // made up its mind. Hand the Escape back and keep the key for
            // the next call: losing either of them is how a swallowed
            // Escape used to leave the key behind it to answer a question
            // the player had asked to get out of.
            pending_byte = lead;

            return KEY_ESC;
        }

        // CSI or SS3: digits and separators, then a letter that says which
        // key it was.
        constexpr int PARAM_MAX = 9999;

        int param = 0;
        bool have_param = false;

        while (true) {
            const int b = ReadByte();

            if (b < 0) {
                return KEY_ESC;
            }

            if (b >= '0' && b <= '9') {
                // Every parameter this decodes is one or two digits. A
                // longer run is a terminal talking nonsense, or something
                // that is not a terminal at all, and is stopped here
                // rather than left to overflow: nothing downstream reads
                // a number this large, so pinning it loses nothing and
                // keeps the arithmetic defined.
                if (param <= PARAM_MAX / 10) {
                    param = param * 10 + (b - '0');
                }

                have_param = true;
                continue;
            }

            if (b == ';') {
                // A modifier follows, which nothing here reads; keep the
                // first number and drop the rest.
                param = have_param ? param : 0;
                continue;
            }

            if (const int key = DecodeSequence(b, param); key >= 0) {
                return key;
            }

            // Not a key this game knows. Wait for the next one rather than
            // returning rubbish.
            break;
        }
    }
}

#endif // _WIN32

#else // notcurses

int vKbhit()
{
    ncinput ni{};
    const struct timespec now = {0, 0};

    if (nc->get(&now, &ni) == 0) {
        return 0;
    }

    // Something was typed; hand it back on the next vGetch().
    pending_key = ni;
    has_pending_key = true;

    return 1;
}

int vGetch()
{
    // notcurses decodes the terminal's escape sequences itself, so what
    // arrives here is either a Unicode codepoint or one of its NCKEY_*
    // synthetic keys.
    ncinput ni{};

    while (true) {
        uint32_t key;

        if (has_pending_key) {
            ni = pending_key;
            key = ni.id;
            has_pending_key = false;
        } else {
            key = nc->get(true, &ni);
        }

        if (key == static_cast<uint32_t>(-1) || ni.evtype == NCTYPE_RELEASE) {
            continue;
        }

        // SIGWINCH arrives on the input queue like anything else. notcurses
        // has already resized its own planes by the time we see it; what is
        // left is to tell the game how much room it has and to get the old
        // picture back on the screen, which stops the display being left in
        // pieces if whoever is waiting here does not know how to lay itself
        // out again.
        if (key == NCKEY_RESIZE) {
            vUpdateScreenSize();

            return KEY_RESIZE;
        }

        switch (key) {
            case NCKEY_UP:        return KEY_UP;
            case NCKEY_DOWN:      return KEY_DOWN;
            case NCKEY_LEFT:      return KEY_LEFT;
            case NCKEY_RIGHT:     return KEY_RIGHT;
            case NCKEY_HOME:      return KEY_HOME;
            case NCKEY_END:       return KEY_END;
            case NCKEY_PGUP:      return KEY_PGUP;
            case NCKEY_PGDOWN:    return KEY_PGDOWN;
            case NCKEY_CENTER:    return KEY_CENTER;
            case NCKEY_ENTER:     return KEY_ENTER;
            case NCKEY_BACKSPACE: return KEY_BACKSPACE;
            case NCKEY_DEL:       return KEY_DEL;
            case NCKEY_ESC:       return KEY_ESC;
            default:
                break;
        }

        const bool ctrl_held = ncinput_ctrl_p(&ni);

        if (ctrl_held && key >= 'a' && key <= 'z') {
            return static_cast<int>(key - 'a' + 1);
        }

        if (ctrl_held && key >= 'A' && key <= 'Z') {
            return static_cast<int>(key - 'A' + 1);
        }

        if (key < 0x80) {
            return static_cast<int>(key);
        }
    }
}

#endif // !USE_NOTCURSES

int vXGetch(const char* ch_buf)
{
    size_t slen = strlen(ch_buf);

    while (true) {
        const int ch = vGetch();

        for (size_t i = 0; i < slen; i++) {
            if (ch == ch_buf[i]) {
                return ch;
            }
        }

        if (ch == KEY_ESC) {
            return 0;
        }
    }
}

#ifndef USE_NOTCURSES

void vXGotoXY(int x, int y)
{
#ifdef __EMSCRIPTEN__
    be_cursor(x, y);
    return;
#endif
    // Shows the hardware cursor at this position - vGetS() uses this to
    // give visible feedback while the player is typing, mirroring
    // ncpp::Plane::cursor_enable(), which both shows and moves it at once.
    std::cout << "\x1b[" << (y + 1) << ';' << (x + 1) << "H\x1b[?25h" << std::flush;
}

void vHideCursor()
{
#ifdef __EMSCRIPTEN__
    be_cursor(-1, -1);
    return;
#endif
    std::cout << "\x1b[?25l" << std::flush;
}

#else // notcurses

void vXGotoXY(int x, int y)
{
    nc->cursor_enable(y, x);
}

void vHideCursor()
{
    nc->cursor_disable();
}

#endif // !USE_NOTCURSES

namespace {

// The scheme in force. Avanor's own, which reproduces exactly the colours
// the screens were written with: labels and framing in brown, values in
// yellow, keys in cyan, everything else light gray.
const ColourScheme avanor_scheme = {
    "avanor",
    {
        xLIGHTGRAY,  // ROLE_TEXT
        xBROWN,      // ROLE_DECORATION
        xBROWN,      // ROLE_LABEL
        xYELLOW,     // ROLE_VALUE
        xYELLOW,     // ROLE_EMPHASIS
        xCYAN,       // ROLE_KEY
        xYELLOW,     // ROLE_SELECTOR
        xYELLOW,     // ROLE_WARNING

        xLIGHTGRAY,  // ROLE_PROGRESS_NONE
        xLIGHTGREEN, // ROLE_PROGRESS_BASIC
        xGREEN,      // ROLE_PROGRESS_SKILLED
        xYELLOW,     // ROLE_PROGRESS_EXPERT
        xLIGHTRED,   // ROLE_PROGRESS_MASTER
        xRED,        // ROLE_PROGRESS_SENIOR_MASTER
        xDARKGRAY,   // ROLE_PROGRESS_GRANDMASTER

        xBLACK,      // 0x1F is the escape byte - never resolved, never used

        xRED,        // ROLE_QUALITY_TERRIBLE
        xLIGHTRED,   // ROLE_QUALITY_POOR
        xLIGHTGRAY,  // ROLE_QUALITY_NEUTRAL
        xLIGHTGREEN, // ROLE_QUALITY_FAIR
        xYELLOW,     // ROLE_QUALITY_GOOD
        xWHITE,      // ROLE_QUALITY_PERFECT

        xLIGHTGRAY,  // ROLE_SEVERITY_MILD
        xYELLOW,     // ROLE_SEVERITY_NOTABLE
        xRED,        // ROLE_SEVERITY_SEVERE
        xDARKGRAY    // ROLE_SEVERITY_CRITICAL
    }
};

const ColourScheme* current_scheme = &avanor_scheme;

// Escape bytes 0-15 are the old sixteen colours, in their old order.
const unsigned legacy_palette[16] = {
    xBLACK, xBLUE, xGREEN, xCYAN, xRED, xMAGENTA, xBROWN, xLIGHTGRAY,
    xDARKGRAY, xLIGHTBLUE, xLIGHTGREEN, xLIGHTCYAN, xLIGHTRED, xLIGHTMAGENTA, xYELLOW, xWHITE
};

// What a role is called when it is written into a string. The order does
// not matter here; the names do.
const struct {
    const char* name;
    TextRole role;
} role_names[] = {
    {"TEXT", ROLE_TEXT},
    {"DECORATION", ROLE_DECORATION},
    {"LABEL", ROLE_LABEL},
    {"VALUE", ROLE_VALUE},
    {"EMPHASIS", ROLE_EMPHASIS},
    {"KEY", ROLE_KEY},
    {"SELECTOR", ROLE_SELECTOR},
    {"WARNING", ROLE_WARNING},

    {"PROGRESS_NONE", ROLE_PROGRESS_NONE},
    {"PROGRESS_BASIC", ROLE_PROGRESS_BASIC},
    {"PROGRESS_SKILLED", ROLE_PROGRESS_SKILLED},
    {"PROGRESS_EXPERT", ROLE_PROGRESS_EXPERT},
    {"PROGRESS_MASTER", ROLE_PROGRESS_MASTER},
    {"PROGRESS_SENIOR_MASTER", ROLE_PROGRESS_SENIOR_MASTER},
    {"PROGRESS_GRANDMASTER", ROLE_PROGRESS_GRANDMASTER},

    {"QUALITY_TERRIBLE", ROLE_QUALITY_TERRIBLE},
    {"QUALITY_POOR", ROLE_QUALITY_POOR},
    {"QUALITY_NEUTRAL", ROLE_QUALITY_NEUTRAL},
    {"QUALITY_FAIR", ROLE_QUALITY_FAIR},
    {"QUALITY_GOOD", ROLE_QUALITY_GOOD},
    {"QUALITY_PERFECT", ROLE_QUALITY_PERFECT},

    {"SEVERITY_MILD", ROLE_SEVERITY_MILD},
    {"SEVERITY_NOTABLE", ROLE_SEVERITY_NOTABLE},
    {"SEVERITY_SEVERE", ROLE_SEVERITY_SEVERE},
    {"SEVERITY_CRITICAL", ROLE_SEVERITY_CRITICAL}
};

// The macros in global.h spell these bytes out, because they must stay
// string literals for the compiler to fold them into their text.
static_assert(ROLE_TEXT == 0x10, "MSG_TEXT's escape byte no longer matches");
static_assert(ROLE_DECORATION == 0x11, "MSG_DECORATION's escape byte no longer matches");
static_assert(ROLE_LABEL == 0x12, "MSG_LABEL's escape byte no longer matches");
static_assert(ROLE_VALUE == 0x13, "MSG_VALUE's escape byte no longer matches");
static_assert(ROLE_EMPHASIS == 0x14, "MSG_EMPHASIS's escape byte no longer matches");
static_assert(ROLE_KEY == 0x15, "MSG_KEY's escape byte no longer matches");
static_assert(ROLE_SELECTOR == 0x16, "MSG_SELECTOR's escape byte no longer matches");
static_assert(ROLE_WARNING == 0x17, "MSG_WARNING's escape byte no longer matches");
static_assert(ROLE_PROGRESS_NONE == 0x18, "MSG_PROGRESS_NONE's escape byte no longer matches");
static_assert(ROLE_PROGRESS_GRANDMASTER == 0x1E, "MSG_PROGRESS_GRANDMASTER's escape byte no longer matches");
static_assert(ROLE_QUALITY_TERRIBLE == 0x20, "MSG_QUALITY_TERRIBLE's escape byte no longer matches");
static_assert(ROLE_QUALITY_PERFECT == 0x25, "MSG_QUALITY_PERFECT's escape byte no longer matches");
static_assert(ROLE_SEVERITY_MILD == 0x26, "MSG_SEVERITY_MILD's escape byte no longer matches");
static_assert(ROLE_SEVERITY_CRITICAL == 0x29, "MSG_SEVERITY_CRITICAL's escape byte no longer matches");

// No role may collide with the byte that introduces one.
static_assert(ROLE_COUNT > 0x1F && ROLE_QUALITY_TERRIBLE > 0x1F, "a role would be indistinguishable from the escape");

} // namespace

void SetColourScheme(const ColourScheme& scheme)
{
    current_scheme = &scheme;
}

std::string ExpandMarkup(const std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (size_t pos = 0; pos < text.size(); pos++) {
        if (text[pos] != '<') {
            out += text[pos];
            continue;
        }

        // "<<" is a '<' that means itself.
        if (pos + 1 < text.size() && text[pos + 1] == '<') {
            out += '<';
            pos++;
            continue;
        }

        const size_t close = text.find('>', pos + 1);

        if (close == std::string_view::npos) {
            out += text[pos];
            continue;
        }

        const std::string_view name = text.substr(pos + 1, close - pos - 1);

        // <#rrggbb>: a colour given outright.
        if (name.size() == 7 && name[0] == '#'
            && name.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string_view::npos) {
            out += RGB_ESCAPE;
            out += name.substr(1);
            pos = close;
            continue;
        }

        bool known = false;

        for (const auto& [role_name, role] : role_names) {
            if (name == role_name) {
                out += '\x1F';
                out += static_cast<char>(role);
                pos = close;
                known = true;
                break;
            }
        }

        // Not one of ours - a creature's name, a bit of punctuation -
        // so it stays as written.
        if (!known) {
            out += text[pos];
        }
    }

    return out;
}

unsigned DimRGB(const unsigned rgb, const int percent)
{
    const unsigned r = ((rgb >> 16) & 0xFF) * percent / 100;
    const unsigned g = ((rgb >> 8) & 0xFF) * percent / 100;
    const unsigned b = (rgb & 0xFF) * percent / 100;

    return (r << 16) | (g << 8) | b;
}

void SetRememberedBrightness(const int percent)
{
    remembered_brightness = std::clamp(percent, 0, 100);
}

namespace {

// A number in [0, 1) fixed by where the cell is and which of the three
// qualities is asking. Not vRand(): the same cell has to come back with
// the same answer every time it is drawn, or the ground would crawl as
// the hero walks over it.
float JitterAt(const int x, const int y, const unsigned quality)
{
    unsigned h = static_cast<unsigned>(x) * 374761393u
               + static_cast<unsigned>(y) * 668265263u
               + quality * 2246822519u;

    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;

    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// A number in [-1, 1), for the qualities that move either way.
float SignedJitterAt(const int x, const int y, const unsigned quality)
{
    return JitterAt(x, y, quality) * 2.0F - 1.0F;
}

struct HSV {
    float h;    // degrees, [0, 360)
    float s;    // [0, 1]
    float v;    // [0, 1]
};

HSV ToHSV(const unsigned r, const unsigned g, const unsigned b)
{
    const float rf = static_cast<float>(r) / 255.0F;
    const float gf = static_cast<float>(g) / 255.0F;
    const float bf = static_cast<float>(b) / 255.0F;

    const float top = std::max({rf, gf, bf});
    const float bottom = std::min({rf, gf, bf});
    const float span = top - bottom;

    float hue = 0.0F;

    if (span > 0.0F) {
        if (top == rf) {
            hue = 60.0F * (gf - bf) / span;
        } else if (top == gf) {
            hue = 60.0F * (2.0F + (bf - rf) / span);
        } else {
            hue = 60.0F * (4.0F + (rf - gf) / span);
        }
    }

    if (hue < 0.0F) {
        hue += 360.0F;
    }

    return {hue, top > 0.0F ? span / top : 0.0F, top};
}

unsigned FromHSV(const HSV& c)
{
    const float sector = c.h / 60.0F;
    const int part = static_cast<int>(sector) % 6;
    const float frac = sector - static_cast<float>(static_cast<int>(sector));

    const float p = c.v * (1.0F - c.s);
    const float q = c.v * (1.0F - c.s * frac);
    const float t = c.v * (1.0F - c.s * (1.0F - frac));

    float rf = 0.0F;
    float gf = 0.0F;
    float bf = 0.0F;

    switch (part) {
        case 0:  rf = c.v; gf = t;   bf = p;   break;
        case 1:  rf = q;   gf = c.v; bf = p;   break;
        case 2:  rf = p;   gf = c.v; bf = t;   break;
        case 3:  rf = p;   gf = q;   bf = c.v; break;
        case 4:  rf = t;   gf = p;   bf = c.v; break;
        default: rf = c.v; gf = p;   bf = q;   break;
    }

    const auto byte = [](const float f) {
        return static_cast<unsigned>(std::lround(std::clamp(f, 0.0F, 1.0F) * 255.0F));
    };

    return (byte(rf) << 16) | (byte(gf) << 8) | byte(bf);
}

} // namespace

unsigned JitterRGB(const unsigned rgb, const int x, const int y)
{
    if (TileJitter() <= 0 && TileHueJitter() <= 0 && TileSaturationJitter() <= 0) {
        return rgb;
    }

    HSV c = ToHSV((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

    if (c.v <= 0.0F) {
        return rgb;     // black: nothing to vary
    }

    // Hue and saturation are what make a stand of trees read as a wood
    // rather than as one colour stamped out a hundred times: some of them
    // a little yellower, some greyer, some deeper. The hue turns by a few
    // degrees at most, so a green stays a green and a blue stays a blue.
    // Scaling the channels apart instead would not turn a colour, it
    // would replace it - water would wander off through lavender - and it
    // would do so unevenly, since a colour with one channel to scale can
    // only change brightness however far the factors are pushed.
    //
    // Saturation is scaled rather than offset, which is what keeps the
    // greys grey without a special case: a grey has no saturation, and
    // nothing times a factor is still nothing.
    c.h += static_cast<float>(TileHueJitter()) * SignedJitterAt(x, y, 1);
    c.h = std::fmod(c.h + 360.0F, 360.0F);

    c.s = std::clamp(c.s * (1.0F + static_cast<float>(TileSaturationJitter()) / 100.0F
                                       * SignedJitterAt(x, y, 2)),
                     0.0F, 1.0F);

    // Brightness keeps the shape it had: a factor either side of one, but
    // never far enough for the brightest channel to reach 255 and clamp,
    // since a channel that stops rising while the others carry on is hue
    // drift by another route. A colour already at full value can only
    // darken.
    const float low = 1.0F - static_cast<float>(TileJitter()) / 100.0F;
    const float high = std::min(low > 0.0F ? 1.0F / low : 1.0F, 1.0F / c.v);

    c.v = std::clamp(c.v * (low + (std::max(high, low) - low) * JitterAt(x, y, 0)), 0.0F, 1.0F);

    return FromHSV(c);
}

void SetTileJitter(const int percent, const int hue_degrees, const int saturation_percent)
{
    tile_jitter = std::clamp(percent, 0, 100);
    tile_hue_jitter = std::clamp(hue_degrees, 0, 180);
    tile_saturation_jitter = std::clamp(saturation_percent, 0, 100);
}

int TileJitter()
{
    return tile_jitter;
}

int TileHueJitter()
{
    return tile_hue_jitter;
}

int TileSaturationJitter()
{
    return tile_saturation_jitter;
}

int RememberedBrightness()
{
    return remembered_brightness;
}

unsigned PaletteRGB(const unsigned char slot)
{
    return legacy_palette[slot & 0x0F];
}

unsigned ResolveColour(const unsigned char escape_byte)
{
    if (escape_byte < ROLE_FIRST) {
        return PaletteRGB(escape_byte);
    }

    if (escape_byte >= ROLE_COUNT) {
        return xLIGHTGRAY;
    }

    return current_scheme->role[escape_byte - ROLE_FIRST];
}

void vSetAttr(const unsigned rgb)
{
    current_attr = rgb;
}

void vClrEol()
{
    while (cursor_pos_x < size_x) {
        vPutCh(cursor_pos_x, cursor_pos_y, ' ');
        cursor_pos_x++;
    }
}

// (31, x) - change attr
void vPutS(const char* s)
{
    // Roles may be written into any string the game prints; they become
    // escape bytes here, on the way to the screen.
    const std::string expanded = ExpandMarkup(s ? s : "");

    s = expanded.c_str();

    if (cursor_pos_y >= size_y) {
        cursor_pos_x = cursor_pos_y = 0;
    }

    while (true) {
        switch (const char ch = *s++) {
            case 0 :
                return;

            case 31 :
                vSetAttr(ResolveColour(static_cast<unsigned char>(*s++)));
                break;

            case RGB_ESCAPE : {
                // Six hex digits follow, put there by ExpandMarkup(), so
                // they are known to be six hex digits.
                unsigned rgb = 0;

                for (int i = 0; i < 6 && *s; i++) {
                    const char digit = *s++;

                    rgb = rgb * 16 + static_cast<unsigned>(digit <= '9' ? digit - '0'
                                                                       : (digit | 0x20) - 'a' + 10);
                }

                vSetAttr(rgb);
                break;
            }

            case 13 :
            case '\n' :
                cursor_pos_x = 0;

                if (++cursor_pos_y >= size_y) {
                    cursor_pos_y = 0;
                }

                break;

            default : {
                // Past the right edge the character is dropped, not
                // wrapped onto the next line. Several screens are laid
                // out at fixed columns for a terminal at least eighty
                // wide - the status line is - and on a narrower one
                // wrapping pushed everything below them down and took
                // the bottom of the screen round to the top. A line too
                // long for the terminal is now simply cut short.
                //
                // Line breaks written into the string still break the
                // line: those are the cases above.
                // Off the top or the left as well as off the right: on a
                // terminal only a couple of rows tall, a screen that puts
                // its status line three rows up from the bottom is
                // addressing a negative row.
                if (cursor_pos_x >= 0 && cursor_pos_x < size_x
                    && cursor_pos_y >= 0 && cursor_pos_y < size_y) {
                    vPutCh(cursor_pos_x, cursor_pos_y, ch, current_attr);
                }

                cursor_pos_x++;
            }
        }
    }
}

void vFPutS(std::ofstream &file, std::string_view s)
{
    for (std::size_t pos = 0; pos < s.size(); ++pos) {
        if (s[pos] == '\x1F') {
            // Escape sequence: skip byte 31 and 1 following byte
            ++pos;
            continue;
        }

        if (s[pos] == RGB_ESCAPE) {
            pos += RGB_ESCAPE_LENGTH - 1;
            continue;
        }
        file << s[pos];
    }
}

static unsigned long randx;

void vRandSeed(const unsigned long seed)
{
    randx = seed;
}

#define X_RAND_A   0x000343FDUL
#define X_RAND_B   0x00269EC3UL
#define X_RAND_MAX 0x7FFFFFFFUL

long vRand()
{
    randx = randx * X_RAND_A + X_RAND_B;
    unsigned long randhigh = randx & 0x7FFF0000;
    randx = randx * X_RAND_A + X_RAND_B;
    unsigned long randlow = randx >> 16;
    return randhigh | randlow;
}

long vRand(unsigned long n)
{
    assert(n > 0);

    if (n > 0x8000) {
        assert(n <= X_RAND_MAX + 1);
        randx = randx * X_RAND_A + X_RAND_B;
        unsigned long randhigh = randx & 0x7FFF0000;
        randx = randx * X_RAND_A + X_RAND_B;
        unsigned long randlow = randx >> 16;
        return (randhigh | randlow) % n;
    } else {
        randx = randx * X_RAND_A + X_RAND_B;
        return (randx >> 16) % n;
    }
}

#ifndef USE_NOTCURSES

V_BUFFER::V_BUFFER()
{
}

V_BUFFER::~V_BUFFER()
{
    delete static_cast<std::vector<STCCell>*>(saved);
}

void vStore(const V_BUFFER* buf)
{
    delete static_cast<std::vector<STCCell>*>(buf->saved);
    const_cast<V_BUFFER*>(buf)->saved = new std::vector<STCCell>(stc_screen);
}

void vRestore(const V_BUFFER* buf)
{
    const auto* saved = static_cast<std::vector<STCCell>*>(buf->saved);

    // A screen saved before the terminal changed shape is of no use - see
    // the notcurses version of this function for why it is safe to just
    // clear and let whoever asked for the restore draw itself again.
    if (!saved || saved->size() != stc_screen.size()) {
        vClrScr();

        return;
    }

    stc_screen = *saved;
}

#else // notcurses

V_BUFFER::V_BUFFER()
{
}

V_BUFFER::~V_BUFFER()
{
    if (saved) {
        ncplane_destroy(static_cast<ncplane*>(saved));
    }
}

void vStore(const V_BUFFER* buf)
{
    // A saved screen is a copy of the plane; putting it back is a blit
    // from that copy, which is what notcurses' own plane machinery does.
    if (buf->saved) {
        ncplane_destroy(static_cast<ncplane*>(buf->saved));
    }

    ncplane* copy = ncplane_dup(*screen, nullptr);

    // ncplane_dup() stacks the copy immediately above its original, where
    // it would hide everything drawn next; it is only storage, so it goes
    // to the bottom, under an opaque screen.
    ncplane_move_bottom(copy);

    const_cast<V_BUFFER*>(buf)->saved = copy;
}

void vRestore(const V_BUFFER* buf)
{
    if (!buf->saved) {
        return;
    }

    auto* copy = static_cast<ncplane*>(buf->saved);

    unsigned saved_rows = 0;
    unsigned saved_cols = 0;
    ncplane_dim_yx(copy, &saved_rows, &saved_cols);

    // A screen saved before the terminal changed shape is of no use: it
    // was laid out for a size that is gone, and blitting it back would
    // put an eighty-column picture in the corner of a wider window, or
    // leave the parts the smaller copy never covered showing whatever
    // happened to be under them. Clear instead and let whoever asked for
    // the restore draw itself again - which every screen does on the next
    // turn of its own loop, having been told about the resize.
    if (static_cast<int>(saved_rows) != size_y || static_cast<int>(saved_cols) != size_x) {
        vClrScr();

        return;
    }

    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            uint16_t stylemask = 0;
            uint64_t channels = 0;

            if (char* egc = ncplane_at_yx(copy, y, x, &stylemask, &channels)) {
                screen->set_channels(channels);
                screen->putc(y, x, egc[0] ? egc[0] : ' ');
                free(egc);
            }
        }
    }
}

#endif // !USE_NOTCURSES

namespace {

// Where the running executable sits, or nothing if the system will not
// say. Not argv[0], which is whatever the caller felt like passing.
std::filesystem::path ExecutableDir()
{
    std::error_code ec;

#if defined(_WIN32)
    char path[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, path, sizeof(path));

    if (len == 0 || len == sizeof(path)) {
        return {};
    }

    return std::filesystem::path(std::string(path, len)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::string path(size, '\0');

    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }

    path.resize(std::strlen(path.c_str()));

    // Through weakly_canonical, because the answer may be a symlink or
    // carry /./ segments, and ../Resources below has to mean what it says.
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(path, ec);

    return ec ? std::filesystem::path(path).parent_path() : resolved.parent_path();
#else
    const std::filesystem::path exe =
        std::filesystem::read_symlink("/proc/self/exe", ec);

    return ec ? std::filesystem::path() : exe.parent_path();
#endif
}

// Whether a directory is the game's - the world is the thing it cannot
// start without, so that is what is looked for.
bool HoldsTheWorld(const std::filesystem::path& dir)
{
    std::error_code ec;

    return !dir.empty()
        && std::filesystem::exists(dir / "world" / "init.lua", ec);
}

} // namespace

// Finds what was installed with the game and steps into it.
//
// Everything the game reads of itself is named relative to where it is
// running from - "./world/init.lua" here, "manual/..." in the manual, and
// six-and-twenty dofile("./world/...") inside init.lua itself. Rather
// than teach each of them where the game lives, the game goes there.
//
// The working directory is tried first so that running from a source tree
// keeps behaving exactly as it always has. Then beside the executable,
// which is how an unpacked release is laid out; then, on macOS only,
// ../Resources, which is where an application bundle keeps such things.
//
// Last is whatever DATA_DIR was compiled in, and that is the one a Linux
// package uses: an RPM or a .deb puts the binary in /usr/bin and the data
// in /usr/share/avanor, so nothing beside the executable is the answer
// there - the path has to be built in.
//
// Finding nothing is reported but not treated as fatal: loading the world
// fails immediately afterwards and names the file, which is the clearer
// error of the two - this only adds where it looked, which is what a
// half-unpacked copy needs said.
void vEnterDataDir()
{
    std::error_code ec;
    const std::filesystem::path here = std::filesystem::current_path(ec);
    const std::filesystem::path exe_dir = ExecutableDir();

    const std::filesystem::path candidates[] = {
        ec ? std::filesystem::path(".") : here,
        exe_dir,
#ifdef __APPLE__
        exe_dir.empty() ? std::filesystem::path() : exe_dir / ".." / "Resources",
#endif
        std::filesystem::path(DATA_DIR),
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (!HoldsTheWorld(candidate)) {
            continue;
        }

        std::filesystem::current_path(candidate, ec);

        return;
    }

    // Nothing left to do but say where it looked. Loading the world will
    // fail in a moment and complain about the file; this is the part that
    // tells somebody with a half-unpacked copy what went wrong.
    std::cerr << "avanor: found no world to load. Looked in:" << std::endl;

    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty()) {
            std::cerr << "  " << std::filesystem::weakly_canonical(candidate, ec).string()
                      << std::endl;
        }
    }
}

std::string vMakePath(std::string_view prefix, std::string_view filename)
{
    std::string path_buffer;

    if (prefix[0] == '~') {
        // HOME is unset for a MinGW build run outside its MSYS2 shell (e.g.
        // launched straight from Explorer); USERPROFILE is Windows' own
        // equivalent and is always set there.
        const char* home = getenv("HOME");

        if (!home) {
            home = getenv("USERPROFILE");
        }

        path_buffer.assign(home ? home : ".")
            .append(prefix.substr(1))
            .append(filename);
    } else {
        path_buffer.append(prefix)
            .append(filename);
    }

    return path_buffer;
}
