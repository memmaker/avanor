// RVIP sound events: game actions name an event, the page plays
// sound/<event>.wav (web/mksounds.py). No-op off the web build.
#pragma once
#ifdef __EMSCRIPTEN__
void be_sound(const char* event);
#define RVIP_SOUND(e) be_sound(e)
#else
#define RVIP_SOUND(e) ((void)0)
#endif
