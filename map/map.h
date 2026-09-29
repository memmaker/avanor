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

#ifndef MAP_H
#define MAP_H

#include <string>
#include <unordered_map>
#include <vector>

#include <memory>
#include <set>

#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>

#include <sol/forward.hpp>

#include "helpers/rect.h"
#include "item/itemlist.h"
#include "map/xanyplace.h"

#define MAP_MINX 10
#define MAP_MINY 10
#define MAP_MAXX 200
#define MAP_MAXY 200
#define SCR_HGT  (size_y - 5)
#define SCR_LEN  (size_x)
#define SCR_X    0
#define SCR_Y    2

struct XTileType {
    // How hard the tile is to cross. Anything from UNWALKABLE upwards
    // cannot be walked at all, which is the comparison the map and the
    // AI both make.
    enum class Movability {
        UNKNOWN,
        NORMAL,
        SHARD,
        AHARD,
        HARD,
        VHARD,
        UNWALKABLE,
        WATER,
        DEEPWATER,
        WALL = 80,
        MOUNTAIN
    };

    // How much the tile blocks sight, on the same scale.
    enum class Visibility {
        UNKNOWN,
        NORMAL,
        SHARD,
        AHARD,
        HARD,
        VHARD,
        WALL = 80
    };

    // A tile is whatever the world script defined, identified by the id
    // DefineTile() handed out in definition order. The engine knows no
    // types of its own - only that id 0 is "nothing here", which is what
    // an undrawn cell and a translation without a tile both hold.
    using Id = int;

    static constexpr Id NONE = 0;

    // Opens the empty Lua table DefineTile() fills, and registers the two
    // scales above as Movability.MEMBER / Visibility.MEMBER.
    static void RegisterLua(sol::state_view& lua);

    // Adds a tile and returns its id. Ids are handed out in definition
    // order, so they belong to one particular tiles.lua - see
    // XTileType::Names() for what that means for saved games.
    static Id Define(const std::string& id_name, char view, unsigned color, const std::string& name,
                     Movability movability, Visibility visibility);

    // What is left behind when this tile is dug through, or NONE when it
    // cannot be dug. Held by name, because a tile may name one the script
    // has not defined yet.
    static void SetDiggableInto(Id tile, const std::string& into_name);
    [[nodiscard]] static Id DiggableInto(Id tile);

    // Whether things grow here - what a herb bush may spread onto.
    static void SetFertile(Id tile, bool fertile);
    [[nodiscard]] static bool isFertile(Id tile);

    // The id the script gave that name, or NONE if it never defined it.
    // A lookup per call site is deliberate: ids change when the scripts
    // are reloaded, so nothing may cache one across a game.
    [[nodiscard]] static Id ByName(const std::string& id_name);

    // Every id's name, in id order - written into a save so a game
    // stored under one tiles.lua can be restored under another.
    [[nodiscard]] static std::vector<std::string> Names();

    // Maps the ids in a save onto today's, given the names that save was
    // written with. Empty when nothing moved and no remapping is needed.
    [[nodiscard]] static std::vector<Id> RemapFrom(const std::vector<std::string>& saved_names);

    // Complains if the world script defined no tiles at all, which would
    // leave every map blank. Returns the number of complaints.
    static int ValidateTiles();

    // Dropped with the Lua state that defined them.
    static void ForgetTiles();

    // The name the script defined this tile under.
    std::string id_name;

    // See SetDiggableInto()/SetFertile() above.
    std::string diggable_into;
    bool fertile = false;

    char view;
    unsigned color;
    std::string name;
    Movability movability;
    Visibility visibility;
};

// Indexed by XTileType::Id - see XTileType::Define().
extern std::vector<XTileType> std_tile_data;

/* Forward declarations */
class XMapObject;
class XItem;

struct XMapTile {
    XMapTile();
    ~XMapTile();

    XTileType::Id n;
    std::shared_ptr<XCreature> pMonster; // if null then no monster here
    XItemList item_list;             // list of item in this cell of map. Automatic construct/destruct
    // Shared ownership, same reasoning and idiom as pMonster above: a
    // schedulable pSpecialObject (e.g. XLuaObject) is ALSO independently owned
    // by the scheduler's own XScheduler::Entry for as long as it's due to run
    // again, exactly like a creature.
    std::shared_ptr<XMapObject> pSpecialObject; // door, way, trap door.
    bool visible;                    // visible for HERO!!!
    char known;                      // for hero memory
    unsigned color;                  // for hero memory
    // RVIP tiles: the sprites last seen here (not saved; -1 = none).
    short rvip_bg = -1, rvip_fg = -1;

    // Pointer to an object which describes this place. It can be nullptr, Shop,
    // special room etc. It will be useful to mark all rooms in cave (to create
    // good links between rooms or to create unique room descriptions).
    XAnyPlace* place;

    // 0 - no room
    // 1, 2, ... - room ID
    // Currently used to indicate "special" rooms which should not be crossed
    // by passages.
    int room_id;

    // `place` is a raw, non-owning pointer into the owning XLocation's
    // places[MAX_PLACES] (which owns the XAnyPlace instances via
    // unique_ptr) - deliberately not persisted here; XLocation
    // re-derives every cell's `place` structurally, by re-running
    // XAnyPlace::Setup() for each loaded place once the whole map grid
    // is loaded, the same idiom as XMapObject::l/SetLocation().
    //
    // pMonster/item_list/pSpecialObject go through non-template,
    // concrete-archive-typed helpers (defined in map.cpp, where XCreature
    // and XItem are fully visible) rather than directly in this template -
    // same reasoning as XCreature::SaveModifier/LoadModifier: pulling
    // creature.h/item.h's full definitions into this widely-included
    // header would be circular.
    void SaveCrossRefs(cereal::JSONOutputArchive& ar) const;
    void LoadCrossRefs(cereal::JSONInputArchive& ar);

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(n, visible, known, color, room_id);

        if constexpr (Archive::is_loading::value) {
            LoadCrossRefs(ar);
        } else {
            SaveCrossRefs(ar);
        }
    }
};

class XMap
{
    public:
        void ForceRecenter(int x, int y);

        // The coordinate space: how far positions on this map reach. For
        // nearly every level this is also how much of it exists.
        int hgt, len;

        // Where the viewport is scrolled to.
        int wx, wy;

        // The part of that space this map holds cells for. A floor built
        // over another level holds only the cells it stands on, and takes
        // its coordinate space from the level underneath, so that a
        // position means the same place on both.
        int stored_x, stored_y;
        int stored_len, stored_hgt;

        // stored_len * stored_hgt cells, row-major within the stored part.
        XMapTile* map;

        // What explorers know of this level, one byte per stored cell,
        // indexed the same way as `map`. Bit 0 is "has been shown this
        // cell"; bit 1 is "there was something lying here when it was
        // last looked at", which is what lets a band come back for what
        // it walked past.
        //
        // Keyed by whoever is doing the exploring: a band shares one
        // record under its group id, so its members do not each re-walk
        // the same corridor, while a creature with no group gets a record
        // of its own under its guid - otherwise every groupless explorer
        // in the world would pool its knowledge with every other, and
        // each would believe corridors seen that nobody had walked.
        // XStandardAI::ExplorerKey() decides which.
        //
        // Empty until somebody with EXPLORER_MOVE looks around, so a
        // world with no explorers in it carries nothing.
        std::unordered_map<std::string, std::vector<uint8_t>> seen;

        // The level this one is a floor above, or null. Whatever this map
        // holds no cell for - outside its own part of the space, or
        // holding XTileType::NONE within it, which is a hole in the floor
        // - is that level's: its ground, its creatures, its items, and
        // the hero's memory of them. Rebuilt by XLocation::LinkLevels()
        // rather than saved, the same way ways_list is.
        XMap* below;

        // The cell at these coordinates as the world sees it, read
        // through to the level below wherever this map holds nothing of
        // its own. Null only when the coordinates are nowhere at all.
        [[nodiscard]] XMapTile* Cell(int x, int y) const;

        // The cell this map holds itself, or null when it holds none
        // here. Everything that changes a level goes through this, so a
        // floor above can never write into the level it looks down on.
        [[nodiscard]] XMapTile* StoredCell(int x, int y) const;

        // Whether this level really has a cell here of its own, rather
        // than a hole showing the level below. Things are placed into
        // this level's own cell but read back through Cell(), which falls
        // through - so anything put on a hole is invisible from the
        // moment it lands.
        [[nodiscard]] bool OwnsCell(int x, int y) const
        {
            const XMapTile* cell = StoredCell(x, y);

            return cell && cell->n != XTileType::NONE;
        }

        // How many cells this map holds, and where the nth of them is.
        // A sweep over a whole map runs over these, not over the
        // coordinate space, which for a floor above is far larger.
        [[nodiscard]] int CellCount() const { return stored_len * stored_hgt; }
        [[nodiscard]] int CellX(const int n) const { return stored_x + n % stored_len; }
        [[nodiscard]] int CellY(const int n) const { return stored_y + n / stored_len; }

        XMap();
        XMap(int l, int h);
        // A floor covering only part of a larger level's space.
        XMap(int l, int h, int sx, int sy, int sl, int sh);
        ~XMap();

        void Put(XCreature* cr) const;
        void Center(int x, int y);
        void PutChar(int x, int y, char c, int color) const;
        [[nodiscard]] XTileType::Movability GetMovability(int x, int y) const;
        [[nodiscard]] int XGetMovability(int x, int y) const;
        [[nodiscard]] int GetVisibility(int x, int y) const;
        void SetXY(int x, int y, XTileType::Id std_map) const;
        [[nodiscard]] XTileType::Id GetXY(int x, int y) const;
        // True when the whole walkable floor is one connected region, so
        // anything placed on it afterwards - a stairway, the hero - can be
        // reached from anywhere else.
        [[nodiscard]] bool isFullyConnected() const;

        // Last-resort repair: carves a straight tunnel from every isolated
        // pocket to a single hub, so no floor is left unreachable however
        // pathological the layout was.
        void ConnectAllRegions(XTileType::Id floor);

        // Translates every cell's tile id through remap, for a map that
        // was saved when the ids meant something else.
        void RemapTiles(const std::vector<XTileType::Id>& remap) const;

        void SetRoom(int x, int y, int room_id) const;
        [[nodiscard]] int GetRoom(int x, int y) const;

        // Remember that `who` can see this cell. Coordinates outside the
        // part of the world this map holds are ignored rather than
        // refused: a floor above reads through to the level below for
        // anything it has no cell of its own for, and what the level
        // below holds is that level's to remember.
        void MarkSeen(const std::string& who, int x, int y);

    private:
        // Where a cell sits in `seen`, or -1 for nowhere this map holds.
        [[nodiscard]] int SeenIndex(int x, int y) const;

    public:

        // Whether `who` has been shown this cell. False for anywhere this
        // map holds no cell of its own, and for a `who` that has never
        // looked at anything.
        [[nodiscard]] bool hasSeen(const std::string& who, int x, int y) const;

        // Remember whether something was lying here when `who` last
        // looked. Only recorded for a cell already being marked seen, so
        // it never invents knowledge of somewhere nobody has been.
        void MarkLoot(const std::string& who, int x, int y, bool there);

        // Whether `who` last saw something lying here. A cell it has
        // never been shown answers false, as does one it has since walked
        // over and found bare.
        [[nodiscard]] bool hasLoot(const std::string& who, int x, int y) const;

        // How much of what can be walked on `who` has seen, 0 to 1. The
        // measure is walkable cells rather than all of them, because the
        // solid rock a level is cut out of is not somewhere anybody was
        // ever going to go and counting it would put a ceiling on the
        // answer that has nothing to do with how well the level was
        // swept.
        [[nodiscard]] double SeenShare(const std::string& who) const;

        void SetVisible(int x, int y) const;
        void ResVisible(int x, int y) const;
        [[nodiscard]] bool GetVisible(int x, int y) const;

        void PutItem(int x, int y, XItem* item) const;
        [[nodiscard]] unsigned int GetItemCount(int x, int y) const;
        [[nodiscard]] XItemList* GetItemList(int x, int y) const;

        void SetMonster(int x, int y, XCreature* monst) const;
        void ResMonster(int x, int y) const;
        [[nodiscard]] XCreature* GetMonster(int x, int y) const;

        void SetPlace(int x, int y, XAnyPlace* place) const;
        [[nodiscard]] XAnyPlace* GetPlace(int x, int y) const;

        void SetKnown(int x, int y) const;
        void ResKnown(int x, int y) const;
        [[nodiscard]] int GetKnown(int x, int y) const;

        [[nodiscard]] const char* GetDescription(int x, int y) const;

        void SetSpecial(int x, int y, XMapObject* spec) const;
        [[nodiscard]] XMapObject* GetSpecial(int x, int y) const;

        void CreateRoom(int x, int y, int l, int h, XTileType::Id m1, XTileType::Id m2) const;
        void CreateRoom(int x, int y, int l, int h, int px, int py, XTileType::Id m1, XTileType::Id m2) const;

        void Dump(std::ofstream &file) const;

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(len, hgt, wx, wy, stored_x, stored_y, stored_len, stored_hgt);
            ar(seen);

            if constexpr (Archive::is_loading::value) {
                map = new XMapTile[CellCount()];
            }

            for (int i = 0; i < CellCount(); i++) {
                ar(map[i]);
            }
        }
};

#endif
