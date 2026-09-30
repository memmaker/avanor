# Handover

## RVIP progress

- **Stage 1 (get + build): done.**
- **Stage 2 (explore + stairs + no `--More--`): done.**
  Explore key `H` (free; `X` also free). Code: `player/xhero_explore.cpp`
  (`XHero::ExploreStart(mode)` / `ExploreStep()`, added to Makefile SRCS).
  Hook: `XHero::NewMove()` (`player/xhero_input.cpp`) asks `ExploreStep()`
  for a digit direction key before `vGetch()`; `<`/`>` off the stairs call
  `ExploreStart(2/3)` (walk to nearest known stair, stop there, press again).
  Known grid: `XMapTile::known` (!= ' ' and != 0; set by `SetVisible`, never
  forgotten). Stops: hostile in view (named), new non-shop item in view,
  new message (`msgwin.count`, added), key (`vKbhit`), no move; a step into a
  closed door (bump opens it) or onto a door/stairs doesn't stop on its own
  message. Shop wares (`GetPlace`) are no targets. `(more)` in
  `XMsgWin::Add` skipped under `__EMSCRIPTEN__` (history `M` keeps all).
  Help: `manual/kblayout.html`. Local build: Homebrew emcc on PATH
  (`EMSDK` optional).
- **Stage 3 (Enter menu + inventory): done.**
  Code: `player/xhero_menu.cpp` (in Makefile SRCS): `XHero::CommandMenu()`
  (Enter; static table grouped as `manual/kblayout.html`, no moves) and
  `XHero::InventoryMenu()` (`i`); both return a command key that
  `NewMove()` (`player/xhero_input.cpp`) dispatches like a typed key.
  Box menus: `XBoxMenu(title, {key,text})` in `helpers/xgui.cpp` (sized to
  content, scrolls when taller than 25 rows; 8/2/arrows, 5/6/Enter/Space
  choose, entry key runs it, Esc/0/./4 close). List cursor:
  `XGuiList::EnableCursor()` (`>` marker; on in every `Inventory()` prompt
  and in `Equipment()`). Item actions: `Actions()` table (u/E/D/r, wear/take
  off direct via `XBodyPart::Wear/UnWear`, !/d/s/g, examine = message);
  command keys run through the real commands with globals `rvip_pre` +
  `rvip_pre_state` (1 pending, 2 used) + `rvip_pre_oneshot`, taken in
  `XHero::Inventory()` without drawing; cleared at the next `NewMove()` key
  read; `rvip_reopen` reopens `i` unless `HostileInView()`. `i` keys:
  letter as shown (uppercase) = main action, lowercase = drop/take off
  (swapped in stage 4: the shown letter used to drop), Ctrl+letter =
  examine, 5/Enter = item menu, + - * numpad, 4/6 = equipment, other keys =
  normal command (filter keys `[|{}'=!?"\%]$X` still filter).
  Explore: ambient messages via `msgwin.AddAmbient()` (corpse smell/decay,
  `item/xcorpse.cpp`); explore compares `msgwin.Important()`.
- **Stage 4 (tiles): done.**
  Set: **DawnLike** only (fallback set; Avanor ships no tiles, upstream/
  SourceForge have none; fantasy theme so no need to ask). Credit
  DragonDePlatino + DawnBringer (CC BY 4.0; README done, Help in stage 6).
  `web/mkdawn.py` reads the world's ids (tiles.lua terrain, Monster.new +
  class, Template.new, Item/Food.new, Plant.new, PotionColour.new,
  TrapType.new, MapObject.new, hero races) and writes `web/tiles-dawn.png`
  (16x16, 16 per row, original size, 691 slots; "floor+sprite" slots are
  composites of two DawnLike sprites) + `port/dawn_map.inc` (key → slot).
  Coverage (own DawnLike sprite per id; rest same-set stand-ins): terrain
  27/27, monsters 112/112, hero races 7/7, item types 76/76, special
  items/food 24/24, potion appearances 37/42 (6 share a sprite), plants
  18/18, traps 7/7, map objects 10/12 (furniture, outer objects stay text)
  = 318/325 = 97.8%. Unknown ids fall back by monster class / ItemKind.
  Code: `port/rvip_tiles.cpp` (`RvipTerrainTile/ObjectTile/ItemTile/
  CreatureTile`), called from `XMap::Put()` (`map/map.cpp`, `#ifdef
  __EMSCRIPTEN__`) → `be_tile(sx, sy, bg, fg, dim, ch, rgb)`
  (`port/be_web.cpp`), sent per present only while the screen cell still
  shows that char+colour; -2 = map cell as text, -1 = not map. `vStore`
  suspends tiles until `vRestore` (menus/lists over the map = text).
  Floors autotile (16 per style, bordered where the real neighbour's
  terrain differs), walls join walls/doors (Movability::WALL). Potions by
  appearance (`PotionDescription::force_color`), herbs by species only
  once identified (`XHerb::Species()`, added), traps by type
  (`XTrap::GetTrapType()`, added), stairs by the object's own glyph (type
  isn't stored), items by `GetContentId()` then `it` then kind. Memory:
  `XMapTile::rvip_bg/fg` (not saved; after a load remembered cells whose
  glyph is the terrain's get the terrain tile, others text); remembered =
  dim flag, page darkens.
  Page (still stage-1 `<pre>`): map rows on a `<canvas>` (16px × zoom,
  nearest-neighbour, `drawImage`), text rows above/below as `<pre>`; bar
  with Tiles button (DawnLike → None) and −/+ zoom (1..6). Pref file
  `/home/web_user/.avanor/web-tiles` = "<set name> <zoom>", read in the
  preRun `syncfs` callback before the sheet loads; `sheetGen` guards late
  loads. Tested in the pane: new game, tiles, explore, remembered dim, i
  menu text-only, None sticks over reload, uppercase D drinks / lowercase
  e drops. IDBFS db deleted after.
- **Stage 5 (web page and windows): done.** Next: stage 6 (docs and sound).
  Live: https://ruzzoli.de/roguelikes/avanor/ (`web/deploy.sh`, guard; branch
  `main-rvip` pushed as `memmaker/main`). Page: `web/index.html` +
  `web/avanor.js` (loads `../rvip-wm.js`, `../rvip-app.js`; Emscripten output
  is `avanor-core.*`). Windows: Map (canvas), Messages, Status, Inventory,
  Visible (HTML text); pop-up `#pop` via `RvipWM.popup`; prompt line =
  message rows 0-1. Top bar: Help, File, Windows, Tiles (DawnLike/None),
  Font; no Audio yet (stage 6). Layout, cell size, tile set, fonts in
  `/avanor/web-layout.json`.
  C side (`port/be_web.cpp`, all to `Module.av`): `XMap::Put` walks the
  whole level under `__EMSCRIPTEN__` (only the viewport goes to the stc
  screen) and sends it via `be_map_begin/be_mapcell/be_map_end` (packed
  bg|fg<<12|dim<<24, -1 = glyph+rgb) with the hero cell; the page centres
  it (`RvipWM.center`), A−/A+ on Map = cell size 8..64. Pop-up: `map_live`
  (set by Put, cleared by `vClrScr`) + a vStore stack (`be_store`/
  `be_restore` push/pop map_live; snapshot at the outermost vStore): no map
  = whole screen's non-blank bbox, menu over map = bbox of cells differing
  from the snapshot; nested menus no longer break tiles (map is its own
  pane). Status = rows marked by `XCreature::PutStatus` (`be_status_rows`).
  Messages = `XMsgWin::Add` sentence lines (`be_msg`, colour runs from the
  markup escapes). Inventory/Visible = `RvipSidePanes` (`port/rvip_tiles.cpp`)
  from `contain` / visible cells, with item/creature colours and tile slots.
  `be_at_cmd` around the command `vGetch` in `NewMove` (prompt hide; clears
  the vStore stack). Text lines: colour runs `\x05#rrggbb`.
  Saves: `HOME_DIR` = `/avanor/` under Emscripten = `RvipApp.dir` (IDBFS
  `RvipApp.mount`); `XArchive::StoreGame` writes `.tmp` + rename, then
  `be_saved()` → sync. Autosave: quiet `StoreGame()` before `>` on stairs
  down (web only). Death deletes the save (web only, `XGame::Run`). Game end:
  `vFinit` → `av.exit` → sync → reload after 1 s. Sync also every 15 s,
  on hide/pagehide.
  Tested in the pane: title/birth pop-ups, map with tiles following the
  hero (zoomed), all windows filled, Enter menu, `i` + nested item menu,
  explore, `>` walk + descend (autosave file written), `S`, reload + `r`
  restores, Q → Goodbye → reload, A+ on Messages only changes it, layout
  survives reload, Tiles None/DawnLike, resize 760x500/1440x900/1200x750,
  divider to both ends, Reset windows, no console errors.
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
- **Open:** shop and death not tested in the browser this stage (death path: save removed, end screen, reload); no help.html yet (stage 6); no mouse. Item prompts can't switch pack/equipment/floor with 4/6 (only `i` → equipment); examine is a one-line message (no item description in the game); Hostile in view blocks explore by design (Valley bandits). Birth screens need
  ~150 ms between keys in tests.

### Orchestrator note (stage 6 interrupted)
- Stages 1–5 done and committed; `main-rvip` pushed to `memmaker` as `main`
  (`d19fb8c`); live at https://ruzzoli.de/roguelikes/avanor/.
- **Stage 6 (docs + sound) was interrupted mid-work.** Uncommitted edits in
  creature/, magic/, player/, port/be_web.cpp, web/avanor.js are that agent's
  unfinished sound/death-path work, not verified. Next agent: review `git diff`,
  finish or revert, then do stage 6 fully.
- Stage 6 brief extras: help sources `manual/`, https://avanor.sourceforge.net/,
  https://www.roguebasin.com/index.php/Avanor (user-supplied; also for stage 8
  shrine); credit DawnLike (DragonDePlatino, DawnBringer, CC BY 4.0); document
  H explore, `<`/`>` stair walk, Enter menu, `i` item menus; exercise the death
  path once (never run in stage 5).
- Still open: shop/options/death untested in browser; no mouse; item prompts
  can't switch lists with 4/6; examine is one message line.
- Remaining: stages 6–9. Before stage 7 the repo exists already (memmaker/avanor).
