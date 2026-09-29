# Handover

## RVIP progress

- **Stage 1 (get + build): done.** Next: stage 2 (explore + stairs + no `--More--`).
- Folder `/home/user/avanor` (cloud), repo `memmaker/avanor`, branch
  `claude/loving-hawking-5x9q75`. Pristine upstream = `20a0f59` "Two more random
  mines" (jaydg/avanor revival, v0.6.0; only `main` upstream).
- **Case O** (C++17 + Lua world scripts, own ANSI terminal backend; nearest
  template: ZAPM/PRIME "own UI class"). Frontend: `engine/global.cpp`, the
  stc/ANSI backend (`#ifndef USE_NOTCURSES`) keeps an 80x25 `stc_screen` cell
  buffer (char + 0xRRGGBB); under `__EMSCRIPTEN__` it hands that buffer to
  `port/be_web.cpp` (`be_present`, `be_getkey`, `be_cursor`, `be_delay`).
  Keys are the game's own codes (`engine/global.h`: `0x8000 | DOS scancode`
  for arrows etc.), pushed from JS via `Module._be_pushkey`.
- **Build:** `sh web/build.sh` (emsdk at `${EMSDK:-/home/user/emsdk}`) →
  `web/dist/{index.html,page.js,avanor.js,.wasm,.data}`. Deps fetched pinned
  into `web/ext/` (gitignored): Lua 5.1.1 (github lua/lua tag; lua.org is 403
  from the cloud), sol2 v3.5.0, fmt 11.0.2, cereal 1.3.2, zstd 1.5.6, argparse
  v3.2; `external/stc.hpp` as the Makefile pins it. Flags: `-O2 -std=c++17
  -fsigned-char -fexceptions -DSOL_ALL_SAFETIES_ON=1 -DSOL_USING_CXX_LUA=1`,
  link `-sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sSTACK_SIZE=8388608
  -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=128MB -sFORCE_FILESYSTEM -lidbfs.js
  --preload-file world@/world --preload-file manual@/manual`. Saves:
  `/home/web_user/.avanor` mounted as IDBFS (synced every 30 s / on hide /
  on exit - to be done properly in stage 5). `ASAN=1 sh web/build.sh` gives
  an emcc ASan build.
- **Quirks:** LuaJIT has no wasm target → PUC Lua 5.1 compiled as C++
  (`em++ -x c++`) so `lua_error` throws; world scripts use nothing
  LuaJIT-specific. sol2 3.3.0 fails with current clang (`optional<T&>::construct`)
  → 3.5.0. Game catches exceptions → `-fexceptions`. Build prints no
  signature-mismatch warnings. Page is a stage-1 placeholder (`<pre>` grid).
- **ASan:** native gcc `-fsanitize=address,undefined` debug build (Makefile,
  `OBJDIR`/`NAME` in the scratchpad, apt luajit/fmt/zstd/cereal/argparse + sol2
  headers), driven via pty with random keys, 4 runs x 60-100 s incl. one death:
  no reports. Build removed.
- **Browser:** headless Chromium (Playwright): title → New game → birth → walks
  in the Valley, no console errors.
- **Open:** save persistence/exit path untested in browser; birth screens need
  ~150 ms between keys in tests.
