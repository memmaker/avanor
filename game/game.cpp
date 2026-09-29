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

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "engine/xarchive.h"
#ifdef __EMSCRIPTEN__
#include <cstdio>
#include "port/be_web.h"
#endif
#include <sol/sol.hpp>
#include "engine/xlua.h"
#include <iostream>
#include "game/game.h"
#include "game/quest.h"
#include "helpers/msgwin.h"
#include "item/xherb.h"
#include "map/map_objects.h"

bool XGame::isGodMode = false;
bool XGame::isMapRevealed = false;
bool XGame::isSpectator = false;
XGUID XGame::hero_guid = 0;
int XGame::current_location = 0;
std::string XGame::start_location;
std::optional<XRect> XGame::start_area;

XGame::XGame()
{
    // Nothing to clear - an empty map already means "no locations".
}

XGame::~XGame()
{
    // XObject::InvalidateAllObjects() (called from Run() before exiting) has
    // already invalidated every location. Deletion itself is now entirely
    // shared_ptr-driven: dropping this map's reference deletes a location
    // only once every other reference to it (the Scheduler's weak_ptr
    // doesn't count) is also gone.
    locations.clear();
}

XCreature* XGame::NewCreature(XCreature * cr, const int x, const int y, XLocation* loc)
{
    cr->FirstStep(x, y, loc);
    Scheduler.Add(cr);
    return cr;
}

//////////////////////////////////////////////////////////////////////////////
// Create all the necessary objects in memory depending on user's choice    //
//////////////////////////////////////////////////////////////////////////////

namespace {

// Is there anybody in this world for the player to be? Asked of a
// restored game, where the answer can be no: a world saved by
// --test-save has no hero in it, and until this was checked, loading one
// looked exactly like a hang. XGame::Run() hands turn after turn to the
// monsters and never reaches anyone who waits for a key, so the screen
// stays on the message the restore left behind while the scheduler runs
// flat out.
bool WorldHasHero()
{
    for (const auto& [id, loc] : Game.locations) {
        if (!loc || !loc->map) {
            continue;
        }

        for (int i = 0; i < loc->map->CellCount(); i++) {
            if (const XCreature* cr = loc->map->map[i].pMonster.get(); cr && cr->isHero()) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

bool XGame::Create(const char type_of_start)
{
    switch (type_of_start) {
        case 'R' :
            vClrScr();
            vGotoXY((size_x - strlen("Restoring game objects, please wait...")) / 2, size_y / 2);
            vPutS("<TEXT>Restoring game objects, please wait...");
            vRefresh();
            if (!XLocation::Restoration()) {
                return false;
            }

            if (XArchive::RestoreGame()) {
                if (WorldHasHero()) {
                    break;
                }

                // The world is loaded and there is nothing to be done
                // with it. Nothing is torn down and no new game is
                // started over the top of it - both would be building on
                // a world that is already half in place - so say what is
                // wrong and stop.
                vGotoXY(0, 20);
                vPutS("<WARNING>That saved game has no hero in it and cannot be played.");
                vGotoXY(0, 21);
                vPutS("<TEXT>Run Avanor again and start a new game.");
                vGotoXY(0, 22);
                vPutS("<KEY>Press any key...");
                vRefresh();
                vGetch();

                return false;
            }

            vGotoXY(0, 20);
            vPutS("<WARNING>There is not a saved game to load. Starting new game.");
            vGotoXY(0, 21);
            vPutS("<KEY>Press any key...");
            vRefresh();
            vGetch();

            [[fallthrough]];

        case 'N' :
            if (!XLocation::LoadScripts()) {
                return false;
            }

            XAlchemy::Init();
            PlantDefinition::Create();
            vClrScr();
            vGotoXY((size_x - strlen("Generating game objects, please wait...")) / 2, size_y / 2);
            vPutS("<TEXT>Generating game objects, please wait...");
            vRefresh();
            if (!CreateLocations()) {
                return false;
            }

            CreateHero();
            break;

        case 'T' :
            if (!XLocation::LoadScripts()) {
                return false;
            }

            XAlchemy::Init();
            PlantDefinition::Create();
            vClrScr();
            vGotoXY((size_x - strlen("Preparing for test, please wait...")) / 2, size_y / 2);
            vPutS("<TEXT>Preparing for test, please wait...");
            vRefresh();

            if (!CreateLocations()) {
                return false;
            }

            break;

        case 'D' :
            if (!XLocation::LoadScripts()) {
                return false;
            }

            XAlchemy::Init();
            PlantDefinition::Create();
            vClrScr();
            vGotoXY((size_x - strlen("Preparing for demo, please wait...")) / 2, size_y / 2);
            vPutS("<TEXT>Preparing for demo, please wait...");
            vRefresh();
            demo = true;

            if (!CreateLocations()) {
                return false;
            }

            break;
    }

    return true;
}

void XGame::RunDemo()
{
    while (true) {
        bool nothing_left = false;

        for (int i = 0; i < 100; i++) {
            const auto o = Game.Scheduler.Get();

            // Nothing left with a turn to take. There is no waiting it
            // out - an empty scheduler stays empty - so the demo is over.
            // Out through the bottom of the loop rather than straight
            // back to the caller: the tidying up after it still has to
            // happen, or every object left alive is destroyed while it
            // still believes it is.
            if (!o) {
                nothing_left = true;
                break;
            }

            o->Run();
            XObject::DrainDeferred();
        }

        if (nothing_left) {
            break;
        }

        if (vKbhit()) {
            const int ch = vGetch();

            if (ch == KEY_ESC) {
                break;
            }

            if (ch == ' ') {
                vGetch();
            }

            if (ch == 'M') {
                msgwin.ShowHistory();
            }

        }
    }

    XObject::InvalidateAllObjects();
}

//////////////////////////////////////////////////////////////////////////////
// Run the game without hero for detecting bugs or benchmarking             //
//////////////////////////////////////////////////////////////////////////////

void XGame::RunWithoutHero() const
{
    clock_t start_clock = clock();

    while (true) {
        bool nothing_left = false;

        for (int i = 0; i < 1000; i++) {
            auto o = Game.Scheduler.Get();

            // Nothing left with a turn to take - see RunDemo().
            if (!o) {
                nothing_left = true;
                break;
            }

            o->Run();
            XObject::DrainDeferred();
        }

        if (nothing_left) {
            break;
        }

        if (vKbhit()) {
            int ch = vGetch();

            if (ch == KEY_ESC) {
                break;
            }

            if (ch == 'L') {
                std::ofstream f(vMakePath(HOME_DIR, "location.txt"));

                for (const auto& [key, location] : locations) {
                    if (location) {
                        location->DumpLocation(f);
                    }
                }
            }

            if (ch == 'I') {
                struct TMP {
                    XItem* pI;
                    int val;
                };

                std::vector<TMP> ia;

                for (const auto& [key, pItem] : XObject::objects) {
                    if (auto* it = dynamic_cast<XItem*>(pItem); it && it->kind & ItemKind::ITEM) {
                        TMP entry{};
                        entry.pI = it;
                        entry.val = entry.pI->GetValue();
                        ia.push_back(entry);
                    }
                }

                // sort by value (descending)
                std::sort(ia.begin(), ia.end(), [](const TMP& a, const TMP& b) {
                    return a.val > b.val;
                });

                // dump it
                std::ofstream f(vMakePath(HOME_DIR, "items.txt"));

                for (const auto&[pI, val] : ia) {
                    if (KindPricedByDice(pI->kind) || KindIsArmour(pI->kind)) {
                        pI->Identify();
                        f << fmt::format("{:<70}{}\n", pI->toString(), val);
                    }
                }
            }
        }

        vClrScr();
        vGotoXY(0, 0);

        std::string status = fmt::format(
            "<EMPHASIS>Testing Avanor - running game without hero ... (press ESC to stop)\n\n"
            "<TEXT>Number of valid objects   : {}\n"
            "Number of invalid objects : {}\n"
            "\n"
            "Number of creatures       : {}\n"
            "Number of items           : {}\n"
            "\n"
            "Turns                     : {}\n"
            "Performance               : {:.1f} turns/s",
            XObject::objects.size(),
            XObject::invalid_count,
            total_cr,
            total_it,
            Game.Scheduler.GetTime() / 1000,
            static_cast<double>(Game.Scheduler.GetTime()) * CLOCKS_PER_SEC
                / (1000. * (clock() - start_clock)));

        vPutS(status.c_str());
        vRefresh();
    }

    XObject::InvalidateAllObjects();
}


//////////////////////////////////////////////////////////////////////////////
// Run the game in ordinary mode                                            //
//////////////////////////////////////////////////////////////////////////////

void XGame::Run()
{
    vHideCursor();

    while (!_exit_flag && XQuest::quest.hero_win == 0 && XQuest::quest.hero_die == 0) {
        const auto o = Game.Scheduler.Get();

        // Nothing left with a turn to take. A game with a hero in it
        // cannot get here - the hero is always scheduled - but Get() is
        // entitled to say so, and following the null would end the
        // process rather than the game.
        if (!o) {
            break;
        }

        if (!o->Run()) {
            // object is dead!
            Game.Scheduler.Remove();
        }

        // Between turns, nothing is mid-call on any game object - the
        // one safe point to actually release objects that evicted
        // themselves from the map during this turn (see
        // XObject::DeferRelease()).
        XObject::DrainDeferred();
    }

    XObject::InvalidateAllObjects();

#ifdef __EMSCRIPTEN__
    // RVIP: the web build autosaves before stairs down; a dead hero's
    // save goes, or reloading the page would bring the dead back.
    if (XQuest::quest.hero_die == 1) {
        std::remove(vMakePath(HOME_DIR, std::string(XArchive::PLAYER_SLOT) + ".svg.zst").c_str());
        be_saved();
    }
#endif

    vClrScr();

    if (XQuest::quest.hero_win == 1) {
        vGotoXY(0, 0);
        vPutS("Congratulations! You are a winner!");
        vGotoXY(0, 1);
        vPutS("Congratulations! You are a winner!");
        vGotoXY(0, 2);
        vPutS("Congratulations! You are a winner!");
    } else if (XQuest::quest.hero_die == 1) {
        vGotoXY(30, 6);
        vPutS("       #       ");
        vGotoXY(30, 7);
        vPutS("     #####     ");
        vGotoXY(30, 8);
        vPutS("       #       ");
        vGotoXY(30, 9);
        vPutS("    #######    ");
        vGotoXY(30, 10);
        vPutS("   #       #   ");
        vGotoXY(30, 11);
        vPutS("   #  RIP  #   ");
        vGotoXY(30, 12);
        vPutS("   #       \x1F\x04@\x1F\x07   ");
        vGotoXY(30, 13);
        vPutS(" #####\x1F\x04@\x1F\x07####\x1F\x02|\x1F\x07## ");
        vGotoXY(30, 14);
        vPutS("\x1F\x02W\x1F\x07#\x1F\x02WWWWWWWWWWW\x1F\x07#\x1F\x02W");
    } else {
        vGotoXY(0, 0);
        vPutS("Goodbye!");
    }

    vRefresh();
    vGetch();
}

bool XGame::CreateLocations() const
{
    //	Create locations
    if (!XLocation::CreateNewGame()) {
        return false;
    }

    //	Bind ways
    for (const auto& [key, loc] : locations) {
        if (loc) {
            for (const auto it1: loc->ways_list) {
                auto way = dynamic_cast<XStairWay*>(it1);

                if (way->dest_x < 0 && way->dest_y < 0 && Location(way->ln)) {
                    for (const auto it2: Location(way->ln)->ways_list) {
                        auto tmp_way = dynamic_cast<XStairWay*>(it2);

                        if (tmp_way->dest_x < 0 && tmp_way->dest_y < 0 && tmp_way->ln == loc->id) {
                            way->Bind(tmp_way);
                        }
                    }
                }
            }
        }
    }

    XLocation::ValidateWays();
    XLocation::LinkLevels();

    return true;
}

void XGame::CreateHero() const
{
    const auto start = Location(start_location);

    // XLocation::ValidateWorld() has already named the culprit if the
    // world script set no start location, or named one that was never
    // built. There is nothing sensible to do with a hero here.
    assert(start != nullptr);

    const XRect whole_map(0, 0, start->map->len, start->map->hgt);
    XRect area = start_area.value_or(whole_map);

    // An area reaching past the map would have GetFreeXY() reading
    // outside it; ValidateWorld() reports that, this just ignores it.
    if (area.left < 0 || area.top < 0
        || area.right > whole_map.right || area.bottom > whole_map.bottom) {
        area = whole_map;
    }

    // The one caller that cannot carry on without a cell: a starting
    // location with nowhere to stand is an unusable world, so let the
    // empty optional throw rather than inventing a coordinate.
    const XPoint hero_point = start->GetFreeXY(&area).value();

    const auto hero = new XHero(XHero::NewCharacter{});
    Game.NewCreature(hero, hero_point.x, hero_point.y, start.get());

    // The hero is standing in the world now, which is what this second
    // hand-over is for: InitHero built the character before there was
    // anywhere to put them, so anything needing a place - a purse, a
    // companion at their heel - waits until here.
    {
        sol::state_view lua(XLua::State());

        if (sol::protected_function placed = lua["OnHeroPlaced"]; placed.valid()) {
            if (const auto result = placed((void*)hero, hero->race, hero->profession);
                !result.valid()) {
                const sol::error err = result;
                std::cerr << "world: OnHeroPlaced: " << err.what() << std::endl;
            }
        }
    }

    XCreature::main_creature = hero;
}
