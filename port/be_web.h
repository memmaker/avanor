// RVIP browser frontend for Avanor: the terminal backend in
// engine/global.cpp hands its cell buffer and its key reads to these when
// built with Emscripten.
#pragma once

void be_init(int cols, int rows);
void be_finit();
void be_present(const char* chars, const unsigned* rgbs, int cols, int rows);
void be_cursor(int x, int y);
int be_kbhit();
int be_getkey();
void be_delay(int ms);
