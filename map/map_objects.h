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

#ifndef OTHER_MISC_H
#define OTHER_MISC_H

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <cereal/types/base_class.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/string.hpp>

#include <sol/forward.hpp>

#include "helpers/registry.h"
#include "creature/deity.h"
#include "item/item.h"
#include "engine/xmapobj.h"
#include "magic/magic.h"

class XLocation;

//////////////////////////////////////////////////////////////////////
// Which sort of trap this is - the id world/traps.lua registered it
// under. What sorts a world has is content; the engine knows only how a
// trap can go off.
using TRAP_TYPE = std::string;

// "Any trap": what the dungeon builder asks for when it wants one and
// does not care which.
inline constexpr const char* TRAP_ANY = "";

// How a trap hurts whoever set it off.
enum class TrapHarm {
    NONE,     // it does no damage of its own - a magic trap casts instead
    FIRES,    // one charge of what it is loaded with, at them
    IMPALES,  // everything it is loaded with, at once
    HURTS,    // plain dice, with nothing loaded
};

struct TrapTypeStats {
    TRAP_TYPE id;
    int colour = 0;

    // A magic trap: the effect it brings about. Empty for one that works
    // by simpler means.
    EFFECT casts;

    TrapHarm harm = TrapHarm::NONE;

    // For HURTS: the damage, as XDice(count, sides, bonus).
    int count = 0;
    int sides = 0;
    int bonus = 0;

    // How hard it is to dodge, and what the message calls the thing that
    // hurt. FIRES names the charge itself instead.
    int to_hit = 0;
    std::string message;

    // A hole in the ground: whoever falls in has to climb out, and may
    // fall in again trying.
    bool pit = false;
};

extern Registry<TrapTypeStats> trap_types_db;

const TrapTypeStats* FindTrapType(const TRAP_TYPE& id);

// A trap type drawn at random, for the dungeon builder.
TRAP_TYPE AnyTrapType();

// Fluent builder - see world/traps.lua.
class TrapTypeBuilder
{
    public:
        explicit TrapTypeBuilder(std::string id);

        TrapTypeBuilder& Looks(int colour);
        TrapTypeBuilder& Casts(const std::string& effect);
        TrapTypeBuilder& Fires(int to_hit);
        TrapTypeBuilder& Impales(int to_hit, const std::string& message);
        TrapTypeBuilder& Hurts(int count, int sides, int bonus, int to_hit,
            const std::string& message);
        TrapTypeBuilder& Pit();
        void Register();

    private:
        TrapTypeStats t;
};

//XTrap
/////////////////////////////////////////////////////////////////////
class XTrap final : public XMapObject
{
    public:
        // How dangerous, and how well hidden. RANDOM again means "pick".
        enum class Level {
            VERY_LOW = 0,
            LOW = 1,
            AVG = 2,
            HI = 3,
            VERY_HI = 4,
            MASTERWORK = 5,
            RANDOM
        };

    private:
        TRAP_TYPE trap_type;
        Level trap_level;

        // owner to get exp.
        std::weak_ptr<XCreature> owner;

        int isVisibleForHero = 0;

        // Owning: the ammo template is created directly here and never
        // placed in any XItemList, so this is always its only reference.
        // See XItem::Own().
        std::shared_ptr<XItem> trap_item;

        bool isMagic = false;

        // required for pits
        XGUID last_activator{};
    public:
        int activation_count = 0;
        const TRAP_TYPE& GetTrapType() const { return trap_type; }
        DECLARE_CREATOR(XTrap, XMapObject);
        XTrap() : trap_type(), trap_level()
        {
            assert(0);
        }

        XTrap(int _x, int _y, XLocation* _l, XTrap::Level tl = XTrap::Level::RANDOM, const TRAP_TYPE& tt = TRAP_ANY, XCreature* _owner = nullptr, XItem* items = nullptr);

        int MoveIn(XCreature * cr);

        int MoveOut(XCreature * cr);

        void Activate(XCreature * cr);

        int Check(XCreature * cr);

        // Whether the hero has found this trap - a different question
        // from XMapObject::isVisible(), which asks whether the cell it
        // sits in is in sight. Named apart from it so it neither hides
        // it nor is mistaken for it.
        [[nodiscard]] bool isDiscovered() const;

        int Disarm(XCreature * cr);

        // Fills an unloaded trap from its recipe - see the definition.
        void LoadFromRecipe();

        // Registers the trap-type and recipe builders.
        static void RegisterLua(sol::state_view& lua);
    protected:
        // teardown hook, called by XObject::Invalidate()
        void OnInvalidate() override;
    public:

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(trap_type, trap_level, owner, trap_item, isVisibleForHero, last_activator, activation_count);
        }
};

//////////////////////////////////////////////////////////////////////
//TrapRecipe
/////////////////////////////////////////////////////////////////////

// One sort of trap a character can build: what the Create Trap menu calls
// it, how good you have to be to attempt it, what it costs to make, and
// what one of that sort comes loaded with when the world puts one in a
// dungeon.
struct TrapRecipe {
    std::string name;
    TRAP_TYPE type;

    // The CREATETRAP level this becomes available at.
    int level{0};

    // A magic recipe casts this instead of consuming anything, and costs
    // mana per charge rather than an item. SP_NONE means it is built
    // from things instead.
    SPELL_NAME spell{SP_NONE};

    // What may be loaded into it. Any one of `loads` will do - a spear
    // trap takes a short spear or a long one - and an empty list means
    // this trap holds nothing at all, like a plain pit.
    ItemKind loads_kind{ItemKind::UNKNOWN};
    std::vector<ItemType> loads;

    // What must be wielded to build it, if anything, and what to call it
    // when the character is not holding one.
    bool needs_tool{false};
    ItemType tool;
    std::string tool_name;

    // How much building one exercises the skill.
    int practice{10};

    // What one found in a dungeon arrives loaded with: a count drawn from
    // [fill_min, fill_max]. Zero means the world never places a loaded one.
    int fill_min{0};
    int fill_max{0};

    // Does this recipe accept that item as its charge?
    [[nodiscard]] bool Accepts(const XItem* item) const;
};

// Every recipe world/traps.lua declared, in the order it declared them -
// which is the order the Create Trap menu lists them, so the file reads
// as the menu does.
extern std::vector<TrapRecipe> trap_recipes;

// The recipe a trap of this type is built from, or nullptr. Where one
// type has several recipes - an arrow trap and a spear trap are the same
// machine loaded differently - this is the first, which is the simplest.
const TrapRecipe* FindTrapRecipe(const TRAP_TYPE& type);

// Fluent builder for one row of that table:
//
//   TrapRecipe.new("Spear trap", "arrow")
//       :Level(2)
//       :Loads(ItemKind.WEAPON, { "short_spear", "long_spear" })
//       :Practice(15)
//       :Register()
class TrapRecipeBuilder
{
    public:
        TrapRecipeBuilder(const std::string& name, const std::string& type);

        TrapRecipeBuilder& Level(int level);
        TrapRecipeBuilder& Spell(SPELL_NAME spell);
        TrapRecipeBuilder& Loads(ItemKind kind, const sol::table& types);
        TrapRecipeBuilder& Tool(ItemType tool, const std::string& tool_name);
        TrapRecipeBuilder& Practice(int practice);
        TrapRecipeBuilder& Fills(int min, int max);

        void Register();

    private:
        TrapRecipe t;
};

//////////////////////////////////////////////////////////////////////
//XStairWay
/////////////////////////////////////////////////////////////////////
class XStairWay final : public XMapObject
{
    public:
        // Which way the stairway leads, which is also what it looks like
        // ('<' or '>'). UNKNOWN builds a stairway that is not drawn and
        // not placed on the map.
        enum class Type {
            UNKNOWN,
            UP,
            DOWN
        };

        // Registers this enum as the Lua table XStairWay.MEMBER.
        static void RegisterLua(sol::state_view& lua);

        DECLARE_CREATOR(XStairWay, XMapObject);
        XStairWay(int _x, int _y, XLocation* loc, const std::string& _ln, Type type);
        // The id of the location this stairway leads to.
        std::string ln;

        // Where this stairway comes out: the matching stairway's position
        // within location ln. Negative until Bind() pairs the two ends, which
        // is what XGame's way-binding pass tests for.
        int dest_x, dest_y;

        void Bind(XStairWay* way);

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(ln, dest_x, dest_y);
        }

        const std::string GetName(XCreature * /*viewer*/) override
        {
            return "a stairway";
        }

    protected:
        XStairWay() : dest_x(-1), dest_y(-1) {
        }
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XTeleport
/////////////////////////////////////////////////////////////////////
class XTeleport final : public XMapObject
{
    public:
        DECLARE_CREATOR(XTeleport, XMapObject);
        XTeleport(int _x, int _y, XLocation* loc, const std::string& _ln, int _dest_x, int _dest_y);
        // The id of the location this pad drops you in.
        std::string ln;

        // Where this pad drops you: a position within location ln. Fixed
        // at construction, unlike a stairway's, which is paired up later.
        int dest_x, dest_y;

        void MoveIn(XCreature* cr);

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(ln, dest_x, dest_y);
        }

        const std::string GetName(XCreature * /*viewer*/) override
        {
            return "a magic circle";
        }

    protected:
        XTeleport() : dest_x(-1), dest_y(-1) {
        }
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XDoor
//////////////////////////////////////////////////////////////////////
class XDoor final : public XMapObject
{
    public:
        DECLARE_CREATOR(XDoor, XMapObject);
        XDoor(int _x, int _y, int flg, XLocation * _l);
        void Switch();
        int isOpened;

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(isOpened);
        }

        const std::string GetName(XCreature * /*viewer*/) override
        {
            return "a door";
        }

    protected:
        XDoor() : isOpened(0) {}
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XAltar
//////////////////////////////////////////////////////////////////////
class XAltar final : public XMapObject
{
    public:
        DECLARE_CREATOR(XAltar, XMapObject);
        XAltar(int _x, int _y, const DEITY& deity, XLocation* _l);

        // Places the altar and gives it its glyph, name and colour, so a
        // bare PlaceSpecial("XAltar", x, y) from script yields a usable
        // altar. Which god it belongs to is said with SetAltarDeity().
        bool PlaceAt(XLocation* location, int _x, int _y) override;

        [[nodiscard]] const DEITY& GetDeity() const { return deity; }
        void SetDeity(const DEITY& id) { deity = id; }

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(deity);
        }

        const std::string GetName(XCreature * /*viewer*/) override
        {
            return color == xWHITE ? "an altar of white granite" : "an altar of black granite";
        }

    protected:
        XAltar() {}

        // Whose altar this is. Empty for one no script has claimed, which
        // then serves whichever god the sacrificer stands best with.
        DEITY deity;
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XGrave
//////////////////////////////////////////////////////////////////////
class XGrave: public XMapObject
{
        int isOpened;
        XItemList hidden_items;
    public:
        DECLARE_CREATOR(XGrave, XMapObject);
        XGrave(int _x, int _y, char* subscr, XLocation* _l);
        void HideItem(XItem* item);
        bool onOuterUse(XCreature* cr) override;
    protected:
        // teardown hook, called by XObject::Invalidate()
        void OnInvalidate() override;
    public:

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(isOpened, hidden_items);
        }

    protected:
        XGrave() : isOpened(0) {}
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XFurniture
//////////////////////////////////////////////////////////////////////
class XFurniture: public XMapObject
{
    public:
        DECLARE_CREATOR(XFurniture, XMapObject);
        XFurniture(int _x, int _y, int _c, char _v, const char* subscr, XLocation* _l);

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
        }

    protected:
        XFurniture() {}
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XLuaObject - a thing on the map that takes turns and does its own
//thinking in Lua.
//////////////////////////////////////////////////////////////////////

// One sort of such thing: what it looks like, and which Lua function it
// hands its turn to. What sorts exist is content.
struct MapObjectStats {
    std::string id;
    std::string name;
    char view{'*'};
    int colour{0};

    // The Lua function its turn goes to, called with the object. It
    // answers how long until its next turn, or nothing at all to stop
    // taking turns and be removed.
    std::string on_run;

    // Called when somebody picks the thing up, with the object and the
    // picker. It answers the thing they got, or nothing. Unsaid, the
    // thing cannot be picked at all.
    std::string on_pick;

    // Called to name the thing, with the object and whoever is looking.
    // Unsaid, it reads as whatever :View() called it.
    std::string on_name;

    // How long before the first turn. Content may say a range, and each
    // one placed draws from it, so a field of them does not all stir at
    // the same moment.
    int first_delay_min{1};
    int first_delay_max{1};
};

extern Registry<MapObjectStats> map_objects_db;

const MapObjectStats* FindMapObject(const std::string& id);

// Fluent builder - see the doc block in world/ where these are declared.
class MapObjectBuilder
{
    public:
        explicit MapObjectBuilder(std::string id);

        MapObjectBuilder& View(const std::string& name, const std::string& view,
            sol::optional<int> colour);
        MapObjectBuilder& OnRun(const std::string& handler);
        MapObjectBuilder& OnPick(const std::string& handler);
        MapObjectBuilder& OnName(const std::string& handler);
        MapObjectBuilder& FirstDelay(int min, sol::optional<int> max);
        void Register();

    private:
        MapObjectStats t;
};

class XLuaObject final : public XMapObject
{
        // Which sort this is - the id content registered it under.
        std::string content_id;

        // Whatever the handlers want to remember between turns. Saved
        // with the object, so a plant keeps its strength and its species
        // across a save.
        std::map<std::string, int> memory;
        std::map<std::string, std::string> notes;

    public:
        DECLARE_CREATOR(XLuaObject, XMapObject);
        XLuaObject(const std::string& id, int _x, int _y, XLocation* _l);

        [[nodiscard]] const std::string& GetContentId() const
        {
            return content_id;
        }

        int Remember(const std::string& key) const;
        void Remember(const std::string& key, int value);
        const std::string& Note(const std::string& key) const;
        void Note(const std::string& key, const std::string& value);

        // Hands over to the row's :OnPick() handler, which answers what
        // was got. Null when the row names no handler.
        XObject* Pick(XCreature* picker) override;

        // Asks the row's :OnName() handler, or falls back to the plain
        // name the row was registered with.
        const std::string GetName(XCreature* viewer) override;

        bool PlaceAt(XLocation* location, int _x, int _y) override;

        // Hands the turn to the handler its row names. When the handler
        // says it is finished - or there is no handler to ask - the thing
        // is invalidated, which takes it off the map, and false comes back
        // so the scheduler drops it too.
        bool Run() override;

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(content_id, memory, notes);
        }

    protected:
        XLuaObject() = default;
        friend class cereal::access;
};

//////////////////////////////////////////////////////////////////////
//XOuterObject
//////////////////////////////////////////////////////////////////////
class XOuterObject final : public XMapObject
{
        std::string onEventLua;
    public:
        DECLARE_CREATOR(XOuterObject, XMapObject);
        XOuterObject(int _x, int _y, int _c, char _v, const char* subscr, XLocation* _l, const char* event);
        bool onOuterUse(XCreature* cr) override;

        // One symmetric serialize() rather than a split save()/load()
        // pair: as found and fixed for XCreature/XPotion/XCorpse earlier
        // this session, a split pair - even a correctly-disambiguated
        // one - silently breaks Cereal's *polymorphic type registration*
        // for the type at runtime whenever an ancestor (XMapObject here)
        // has its own plain serialize().
        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XMapObject>(this));
            ar(onEventLua);
        }

    protected:
        XOuterObject() = default;
        friend class cereal::access;
};

#endif
