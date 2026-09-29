/*
This file is part of "Avanor, the Land of Mystery" roguelike game

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// RVIP: auto-explore ('H') and stair walks ('<' / '>' off the stairs).
// One step per turn: XHero::NewMove() asks ExploreStep() for a direction
// key before it would read one from the player. Paths run over what the
// hero remembers (XMapTile::known), never the true map.

#include <deque>
#include <map>
#include <set>
#include <vector>

#include <fmt/format.h>

#include "creature/std_ai.h"
#include "creature/xhero.h"
#include "engine/xapi.h"
#include "game/location.h"
#include "helpers/msgwin.h"
#include "item/item.h"
#include "map/map_objects.h"

namespace {

enum { EX_OFF, EX_EXPLORE, EX_UP, EX_DOWN };

int mode = EX_OFF;
bool first = false;
bool door_step = false; // last step opened a door instead of moving
bool quiet_step = false; // last step went onto a door or stairs: its "There is" is ours
int last_x = -1, last_y = -1;
int msg_base = 0;
int start_monsters = 0;
const XMap* ex_level = nullptr;

// Per level: cells the hero stood on, and item cells already seen.
struct LevelMemory {
    std::set<std::pair<int, int>> stood, items;
};
std::map<const XMap*, LevelMemory> memory;

bool Known(const XMap* m, int x, int y)
{
    if (x < 0 || y < 0 || x >= m->len || y >= m->hgt) {
        return false;
    }

    const XMapTile* c = m->Cell(x, y);
    return c && c->known != ' ' && c->known != 0;
}

XStairWay* StairsAt(const XMap* m, int x, int y, int want)
{
    auto* s = dynamic_cast<XStairWay *>(m->GetSpecial(x, y));
    return s && s->view == (want == EX_UP ? '<' : '>') ? s : nullptr;
}

// A remembered cell explore may step on (the hero's own cell included).
bool Walkable(const XMap* m, int x, int y, bool avoid_traps)
{
    if (!Known(m, x, y)) {
        return false;
    }

    XMapObject* spec = m->GetSpecial(x, y);

    if (auto* trap = dynamic_cast<XTrap *>(spec); trap && trap->isDiscovered() && avoid_traps) {
        return false;
    }

    if (auto* door = dynamic_cast<XDoor *>(spec); door && !door->isOpened) {
        return true; // stepping into it opens it
    }

    return m->GetMovability(x, y) < XTileType::Movability::UNWALKABLE;
}

int KeyFor(int dx, int dy)
{
    static const char keys[3][3] = {{'7', '8', '9'}, {'4', '5', '6'}, {'1', '2', '3'}};
    return keys[dy + 1][dx + 1];
}

} // namespace

bool XHero::ExploreStart(const int m)
{
    // Stair walks need a known staircase of that kind.
    if (m != EX_EXPLORE) {
        bool any = false;

        for (int yy = 0; yy < l->map->hgt && !any; yy++)
            for (int xx = 0; xx < l->map->len && !any; xx++)
                any = Known(l->map, xx, yy) && StairsAt(l->map, xx, yy, m);

        if (!any) {
            msgwin.Add(m == EX_UP ? "You know of no stair up." : "You know of no stair down.");
            return false;
        }
    }

    mode = m;
    first = true;
    return true;
}

int XHero::ExploreStep()
{
    if (mode == EX_OFF) {
        return 0;
    }

    XMap* m = l->map;
    LevelMemory& mem = memory[m];
    auto stop = [&](const std::string& why) {
        mode = EX_OFF;
        if (!why.empty()) {
            msgwin.Add(why);
            vRefresh();
        }
        return 0;
    };

    // Count hostiles in view; remember the first one's name.
    int monsters = 0;
    std::string seen_monster;
    std::vector<std::pair<int, int>> new_items;

    for (int yy = 0; yy < m->hgt; yy++)
        for (int xx = 0; xx < m->len; xx++) {
            if (!m->GetVisible(xx, yy)) {
                continue;
            }

            XCreature* cr = m->GetMonster(xx, yy);

            if (cr && cr != this && isCreatureVisible(cr) && cr->xai->isEnemy(this)) {
                if (!monsters++) {
                    seen_monster = cr->GetNameEx(CRN_T1);
                }
            }

            // Shop wares are not finds.
            if (m->GetItemCount(xx, yy) > 0 && !m->GetPlace(xx, yy) && !mem.items.count({xx, yy})) {
                mem.items.insert({xx, yy});
                new_items.emplace_back(xx, yy);
            }
        }

    if (first) {
        first = false;
        ex_level = m;
        start_monsters = monsters;
    } else {
        if (m != ex_level) {
            return stop(""); // took stairs or fell somewhere
        }

        if (mode != EX_EXPLORE && StairsAt(m, x, y, mode)) {
            return stop(fmt::format("You are on the stairs {}. Press {} to take them.",
                mode == EX_UP ? "up" : "down", mode == EX_UP ? '<' : '>'));
        }

        if (!door_step && x == last_x && y == last_y) {
            return stop("");
        }

        if (!door_step && !quiet_step && msgwin.Important() != msg_base) {
            return stop("");
        }

        if (vKbhit()) {
            vGetch();
            return stop("");
        }

        if (!new_items.empty() && mode == EX_EXPLORE) {
            const auto& [ix, iy] = new_items.front();
            XItem* item = m->GetItemList(ix, iy)->begin()->get();
            return stop(fmt::format("You see {}.", item->toSentence()));
        }
    }

    if (monsters > (mode == EX_EXPLORE ? 0 : start_monsters)) {
        return stop(fmt::format("In view: {}.", seen_monster));
    }

    if (mode != EX_EXPLORE && StairsAt(m, x, y, mode)) {
        return stop(fmt::format("You are on the stairs {}. Press {} to take them.",
            mode == EX_UP ? "up" : "down", mode == EX_UP ? '<' : '>'));
    }

    mem.stood.insert({x, y});

    // Breadth-first search from the hero over remembered cells.
    auto target = [&](int xx, int yy) {
        if (mode != EX_EXPLORE) {
            return StairsAt(m, xx, yy, mode) != nullptr;
        }

        if (mem.stood.count({xx, yy})) {
            return false;
        }

        if (m->GetItemCount(xx, yy) > 0 && !m->GetPlace(xx, yy)) {
            return true;
        }

        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if ((dx || dy) && !Known(m, xx + dx, yy + dy)
                    && xx + dx >= 0 && yy + dy >= 0 && xx + dx < m->len && yy + dy < m->hgt) {
                    return true;
                }

        return false;
    };

    auto search = [&](bool avoid_traps) {
        std::vector<int> from(static_cast<size_t>(m->len) * m->hgt, -1);
        std::deque<int> queue;
        const int start = y * m->len + x;
        from[start] = start;
        queue.push_back(start);

        while (!queue.empty()) {
            const int cur = queue.front();
            queue.pop_front();
            const int cx = cur % m->len;
            const int cy = cur / m->len;

            if (cur != start && target(cx, cy)) {
                int c = cur;
                while (from[c] != start) {
                    c = from[c];
                }
                return c;
            }

            // Straight steps first: paths prefer them over diagonals.
            static const int dirs[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};

            for (const auto& d : dirs) {
                const int nx2 = cx + d[0];
                const int ny2 = cy + d[1];

                if (!Walkable(m, nx2, ny2, avoid_traps)) {
                    continue;
                }

                const int n = ny2 * m->len + nx2;

                if (from[n] != -1) {
                    continue;
                }

                // A creature in the way: go round it (no attack, no swap).
                if (cur == start && m->GetMonster(nx2, ny2)) {
                    continue;
                }

                from[n] = cur;
                queue.push_back(n);
            }
        }

        return -1;
    };

    const int step = search(true);

    if (step < 0) {
        if (search(false) >= 0) {
            return stop("Known traps block the way.");
        }

        if (mode != EX_EXPLORE) {
            return stop("You can't find a way to the stairs.");
        }

        return stop("Nothing left to explore. A secret door? Try searching.");
    }

    const int sx = step % m->len;
    const int sy = step / m->len;

    // Paint the step, and let a key press get in.
    l->map->Center(x, y);
    l->map->Put(this);
    PutStatus();
    vRefresh();
    vDelay(40);

    last_x = x;
    last_y = y;
    msg_base = msgwin.Important();
    // Stepping into a closed door opens it and stays put; its message is
    // our own, not news.
    auto* door = dynamic_cast<XDoor *>(m->GetSpecial(sx, sy));
    door_step = door && !door->isOpened;
    XMapObject* spec = m->GetSpecial(sx, sy);
    quiet_step = spec && !dynamic_cast<XTrap *>(spec);

    return KeyFor(sx - x, sy - y);
}
