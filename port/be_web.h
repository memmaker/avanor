// RVIP browser frontend for Avanor: the terminal backend in
// engine/global.cpp hands its cell buffer and its key reads to these when
// built with Emscripten. be_web.cpp splits the screen into the page's
// windows (RVIP W0): map (whole level, from XMap::Put), status rows
// (XCreature::PutStatus), messages (XMsgWin), prompt (message rows) and a
// pop-up for everything drawn over the map or on a cleared screen.
#pragma once

#include <string>

void be_init(int cols, int rows);
void be_finit();
void be_present(const char* chars, const unsigned* rgbs, int cols, int rows);
void be_cursor(int x, int y);
int be_kbhit();
int be_getkey();
void be_delay(int ms);

// vStore / vRestore / vClrScr: what is on screen over the map.
void be_store(const char* chars, const unsigned* rgbs, int n);
void be_restore();
void be_cleared();
// Screen rows y0 .. y0+n-1 are the status (XCreature::PutStatus).
void be_status_rows(int y0, int n);
// The hero waits for a command (XHero::NewMove).
void be_at_cmd(bool on);
// A save was written or removed: write IndexedDB.
void be_saved();
// A run ended (ev death/win/quit): graveyard beacon via RvipWM.report.
void be_run_end(const char* ev, const char* name, const char* killer, long score, int turns, int lvl);
// A finished message line (markup already expanded) for the log.
void be_msg(const std::string& line);
