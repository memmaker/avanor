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

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <zstd.h>

#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/polymorphic.hpp>

#include "engine/xarchive.h"
#ifdef __EMSCRIPTEN__
#include "port/be_web.h"
#endif
#include "map/map.h"
#include "game/game.h"
#include "game/location.h"
#include "game/quest.h"
#include "game/xtime.h"
#include "item/xbook.h"
#include "item/xherb.h"
#include "item/xpotion.h"
#include "item/xenhance.h"
#include "item/xscroll.h"

constexpr unsigned int SAVE_GAME_VERSION = 0x0000087;
constexpr unsigned int SAVE_GAME_CONTROL = 0x9ABCDEF;

// ZSTD compression level: 1 provides excellent speed/size tradeoff
// (0.02s compression, ~0.6MB for 62MB input)
constexpr int ZSTD_SAVEGAME_LEVEL = 1;

// ============================================================================
// Helper: Compress string data with ZSTD
// Returns empty vector on error
// ============================================================================
std::vector<char> CompressWithZstd(const std::string& data, int level = ZSTD_SAVEGAME_LEVEL) {
    if (data.empty()) {
        return {};
    }

    // Get maximum possible compressed size
    size_t const max_compressed_size = ZSTD_compressBound(data.size());
    std::vector<char> compressed(max_compressed_size);

    // Compress the data
    size_t const actual_size = ZSTD_compress(
        compressed.data(), max_compressed_size,
        data.data(), data.size(),
        level);

    if (ZSTD_isError(actual_size)) {
        return {};
    }

    compressed.resize(actual_size);
    return compressed;
}

// ============================================================================
// Helper: Decompress vector data with ZSTD
// Returns empty string on error
// ============================================================================
std::string DecompressWithZstd(const std::vector<char>& compressed) {
    if (compressed.empty()) {
        return {};
    }

    // Get the decompressed size from frame header
    unsigned long long const decompressed_size = ZSTD_getFrameContentSize(
        compressed.data(), compressed.size());

    if (decompressed_size == 0) {
        return {};
    }

    std::string decompressed(decompressed_size, '\0');

    size_t const actual_size = ZSTD_decompress(
        decompressed.data(), decompressed_size,
        compressed.data(), compressed.size());

    if (ZSTD_isError(actual_size)) {
        return {};
    }

    return decompressed;
}

// ============================================================================
// Store game state to compressed file
// ============================================================================
bool XArchive::StoreGame(const char* slot)
{
    // Saving happens mid-turn (from the hero's own key handling), so the
    // deferred-release graveyard could still hold objects evicted earlier
    // in this same turn - invalidated, but still lockable through the
    // scheduler's weak_ptr entries, which Cereal would happily serialize
    // as live objects. Release them first so they can't leak into the
    // save as zombies.
    XObject::DrainDeferred();

    // First, serialize game state to JSON string using Cereal
    std::string serialized_data;
    std::ostringstream oss;

    {
        // cereal::JSONOutputArchive only writes the document's closing brace
        // in its own destructor. Scoped separately from oss, so ar destructs
        // and finalizes the document before oss.str() below ever runs.
        cereal::JSONOutputArchive ar(oss);

        ar(SAVE_GAME_VERSION);
        ar(::guid);

        // Tile ids are handed out in world/tiles.lua's order, so they mean
        // nothing on their own. Storing the names beside them lets a game
        // saved under one tile list be restored under another.
        std::vector<std::string> tile_names = XTileType::Names();
        ar(tile_names);

        ar(Game.locations);

        ar(XQuest::quest);

        XBook::SaveTable(ar);
        XPotion::SaveTable(ar);
        XScroll::SaveTable(ar);
        XEnhance::SaveTable(ar);
        PlantDefinition::SaveTable(ar);

        XTime::serialize(ar);

        ar(XGame::hero_guid);
        ar(Game.Scheduler);

        // main_creature is a raw XCreature* (used pervasively as one
        // throughout gameplay code, not worth converting) - saved as a
        // weak_ptr so it resolves via the same shared_ptr identity
        // tracking as everything else in this archive. Must come after
        // Game.locations above: that's what actually registers this
        // creature's shared_ptr id with Cereal.
        ar(XCreature::ToWeakPtr(XCreature::main_creature));

        ar(SAVE_GAME_CONTROL);
    }

    serialized_data = oss.str();

    std::vector<char> compressed = CompressWithZstd(serialized_data, ZSTD_SAVEGAME_LEVEL);

    // Whether the game reached the disk, honestly - this used to answer
    // "saved" whatever happened, so a save that never got written said so
    // to nobody: not to the player, and not to --test-save, which prints
    // PASS on this very answer.
    if (compressed.empty()) {
        std::cerr << "save: could not compress the game" << std::endl;

        return false;
    }

    const std::string path = vMakePath(HOME_DIR, std::string(slot) + ".svg.zst");
    // Written to a temp file and renamed over the old save, so a failed
    // write never leaves a broken save behind.
    const std::string tmp = path + ".tmp";
    std::ofstream file(tmp, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "save: could not open " << tmp << std::endl;

        return false;
    }

    file.write(compressed.data(), compressed.size());
    file.close();

    std::error_code ec;
    if (!file) {
        std::cerr << "save: could not write " << tmp << std::endl;
        std::filesystem::remove(tmp, ec);

        return false;
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::cerr << "save: could not rename " << tmp << std::endl;
        std::filesystem::remove(tmp, ec);

        return false;
    }

#ifdef __EMSCRIPTEN__
    be_saved();
#endif
    return true;
}

// ============================================================================
// Restore game state from file (compressed or uncompressed)
// ============================================================================
int XArchive::RestoreGame(const char* slot)
{
    // First try compressed format
    {
        std::ifstream file(vMakePath(HOME_DIR, std::string(slot) + ".svg.zst"), std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            size_t const file_size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<char> compressed(file_size);
            if (file.read(compressed.data(), file_size)) {
                std::string serialized_data = DecompressWithZstd(compressed);
                if (!serialized_data.empty()) {
                    return RestoreFromSerializedData(serialized_data);
                }
            }
        }
    }

    // Fall back to uncompressed format
    {
        std::ifstream file(vMakePath(HOME_DIR, std::string(slot) + ".svg"));
        if (file.is_open()) {
            std::string serialized_data((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
            return RestoreFromSerializedData(serialized_data);
        }
    }

    return 0;
}

// ============================================================================
// Internal helper: Restore game state from serialized JSON string
// Used by both compressed and uncompressed restore paths
// ============================================================================
int XArchive::RestoreFromSerializedData(const std::string& serialized_data) {
    try {
        std::istringstream iss(serialized_data);
        cereal::JSONInputArchive ar(iss);

        unsigned int version = 0;
        ar(version);

        if (version != SAVE_GAME_VERSION) {
            return 0;
        }

        ar(::guid);

        std::vector<std::string> tile_names;
        ar(tile_names);

        ar(Game.locations);

        // Every map cell holds a tile id; if the ids moved since the save
        // was written, they are translated through the names now, before
        // anything reads a map.
        if (const std::vector<XTileType::Id> remap = XTileType::RemapFrom(tile_names); !remap.empty()) {
            for (const auto& [key, loc] : Game.locations) {
                if (loc && loc->map) {
                    loc->map->RemapTiles(remap);
                }
            }
        }

        ar(XQuest::quest);

        XBook::LoadTable(ar);
        XPotion::LoadTable(ar);
        XScroll::LoadTable(ar);
        XEnhance::LoadTable(ar);
        PlantDefinition::LoadTable(ar);

        XTime::serialize(ar);

        ar(XGame::hero_guid);
        ar(Game.Scheduler);

        std::weak_ptr<XCreature> main_creature_weak;
        ar(main_creature_weak);
        XCreature::main_creature = main_creature_weak.lock().get();

        unsigned int control = 0;
        ar(control);

        if (control != SAVE_GAME_CONTROL) {
            printf("File corrupted!");
            exit(0);
        }

        // Everything the save does not carry, re-derived now that the
        // whole world is back: a floor above another level saves only
        // the *name* of the level it covers (XLocation::below), so the
        // pointers that make a hole in that floor show the level below
        // have to be made again here. This cannot happen before the
        // load - XLocation::Restoration() runs first, when
        // Game.locations is still empty and there is nothing to link.
        XLocation::ValidateWorld(false);
        XLocation::LinkLevels();
    } catch (const cereal::Exception&) {
        // Malformed/foreign/truncated file - same graceful "nothing to
        // load" outcome as the version check above, not a hard failure.
        return 0;
    }

    return 1;
}
