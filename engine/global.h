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

#ifndef GLOBAL_H
#define GLOBAL_H

#include <fstream>
#include <string_view>

#include <sol/forward.hpp>

// Directory for common data files (game manual, hiscore, ...)
#ifndef DATA_DIR
    #define DATA_DIR "./"
#endif

// Directory for private data files (user settings, saved games, ...)
#if defined(__EMSCRIPTEN__) && !defined(HOME_DIR)
    // RVIP: the page mounts its IndexedDB folder here (RvipApp.dir).
    #define HOME_DIR "/avanor/"
#endif
#ifndef HOME_DIR
    #define HOME_DIR "~/.avanor/"
#endif

// The terminal is driven by plain ANSI escape sequences on every platform
// this builds for - see engine/global.cpp, which carries the Windows and
// the unixoid halves of that backend side by side. Building with
// notcurses=1 swaps in notcurses instead.
#include <cstring>
#define stricmp(a, b) strcasecmp(a, b)
#define strnicmp(a, b, n) strncasecmp(a, b, n)

#include <cmath>
#include <cassert>

extern int size_x;
extern int size_y;

extern unsigned long total_cr;
extern unsigned long total_it;
extern unsigned long cr_kiled;
extern unsigned long cr_died;

//global flags (users preferences)
extern int __animation_flag; // 0 - none, other - delay milliseconds

// The sixteen colours Avanor was written in, as the colours they were
// always meant to be rather than as terminal palette slots: brown is
// brown here, not "yellow, but dim". A value IS an RGB triple, so
// anything holding an xColor - a tile, an item, a creature - already
// holds a colour a terminal can draw exactly.
enum xColor {
    xBLACK        = 0x000000,
    xBLUE         = 0x0000C4,
    xGREEN        = 0x00A400,
    xCYAN         = 0x00A6A6,
    xRED          = 0xC00000,
    xMAGENTA      = 0xA800A8,
    xBROWN        = 0x8B5A2B,
    xLIGHTGRAY    = 0xC0C0C0,
    xDARKGRAY     = 0x707070,
    xLIGHTBLUE    = 0x5A8BFF,
    xLIGHTGREEN   = 0x50E050,
    xLIGHTCYAN    = 0x50E0E0,
    xLIGHTRED     = 0xFF5A5A,
    xLIGHTMAGENTA = 0xFF60FF,
    xYELLOW       = 0xFFD24A,
    xWHITE        = 0xFFFFFF
};

// Registers this enum as the Lua table xColor.MEMBER (e.g. xColor.xBLUE)
void RegisterColorEnum(sol::state_view& lua);

#define MSG_BLACK          "\x1F\x00"
#define MSG_BLUE           "\x1F\x01"
#define MSG_GREEN          "\x1F\x02"
#define MSG_CYAN           "\x1F\x03"
#define MSG_RED            "\x1F\x04"
#define MSG_MAGENTA        "\x1F\x05"
#define MSG_BROWN          "\x1F\x06"
#define MSG_LIGHTGRAY      "\x1F\x07"
#define MSG_DARKGRAY       "\x1F\x08"
#define MSG_LIGHTBLUE      "\x1F\x09"
#define MSG_LIGHTGREEN     "\x1F\x0A"
#define MSG_LIGHTCYAN      "\x1F\x0B"
#define MSG_LIGHTRED       "\x1F\x0C"
#define MSG_LIGHTMAGENTA   "\x1F\x0D"
#define MSG_YELLOW         "\x1F\x0E"
#define MSG_WHITE          "\x1F\x0F"

// What a piece of text IS, rather than what colour it happens to be.
// The colour comes from the current scheme, so a game can be recoloured
// - or given a high-contrast reading - without touching a single string.
//
// The escape byte's values 0-15 stay literal xColors, which is what
// SCOLOR() and anything drawing an object in its own colour still emits;
// roles start above them.
enum TextRole {
    ROLE_FIRST = 16,

    // Ordinary body text - the colour everything falls back to.
    ROLE_TEXT = ROLE_FIRST,

    // Frames, separators, brackets and the punctuation between values:
    // the furniture a screen is drawn with.
    ROLE_DECORATION,

    // The name of a thing being shown - field names, column headings.
    ROLE_LABEL,

    // The thing itself: numbers, names, measurements.
    ROLE_VALUE,

    // A line meant to stand out from the ones around it.
    ROLE_EMPHASIS,

    // A key the player can press, named in running text.
    ROLE_KEY,

    // The letter that picks an entry out of a list or menu.
    ROLE_SELECTOR,

    // Something the player should notice before carrying on.
    ROLE_WARNING,

    // How far along a ladder of proficiency something is - a skill's
    // mastery, a school of magic. The rungs are the game's, not the
    // engine's; a game with fewer of them simply leaves some unused.
    ROLE_PROGRESS_NONE,
    ROLE_PROGRESS_BASIC,
    ROLE_PROGRESS_SKILLED,
    ROLE_PROGRESS_EXPERT,
    ROLE_PROGRESS_MASTER,
    ROLE_PROGRESS_SENIOR_MASTER,
    ROLE_PROGRESS_GRANDMASTER,

    // 0x1F is the escape byte itself, so no role may be given that
    // value - the ladder above ends at 0x1E and the next one starts past
    // it. The static_asserts in global.cpp hold everything to its byte.
    // How good something is - a resistance, standing with a god.
    ROLE_QUALITY_TERRIBLE = 0x20,
    ROLE_QUALITY_POOR,
    ROLE_QUALITY_NEUTRAL,
    ROLE_QUALITY_FAIR,
    ROLE_QUALITY_GOOD,
    ROLE_QUALITY_PERFECT,

    // How bad it has got - a wound, hunger, a condition.
    ROLE_SEVERITY_MILD,
    ROLE_SEVERITY_NOTABLE,
    ROLE_SEVERITY_SEVERE,
    ROLE_SEVERITY_CRITICAL,

    ROLE_COUNT
};

// The literals below have to stay string literals, so that the compiler
// keeps folding MSG_LABEL "Name:" into one string - hence the hand-written
// escape bytes, checked against the enum in global.cpp.
#define MSG_TEXT           "\x1F\x10"
#define MSG_DECORATION     "\x1F\x11"
#define MSG_LABEL          "\x1F\x12"
#define MSG_VALUE          "\x1F\x13"
#define MSG_EMPHASIS       "\x1F\x14"
#define MSG_KEY            "\x1F\x15"
#define MSG_SELECTOR       "\x1F\x16"
#define MSG_WARNING        "\x1F\x17"

#define MSG_PROGRESS_NONE          "\x1F\x18"
#define MSG_PROGRESS_BASIC         "\x1F\x19"
#define MSG_PROGRESS_SKILLED       "\x1F\x1A"
#define MSG_PROGRESS_EXPERT        "\x1F\x1B"
#define MSG_PROGRESS_MASTER        "\x1F\x1C"
#define MSG_PROGRESS_SENIOR_MASTER "\x1F\x1D"
#define MSG_PROGRESS_GRANDMASTER   "\x1F\x1E"

#define MSG_QUALITY_TERRIBLE  "\x1F\x20"
#define MSG_QUALITY_POOR      "\x1F\x21"
#define MSG_QUALITY_NEUTRAL   "\x1F\x22"
#define MSG_QUALITY_FAIR      "\x1F\x23"
#define MSG_QUALITY_GOOD      "\x1F\x24"
#define MSG_QUALITY_PERFECT   "\x1F\x25"

#define MSG_SEVERITY_MILD     "\x1F\x26"
#define MSG_SEVERITY_NOTABLE  "\x1F\x27"
#define MSG_SEVERITY_SEVERE   "\x1F\x28"
#define MSG_SEVERITY_CRITICAL "\x1F\x29"

// The colour each role is drawn in. Swap the whole table to recolour the
// game; see global.cpp for the one Avanor ships with.
struct ColourScheme {
    const char* name;

    // An RGB value per role. xColor's members are RGB values too, so a
    // scheme may name them or write its own triples.
    unsigned role[ROLE_COUNT - ROLE_FIRST];
};

void SetColourScheme(const ColourScheme& scheme);

// The colour an escape byte asks for: literal below ROLE_FIRST, and the
// scheme's answer at or above it.
unsigned ResolveColour(unsigned char escape_byte);

// The colour behind one of the sixteen legacy escape bytes, which is
// what MSG_BROWN and its remaining siblings still emit.
unsigned PaletteRGB(unsigned char slot);

// The same colour at a fraction of its brightness: DimRGB(c, 80) is four
// fifths as bright. Used for ground the hero remembers but cannot see.
unsigned DimRGB(unsigned rgb, int percent);

// How bright remembered ground is drawn, as a percentage of what it
// looks like in plain sight. Set by the world script.
void SetRememberedBrightness(int percent);
[[nodiscard]] int RememberedBrightness();

// The same colour, nudged a little lighter or darker, so that a field of
// one tile type reads as ground rather than as a flat wash. Which way it
// is nudged depends only on x and y, so a place keeps the same face for
// as long as it holds the same tile - walking away and back, or saving
// and loading, changes nothing.
//
// Only the brightness moves: all three channels are scaled together and
// none is allowed to clamp, so the hue and the saturation come back
// exactly as they went in. A grey stays on the grey ramp, and water stays
// the colour of water. Black has nothing to scale and is returned as is.
unsigned JitterRGB(unsigned rgb, int x, int y);

// How far JitterRGB() may stray: brightness either side of the tile's own,
// per cent; hue, in degrees of turn; and saturation, per cent of what the
// colour has. All three zero gives back the flat colours. Set by the world
// script.
void SetTileJitter(int percent, int hue_degrees = 12, int saturation_percent = 15);
[[nodiscard]] int TileJitter();
[[nodiscard]] int TileHueJitter();
[[nodiscard]] int TileSaturationJitter();

// Turns the roles written into a string - "<LABEL>Name:<VALUE> Deus" -
// into the escape bytes the screen is painted from. Everything the game
// prints goes through this, so a role can be written wherever text is,
// including from Lua.
//
// A name the game does not know is left exactly as it stands, so an item
// called <pickaxe> reads as itself rather than disappearing; "<<" is how
// a string says a single '<' that must survive.
std::string ExpandMarkup(std::string_view text);

// A colour written into a string by value rather than by name, for text
// that takes its colour from something in the world: vPutS("that " +
// SCOLOR(monster->color) + "cyclops" ). It is markup like any other, and
// becomes the escape below when the text is drawn.
std::string SCOLOR(unsigned rgb);

// Roles and the sixteen palette slots travel as 0x1F plus one byte. A
// colour of its own needs more room, so it travels as 0x1E followed by
// six hex digits - text, so that no part of it can be a NUL and cut a
// C string short.
constexpr char RGB_ESCAPE = 0x1E;
constexpr int RGB_ESCAPE_LENGTH = 7;

// A screen put aside while something is drawn over it - a menu, a
// character sheet - and blitted back afterwards. What it holds is the
// backend's business: a copy of the cell grid by default, a duplicate of
// the standard plane made by notcurses itself under notcurses=1.
struct V_BUFFER {
    void* saved = nullptr;    // the backend's own, see engine/global.cpp

    V_BUFFER();
    ~V_BUFFER();
};


#undef KEY_EXTENDED_CODE
#undef KEY_ESC
#undef KEY_UP
#undef KEY_DOWN
#undef KEY_LEFT
#undef KEY_RIGHT
#undef KEY_HOME
#undef KEY_END
#undef KEY_PGUP
#undef KEY_PGDOWN
#undef KEY_CENTER

#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_DEL

#undef KEY_CTRL_Z
#undef KEY_UNKNOWN

#define KEY_EXTENDED_CODE 0x8000
#define KEY_ESC           (27)
#define KEY_UP            (KEY_EXTENDED_CODE | 72)
#define KEY_DOWN          (KEY_EXTENDED_CODE | 80)
#define KEY_LEFT          (KEY_EXTENDED_CODE | 75)
#define KEY_RIGHT         (KEY_EXTENDED_CODE | 77)
#define KEY_HOME          (KEY_EXTENDED_CODE | 71)
#define KEY_END           (KEY_EXTENDED_CODE | 79)
#define KEY_PGUP          (KEY_EXTENDED_CODE | 73)
#define KEY_PGDOWN        (KEY_EXTENDED_CODE | 81)
#define KEY_CENTER        (KEY_EXTENDED_CODE | 76)

#define KEY_ENTER         13
#define KEY_BACKSPACE     8
#define KEY_DEL           127

#define KEY_CTRL_D		  4
#define KEY_CTRL_F		  6

// Also what Tab sends - the two are one byte in a terminal.
#define KEY_CTRL_I		  9
#define KEY_CTRL_O		  15
#define KEY_CTRL_T		  20
#define KEY_CTRL_W		  23
#define KEY_CTRL_Z		  26

// The terminal changed shape. vGetch() hands this back so that whatever
// screen is waiting for a key can lay itself out again; size_x and size_y
// have already been brought up to date by the time it arrives.
#define KEY_RESIZE        (KEY_EXTENDED_CODE | 90)

#define KEY_UNKNOWN       0xFFFF

// Brings size_x/size_y back in line with the terminal and repaints what
// is currently on screen at the new size. Called for you when vGetch()
// sees a resize; the screen still has to lay itself out again to make use
// of the new room.
void vUpdateScreenSize();

void vClrScr();
void vInit();
void vFinit();
void vRefresh();
void vPutCh(int x, int y, char ch);
void vPutCh(int x, int y, char ch, unsigned rgb);
char vTestCh(int x, int y);
void vPutS(const char* s);

// std::string migration helper
inline void vPutS(std::string_view s) {
    vPutS(s.data());
}

void vFPutS(std::ofstream &file, std::string_view s);
int vGetS(char* s, int buffer_size);
void vClrEol();
void vGotoXY(int x, int y);
void vXGotoXY(int x, int y);
void vGetCursorPos(int* x, int* y);
void vSetAttr(unsigned rgb);
void vDelay(int n);
int vKbhit();
int vGetch();
int vXGetch(const char* ch_buf);
void vStore(const V_BUFFER * buf);
void vRestore(const V_BUFFER * buf);
void vHideCursor();

void vRandSeed(unsigned long seed);
long vRand();
long vRand(unsigned long n);

std::string vMakePath(std::string_view prefix, std::string_view filename);

// Steps into the directory the game was installed into, so that the paths
// it reads of itself - world/, manual/ - mean what they say wherever it
// was started from. Call it before anything reads a file. See the comment
// on the definition for the order it looks in.
void vEnterDataDir();

#endif
