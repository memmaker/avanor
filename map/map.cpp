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

#include <fstream>
#include <iostream>

#include <cereal/types/polymorphic.hpp>
#include <sol/sol.hpp>

#include "creature/creature.h"
#include "game/game.h"
#include "item/item.h"
#include "map/map.h"
#include "map/map_objects.h"
#ifdef __EMSCRIPTEN__
#include "port/rvip_tiles.h"
#endif

void XTileType::RegisterLua(sol::state_view& lua)
{
    lua.new_enum("Movability",
        "UNKNOWN", Movability::UNKNOWN,
        "NORMAL", Movability::NORMAL,
        "SHARD", Movability::SHARD,
        "AHARD", Movability::AHARD,
        "HARD", Movability::HARD,
        "VHARD", Movability::VHARD,
        "UNWALKABLE", Movability::UNWALKABLE,
        "WATER", Movability::WATER,
        "DEEPWATER", Movability::DEEPWATER,
        "WALL", Movability::WALL,
        "MOUNTAIN", Movability::MOUNTAIN
    );

    lua.new_enum("Visibility",
        "UNKNOWN", Visibility::UNKNOWN,
        "NORMAL", Visibility::NORMAL,
        "SHARD", Visibility::SHARD,
        "AHARD", Visibility::AHARD,
        "HARD", Visibility::HARD,
        "VHARD", Visibility::VHARD,
        "WALL", Visibility::WALL
    );

    // Filled in by DefineTile(), one entry per tile the script defines.
    lua.create_named_table("XTileType");
}

std::vector<XTileType> std_tile_data;

XTileType::Id XTileType::Define(const std::string& id_name, const char view, const unsigned color,
                                const std::string& name, const Movability movability, const Visibility visibility)
{
    if (const Id existing = ByName(id_name); existing != NONE || (!std_tile_data.empty() && std_tile_data[NONE].id_name == id_name)) {
        std::cerr << "tiles: '" << id_name << "' defined twice" << std::endl;

        return existing;
    }

    XTileType tile;
    tile.id_name = id_name;
    tile.view = view;
    tile.color = color;
    tile.name = name;
    tile.movability = movability;
    tile.visibility = visibility;

    std_tile_data.push_back(tile);

    return static_cast<Id>(std_tile_data.size()) - 1;
}

void XTileType::SetDiggableInto(const Id tile, const std::string& into_name)
{
    if (static_cast<size_t>(tile) < std_tile_data.size()) {
        std_tile_data[tile].diggable_into = into_name;
    }
}

XTileType::Id XTileType::DiggableInto(const Id tile)
{
    if (static_cast<size_t>(tile) >= std_tile_data.size() || std_tile_data[tile].diggable_into.empty()) {
        return NONE;
    }

    return ByName(std_tile_data[tile].diggable_into);
}

void XTileType::SetFertile(const Id tile, const bool fertile)
{
    if (static_cast<size_t>(tile) < std_tile_data.size()) {
        std_tile_data[tile].fertile = fertile;
    }
}

bool XTileType::isFertile(const Id tile)
{
    return static_cast<size_t>(tile) < std_tile_data.size() && std_tile_data[tile].fertile;
}

XTileType::Id XTileType::ByName(const std::string& id_name)
{
    for (size_t i = 0; i < std_tile_data.size(); i++) {
        if (std_tile_data[i].id_name == id_name) {
            return static_cast<Id>(i);
        }
    }

    return NONE;
}

std::vector<std::string> XTileType::Names()
{
    std::vector<std::string> names;

    names.reserve(std_tile_data.size());

    for (const auto& tile : std_tile_data) {
        names.push_back(tile.id_name);
    }

    return names;
}

std::vector<XTileType::Id> XTileType::RemapFrom(const std::vector<std::string>& saved_names)
{
    std::vector<Id> remap;
    bool moved = false;

    for (size_t saved_id = 0; saved_id < saved_names.size(); saved_id++) {
        const Id now = ByName(saved_names[saved_id]);

        if (now == NONE && saved_names[saved_id] != std_tile_data[NONE].id_name) {
            std::cerr << "tiles: the save holds '" << saved_names[saved_id]
                      << "', which no longer exists - those cells become "
                      << std_tile_data[NONE].id_name << std::endl;
        }

        moved = moved || now != static_cast<Id>(saved_id);
        remap.push_back(now);
    }

    return moved ? remap : std::vector<Id>{};
}

int XTileType::ValidateTiles()
{
    if (std_tile_data.empty()) {
        std::cerr << "tiles: the world script defined none - every map would be blank"
                  << std::endl;

        return 1;
    }

    return 0;
}

void XTileType::ForgetTiles()
{
    std_tile_data.clear();
}

XMapTile::XMapTile()
{
    n = XTileType::NONE;
    pMonster = nullptr;
    pSpecialObject = nullptr;
    visible = false;
    known = ' ';
    color = 0;
    place = nullptr; // by default
    room_id = 0;
};

XMapTile::~XMapTile()
{
    item_list.InvalidateAll();

    // Unlike XItem, XCreature::Invalidate() doesn't remove itself from
    // wherever it's referenced from (no map-cell back-reference to do
    // that through) - clear pMonster explicitly afterward rather than
    // relying on a side effect. A cell can still be genuinely holding a
    // live creature here (e.g. the whole map being torn down at once,
    // out from under a creature that was simply still standing on it) -
    // this is real, not theoretical: reachable via
    // XLocation::Invalidate()'s `delete map`.
    if (pMonster) {
        pMonster->Invalidate();
        pMonster = nullptr;
    }

    // Move the pointer out BEFORE invalidating: XMapObject::Invalidate()
    // self-evicts via SetSpecial() on its own cell, which must not run
    // against this very cell mid-destruction. With the member already
    // moved-from, Invalidate()'s GetSpecial(x, y) == this check fails
    // harmlessly and the object is released at scope end instead (its
    // deleter sees is_valid already false and plain-deletes).
    if (auto spec = std::move(pSpecialObject)) {
        spec->Invalidate();
    }
}

void XMapTile::SaveCrossRefs(cereal::JSONOutputArchive& ar) const
{
    ar(pMonster, item_list, pSpecialObject);
}

void XMapTile::LoadCrossRefs(cereal::JSONInputArchive& ar)
{
    ar(pMonster, item_list, pSpecialObject);
}

XMap::XMap()
{
    map = nullptr;
    hgt = 0;
    len = 0;
    wx = 0;
    wy = 0;
    stored_x = 0;
    stored_y = 0;
    stored_len = 0;
    stored_hgt = 0;
    below = nullptr;
}

XMap::XMap(const int l, const int h) : XMap(l, h, 0, 0, l, h)
{
}

XMap::XMap(const int l, const int h, const int sx, const int sy, const int sl, const int sh)
{
    map = new XMapTile[sl * sh];

    hgt = h;
    len = l;
    wx = 0;
    wy = 0;
    stored_x = sx;
    stored_y = sy;
    stored_len = sl;
    stored_hgt = sh;
    below = nullptr;
}

XMapTile* XMap::StoredCell(const int x, const int y) const
{
    if (x < stored_x || x >= stored_x + stored_len || y < stored_y || y >= stored_y + stored_hgt) {
        return nullptr;
    }

    return &map[(x - stored_x) + (y - stored_y) * stored_len];
}

XMapTile* XMap::Cell(const int x, const int y) const
{
    if (x < 0 || x >= len || y < 0 || y >= hgt) {
        return nullptr;
    }

    XMapTile* cell = StoredCell(x, y);

    // Nothing of this level's own here: either the floor above does not
    // reach this far, or it does and there is a hole in it. Either way
    // what is at these coordinates is what the level below has.
    if (below && (!cell || cell->n == XTileType::NONE)) {
        return below->Cell(x, y);
    }

    return cell;
}

XMap::~XMap()
{
    delete[] map;
}

void XMap::ResVisible(const int x, const int y) const
{
    if (XMapTile* cell = Cell(x, y)) {
        cell->visible = false;
    }
}

void XMap::SetVisible(const int x, const int y) const
{
    if (XMapTile* cell = Cell(x, y)) {
        cell->visible = true;
        // Remembered in the shade it was actually seen in, so a place
        // does not change colour the moment it drops out of sight.
        cell->color = JitterRGB(std_tile_data[cell->n].color, x, y);
        cell->known = std_tile_data[cell->n].view;
    }
}

bool XMap::GetVisible(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);

    return cell && cell->visible;
}

void XMap::SetPlace(const int x, const int y, XAnyPlace* place) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    cell->place = place;
}

XAnyPlace* XMap::GetPlace(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->place;
}

void XMap::ResKnown(const int x, const int y) const
{
    XMapTile* cell = Cell(x, y);
    assert(cell);

    cell->known = 0;
}

void XMap::SetKnown(const int x, const int y) const
{
    XMapTile* cell = Cell(x, y);
    assert(cell);

    cell->known = 1;
}

int XMap::GetKnown(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->known;
}

void XMap::SetSpecial(const int x, const int y, XMapObject* spec) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    // Callers only ever pass either a fresh object self-registering into an
    // empty slot, or nullptr to evict the current occupant (now always from
    // inside the occupant's own Invalidate() - see XMapObject::Invalidate())
    // - never a replacement of one live occupant with another.
    if (spec == nullptr) {
        // The cell can be the occupant's only strong owner, and eviction
        // is typically requested from inside one of the occupant's own
        // methods - destroying it synchronously here would delete it out
        // from under its own still-executing code (for Cereal-loaded
        // objects, via a deleter that additionally destructs without
        // Invalidate(), tripping ~XObject's assert). Park the reference
        // in the graveyard instead; it's released between turns.
        XObject::DeferRelease(std::move(cell->pSpecialObject));
        cell->pSpecialObject = nullptr;
        return;
    }

    // Same idiom as SetMonster(): first placement (nothing owns it yet, e.g. a
    // fresh XLuaObject self-registering from its own constructor) establishes
    // the one master shared_ptr, with a deleter that defers to Invalidate(),
    // since the scheduler may still hold its own reference keeping this alive
    // past this call. Any later placement (already shared_ptr-owned, e.g. via
    // the scheduler) just takes another reference to that same control block.
    if (spec->weak_from_this().expired()) {
        cell->pSpecialObject = std::shared_ptr<XMapObject>(spec, [](XMapObject* p) {
            if (p->isValid()) {
                p->Invalidate();
            } else {
                delete p;
            }
        });
    } else {
        cell->pSpecialObject = std::static_pointer_cast<XMapObject>(spec->shared_from_this());
    }
}

XMapObject* XMap::GetSpecial(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->pSpecialObject.get();
}

int XMap::GetVisibility(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    auto* xdoor = dynamic_cast<XDoor *>(cell->pSpecialObject.get());

    if (xdoor && xdoor->isOpened == 0) {
        return 0;
    }

    // Anything from HARD upwards stops sight:
    // a large tree or a mountain is as good as a wall to look through.
    if (std_tile_data[cell->n].visibility >= XTileType::Visibility::HARD) {
        return 0;
    }

    return 1;
}

const char* XMap::GetDescription(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return std_tile_data[cell->n].name.c_str();
}

XTileType::Movability XMap::GetMovability(const int x, const int y) const
{
    // A floor above another level offers footing only where it has floor
    // of its own. The ground below can be seen through the gaps, and
    // walked when you are down there - not from up here.
    const XMapTile* cell = StoredCell(x, y);

    if (below && (!cell || cell->n == XTileType::NONE)) {
        return XTileType::Movability::UNWALKABLE;
    }

    assert(cell);
    auto* xdoor = dynamic_cast<XDoor *>(cell->pSpecialObject.get());

    if (xdoor && xdoor->isOpened == 0) {
        return XTileType::Movability::WALL;
    }

    return std_tile_data[cell->n].movability;
}

int XMap::XGetMovability(const int x, const int y) const
{
    // See GetMovability(): open air is not somewhere to step.
    const XMapTile* m = StoredCell(x, y);

    if (below && (!m || m->n == XTileType::NONE)) {
        return 1;
    }

    assert(m);

    if (m->pMonster) {
        return 2;
    }

    auto* xdoor = dynamic_cast<XDoor *>(m->pSpecialObject.get());

    if (std_tile_data[m->n].movability < XTileType::Movability::UNWALKABLE
        && !(xdoor && xdoor->isOpened == 0)) {
        return 0;
    }

    return 1;
}

void XMap::PutItem(const int x, const int y, XItem* item) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    item->x = x;
    item->y = y;
    cell->item_list.insert(XItem::Own(item));
}

XItemList* XMap::GetItemList(const int x, const int y) const
{
    XMapTile* cell = Cell(x, y);
    assert(cell);

    return &cell->item_list;
}

unsigned int XMap::GetItemCount(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->item_list.size();
}

void XMap::SetMonster(const int x, const int y, XCreature* monst) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    // First time this creature is ever placed on the map (birth): nothing
    // owns it yet, so this is the one place its master shared_ptr gets
    // constructed. Every later move just transfers a fresh shared_ptr from
    // the same control block via shared_from_this() - never a second,
    // independent one.
    if (monst->weak_from_this().expired()) {
        // This can be the last shared_ptr/weak_ptr reference going away
        // without the creature ever having gone through Die() (e.g. the
        // whole map being torn down at once at full teardown, out from
        // under a creature that was simply still standing on it) - make
        // sure Invalidate() always runs first regardless, since it does
        // real cleanup (deregistering from the object registry, invalidating
        // carried items, ...), not just bookkeeping. No XPtr<XCreature>
        // cross-reference exists anywhere anymore (all migrated to
        // weak_ptr), so nothing can still be holding a legacy reference by
        // the time we get here - safe to delete unconditionally once
        // Invalidate() has run (or already had, on a previous pass).
        cell->pMonster = std::shared_ptr<XCreature>(monst, [](XCreature* p) {
            if (p->isValid()) {
                p->Invalidate();
            } else {
                delete p;
            }
        });
    } else {
        cell->pMonster = std::static_pointer_cast<XCreature>(monst->shared_from_this());
    }
}

void XMap::ResMonster(const int x, const int y) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    cell->pMonster = nullptr;
}

XCreature* XMap::GetMonster(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->pMonster.get();
}

void XMap::PutChar(const int x, const int y, const char c, const int color) const
{
    if (x >= wx && x < wx + SCR_LEN && y >= wy && y < wy + SCR_HGT) {
        vPutCh(x - wx + SCR_X, y - wy + SCR_Y, c, color);
    }
}

void XMap::Put(XCreature * cr) const
{
#ifdef __EMSCRIPTEN__
    // What each cell shows, for the map tiles: the char+colour drawn and
    // the sprites the game's data gives for it (port/rvip_tiles.cpp).
    char tch = ' ';
    unsigned trgb = xBLACK;
    int tbg = -1, tfg = -1;
    bool tdim = false;
    auto put = [&](int sx, int sy, char c, unsigned rgb) {
        vPutCh(sx, sy, c, rgb);
        tch = c;
        trgb = rgb;
    };
#else
    auto put = [](int sx, int sy, char c, unsigned rgb) { vPutCh(sx, sy, c, rgb); };
#endif
    for (int i = 0; i < SCR_HGT && wy + i < hgt; i++) {
        for (int j = 0; j < SCR_LEN && wx + j < len; j++) {
            XMapTile* tmap = Cell(wx + j, wy + i);
#ifdef __EMSCRIPTEN__
            tbg = tfg = -1;
            tdim = false;
#endif

            if (!tmap) {
                vPutCh(j + SCR_X, i + SCR_Y, ' ', xBLACK);
#ifdef __EMSCRIPTEN__
                be_tile(j + SCR_X, i + SCR_Y, -1, -1, false, ' ', xBLACK);
#endif
                continue;
            }

            // God mode's reveal lights the whole level,
            // but only the hero's own sight is remembered
            const bool lit = tmap->visible || XGame::isMapRevealed;

            if (lit) {
                auto* trap = dynamic_cast<XTrap *>(tmap->pSpecialObject.get());
#ifdef __EMSCRIPTEN__
                tbg = RvipTerrainTile(this, wx + j, wy + i);
#endif

                // Everything standing here is drawn, except a trap the
                // hero has not found yet.
                if (tmap->pSpecialObject && (!trap || trap->isDiscovered())) {
                    put(j + SCR_X, i + SCR_Y, tmap->pSpecialObject->view, tmap->pSpecialObject->color);
#ifdef __EMSCRIPTEN__
                    tfg = RvipObjectTile(tmap->pSpecialObject.get());
                    if (tfg < 0) {
                        tbg = -1;
                    }
#endif

                    if (tmap->visible) {
                        tmap->color = tmap->pSpecialObject->color;
                        tmap->known = tmap->pSpecialObject->view;
                    }
                } else if (!tmap->item_list.empty()) {
                    const XItem* item = tmap->item_list.begin()->get();

                    put(j + SCR_X, i + SCR_Y, item->view, item->color);
#ifdef __EMSCRIPTEN__
                    tfg = RvipItemTile(item);
#endif

                    if (tmap->visible) {
                        tmap->color = item->color;
                        tmap->known = item->view;
                    }
                } else {
                    //int tn = (i + wy) * len + j + wx;
                    int n = tmap->n;
                    put(j + SCR_X, i + SCR_Y, std_tile_data[n].view,
                           JitterRGB(std_tile_data[n].color, wx + j, wy + i));
                }

#ifdef __EMSCRIPTEN__
                if (tmap->visible) {
                    tmap->rvip_bg = static_cast<short>(tbg);
                    tmap->rvip_fg = static_cast<short>(tfg);
                }
#endif

                if (tmap->pMonster && cr->isCreatureVisible(tmap->pMonster.get())) {
                    XCreature * xb = tmap->pMonster.get();
                    put(xb->x - wx + SCR_X, xb->y - wy + SCR_Y, xb->view, xb->color);
#ifdef __EMSCRIPTEN__
                    tfg = RvipCreatureTile(xb);
                    if (tbg < 0) {
                        tbg = RvipTerrainTile(this, wx + j, wy + i);
                    }
#endif
                }
            } else {
                put(j + SCR_X, i + SCR_Y, ' ', xBLACK);
            }

            // Remembered, not seen: the same glyph and colour, dimmed,
            // so the hero's field of view reads at a glance.
            if (tmap->known && !lit) {
                put(j + SCR_X, i + SCR_Y, tmap->known, DimRGB(tmap->color, RememberedBrightness()));
#ifdef __EMSCRIPTEN__
                tdim = true;
                if (tmap->rvip_bg >= 0) {
                    tbg = tmap->rvip_bg;
                    tfg = tmap->rvip_fg;
                } else if (tmap->known == std_tile_data[tmap->n].view) {
                    // Restored from a save: memory holds only the glyph.
                    tbg = RvipTerrainTile(this, wx + j, wy + i);
                }
#endif
            }
#ifdef __EMSCRIPTEN__
            be_tile(j + SCR_X, i + SCR_Y, tbg, tfg, tdim, tch, trgb);
#endif
        }

        // A map narrower than the screen reaches only part of the way
        // across it. Nothing clears the screen between turns, so the
        // rest of the row would keep whatever was drawn there before -
        // blank it. For a map at least as wide as the screen this loop
        // does not run at all.
        for (int j = len - wx; j < SCR_LEN; j++) {
            vPutCh(j + SCR_X, i + SCR_Y, ' ', xBLACK);
        }
    }

    // The same for a map shorter than the screen: every row past its
    // bottom edge.
    for (int i = hgt - wy; i < SCR_HGT; i++) {
        for (int j = 0; j < SCR_LEN; j++) {
            vPutCh(j + SCR_X, i + SCR_Y, ' ', xBLACK);
        }
    }
}

void XMap::Center(const int x, const int y)
{
    if (x <= wx + 2 || x >= wx + SCR_LEN - 2) {
        wx = x - SCR_LEN / 2;

        if (wx + SCR_LEN > len) {
            wx = len - SCR_LEN;
        }

        if (wx < 0) {
            wx = 0;
        }
    }

    if (y <= wy + 2 || y >= wy + SCR_HGT - 2) {
        wy = y - SCR_HGT / 2;

        if (wy + SCR_HGT > hgt) {
            wy = hgt - SCR_HGT;
        }

        if (wy < 0) {
            wy = 0;
        }
    }
}

void XMap::SetXY(const int x, const int y, const XTileType::Id std_map) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    cell->n = std_map;
}

XTileType::Id XMap::GetXY(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->n;
}

void XMap::SetRoom(const int x, const int y, const int room_id) const
{
    XMapTile* cell = StoredCell(x, y);
    assert(cell);

    cell->room_id = room_id;
}

int XMap::GetRoom(const int x, const int y) const
{
    const XMapTile* cell = Cell(x, y);
    assert(cell);

    return cell->room_id;
}

// Where a cell sits in `seen`, or -1 for anywhere this map holds no cell
// of its own. The same row-major indexing as `map`, over the stored part.
int XMap::SeenIndex(const int x, const int y) const
{
    if (x < stored_x || x >= stored_x + stored_len
        || y < stored_y || y >= stored_y + stored_hgt) {
        return -1;
    }

    return (y - stored_y) * stored_len + (x - stored_x);
}

void XMap::MarkSeen(const std::string& who, const int x, const int y)
{
    const int at = SeenIndex(x, y);

    if (at < 0) {
        return;
    }

    // Allocated on first sight, so a level nobody explores costs nothing.
    auto& record = seen[who];

    if (record.empty()) {
        record.assign(CellCount(), 0);
    }

    record[at] |= 1;
}

void XMap::MarkLoot(const std::string& who, const int x, const int y, const bool there)
{
    const int at = SeenIndex(x, y);

    if (at < 0) {
        return;
    }

    const auto it = seen.find(who);

    // Nothing is remembered about somewhere nobody has been shown -
    // MarkSeen() comes first, and this only adds to what it recorded.
    if (it == seen.end() || it->second.empty()) {
        return;
    }

    if (there) {
        it->second[at] |= 2;
    } else {
        it->second[at] &= ~2;
    }
}

bool XMap::hasLoot(const std::string& who, const int x, const int y) const
{
    const int at = SeenIndex(x, y);

    if (at < 0) {
        return false;
    }

    const auto it = seen.find(who);

    return it != seen.end() && !it->second.empty() && (it->second[at] & 2);
}

bool XMap::hasSeen(const std::string& who, const int x, const int y) const
{
    const int at = SeenIndex(x, y);

    if (at < 0) {
        return false;
    }

    const auto it = seen.find(who);

    return it != seen.end() && !it->second.empty() && (it->second[at] & 1);
}

double XMap::SeenShare(const std::string& who) const
{
    const auto it = seen.find(who);

    if (it == seen.end() || it->second.empty()) {
        return 0.0;
    }

    int walkable = 0;
    int known = 0;

    for (int i = 0; i < CellCount(); i++) {
        if (std_tile_data[map[i].n].movability >= XTileType::Movability::UNWALKABLE) {
            continue;
        }

        walkable++;

        if (it->second[i] & 1) {
            known++;
        }
    }

    return walkable > 0 ? static_cast<double>(known) / walkable : 0.0;
}

void XMap::CreateRoom(const int x, const int y, const int l, const int h, const int px, const int py, const XTileType::Id m1, const XTileType::Id m2) const
{
    CreateRoom(x, y, l, h, m1, m2);
    SetXY(px, py, m1);
}

void XMap::CreateRoom(const int x, const int y, const int l, const int h, const XTileType::Id m1, const XTileType::Id m2) const
{
    for (int i = 0; i < l; i++) {
        for (int j = 0; j < h; j++) {
            if (i == 0 || i == l - 1 || j == 0 || j == h - 1) {
                SetXY(i + x, j + y, m2);
            } else {
                SetXY(i + x, j + y, m1);
            }
        }
    }
}

void XMap::Dump(std::ofstream &file) const
{
    for (int i = 0; i < stored_hgt; i++) {
        for (int j = 0; j < stored_len; j++) {
            XMapTile* tmap = &map[i * stored_len + j];
            int n = tmap->n;
            char vch = std_tile_data[n].view;

            if (tmap->pSpecialObject) {
                vch = tmap->pSpecialObject->view;
            }

            if (!tmap->item_list.empty()) {
                const auto item = *(tmap->item_list.begin());
                vch = item->view;
            }

            if (tmap->pMonster) {
                vch = tmap->pMonster->view;
            }

            file << vch;
        }

        file << "\n";
    }
}

void XMap::ForceRecenter(const int x, const int y)
{
    wx = x - SCR_LEN / 2;

    if (wx + SCR_LEN > len) {
        wx = len - SCR_LEN;
    }

    if (wx < 0) {
        wx = 0;
    }

    wy = y - SCR_HGT / 2;

    if (wy + SCR_HGT > hgt) {
        wy = hgt - SCR_HGT;
    }

    if (wy < 0) {
        wy = 0;
    }
}

namespace {

// Whether the terrain itself can be walked on, ignoring whatever
// happens to be standing there: a closed door opens, and a creature
// moves. Connectivity is a property of the map, not of its occupants.
bool isWalkableTerrain(const XMap* map, int x, int y)
{
    return std_tile_data[map->GetXY(x, y)].movability < XTileType::Movability::UNWALKABLE;
}

// 4-directional flood fill of every walkable tile reachable from (sx, sy),
// marking each in visited[] (row-major, same indexing as XMap::map).
// Returns the number of tiles marked (including the seed).
int FloodFillWalkable(const XMap* map, int sx, int sy, std::vector<bool>& visited)
{
    std::vector<XPoint> stack;
    stack.emplace_back(sx, sy);
    visited[sx + sy * map->len] = true;
    int count = 1;

    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };

    while (!stack.empty()) {
        XPoint p = stack.back();
        stack.pop_back();

        for (int i = 0; i < 4; i++) {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];

            if (nx < 0 || nx >= map->len || ny < 0 || ny >= map->hgt) {
                continue;
            }

            int idx = nx + ny * map->len;

            if (visited[idx] || !isWalkableTerrain(map, nx, ny)) {
                continue;
            }

            visited[idx] = true;
            count++;
            stack.emplace_back(nx, ny);
        }
    }

    return count;
}

// True if the map's entire walkable floor is a single connected region -
// i.e. any floor tile can be reached from any other, so a stairway placed
// anywhere on it (chosen afterward, once generation returns) can never
// land in an isolated pocket.
} // namespace

bool XMap::isFullyConnected() const
{
    const XMap* map = this;

    int seed_x = -1;
    int seed_y = -1;
    int total_floor = 0;

    for (int y = 0; y < map->hgt; y++) {
        for (int x = 0; x < map->len; x++) {
            if (isWalkableTerrain(map, x, y)) {
                total_floor++;

                if (seed_x < 0) {
                    seed_x = x;
                    seed_y = y;
                }
            }
        }
    }

    if (total_floor == 0) {
        return false;
    }

    std::vector<bool> visited(map->len * map->hgt, false);

    return FloodFillWalkable(map, seed_x, seed_y, visited) == total_floor;
}

// Last-resort fallback if regeneration never produced a fully-connected
// layout within the attempt budget below - carves a straight tunnel from
// one tile of every disconnected pocket to a single hub tile, guaranteeing
// full reachability regardless of how pathological the random layout was,
// rather than silently shipping a map with an unreachable area.
void XMap::ConnectAllRegions(const XTileType::Id floor)
{
    XMap* map = this;

    std::vector<bool> visited(map->len * map->hgt, false);
    std::vector<XPoint> component_seeds;

    for (int y = 0; y < map->hgt; y++) {
        for (int x = 0; x < map->len; x++) {
            if (!isWalkableTerrain(map, x, y) || visited[x + y * map->len]) {
                continue;
            }

            component_seeds.emplace_back(x, y);
            FloodFillWalkable(map, x, y, visited);
        }
    }

    if (component_seeds.size() < 2) {
        return;
    }

    const XPoint hub = component_seeds[0];

    for (size_t i = 1; i < component_seeds.size(); i++) {
        int x = component_seeds[i].x;
        int y = component_seeds[i].y;

        while (x != hub.x) {
            map->SetXY(x, y, floor);
            x += (x < hub.x) ? 1 : -1;
        }

        while (y != hub.y) {
            map->SetXY(x, y, floor);
            y += (y < hub.y) ? 1 : -1;
        }
    }
}

void XMap::RemapTiles(const std::vector<XTileType::Id>& remap) const
{
    for (int i = 0; i < CellCount(); i++) {
        const size_t saved = static_cast<size_t>(map[i].n);

        map[i].n = saved < remap.size() ? remap[saved] : XTileType::NONE;
    }
}
