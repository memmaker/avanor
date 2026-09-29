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

#ifdef __EMSCRIPTEN__
#include "port/be_web.h"
#endif
#include "item/xmoney.h"
#include "item/xbook.h"
#include "item/xscroll.h"
#include "item/xpotion.h"
#include "item/xcorpse.h"
#include "item/xmissile.h"
#include "engine/xlua.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fmt/format.h>

#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

#include "creature/anycr.h"
#include "creature/creature.h"
#include "map/fov.h"
#include "creature/std_ai.h"
#include "creature/xhero.h"
#include "game/game.h"
#include "helpers/msgwin.h"
#include "magic/modifier.h"
#include "map/map_objects.h"

#include <sol/sol.hpp>

// XCreature is never itself a dynamic type - every actual creature is
// a concrete subclass (XAnyCreature, XHero, the uniques), each with
// its own CEREAL_REGISTER_TYPE against XCreature - this just extends
// the polymorphic pointer-cast chain one more hop, up to XObject, for
// XScheduler::Entry's shared_ptr<XObject>.
CEREAL_REGISTER_POLYMORPHIC_RELATION(XBaseObject, XCreature);

// a creature which is currently being displayed
XCreature* XCreature::main_creature = nullptr;

XCreatureGroupMap XCreature::group_members = XCreatureGroupMap();

// Defined here rather than inline in creature.h: XHero derives from
// XCreature, so creature.h can never see a complete XHero to dynamic_cast
// against - same circular-include shape as XLocation::GroupId earlier.
bool XCreature::isHero() const
{
    return dynamic_cast<const XHero*>(this) != nullptr;
}

void XCreature::RegisterLua(sol::state_view& lua)
{
    lua.new_enum("CreatureSize",
        "VERY_SMALL", Size::VERY_SMALL,
        "SMALL", Size::SMALL,
        "NORMAL", Size::NORMAL,
        "LARGE", Size::LARGE,
        "VERY_LARGE", Size::VERY_LARGE
    );

    lua.new_enum("Gender",
        "MALE", XCreature::MALE,
        "FEMALE", XCreature::FEMALE,
        "NEUTER", XCreature::NEUTER
    );

    lua.new_enum("PersonType",
        "IT", XCreature::IT,
        "HE", XCreature::HE,
        "SHE", XCreature::SHE,
        "NAMED_HE", XCreature::NAMED_HE,
        "NAMED_SHE", XCreature::NAMED_SHE,
        "NAMED_IT", XCreature::NAMED_IT,
        "THEY", XCreature::THEY,
        "NAMED_THEY", XCreature::NAMED_THEY
    );

    // Real C++ methods/properties, not one-off void*-taking free functions -
    // reachable from Lua via AsCreature(void*) on any existing void* handle
    // (event_handler dispatch, FindCreature, etc.), which still pass plain
    // void* under the hood (verified: a usertype-wrapped pointer can't be
    // read back correctly by a void*-parameter function, so the existing
    // void* dispatch plumbing had to stay as-is rather than switch to
    // passing real XCreature* to it).
    lua.new_usertype<XCreature>("XCreature",
        "MoneyOp", &XCreature::MoneyOp,
        "IsCreatureVisible", [](XCreature& cr, XCreature* target) { return cr.isCreatureVisible(target) != 0; },
        "isHero", &XCreature::isHero,
        "hp", sol::readonly(&XCreature::HP),
        "max_hp", sol::property(&XCreature::GetMaxHP),
        "name", &XMapObject::name,
        "ContainItem", &XCreature::ContainItem,
        "IsMale", [](XCreature& cr) { return static_cast<bool>(cr.creature_person_type & XCreature::HE); },
        "xai", sol::property([](XCreature& cr) -> XStandardAI* { return cr.xai.get(); }),
        "religion", &XCreature::religion,
        "IsWearingItemType", [](XCreature& cr, int bodypart, int slot, ItemType it) {
            XBodyPart* bp = cr.GetBodyPart((BODY_PART)bodypart, slot);
            return bp && bp->Item() && bp->Item()->it == it;
        },
        // Wear() is a no-op (returns without swapping) if the slot is
        // already occupied - clear it first, same as XBandit's old
        // cloak-swap ctor did before this became a generic Lua primitive.
        //
        // Named PutOnBody, not the more obvious WearItem/EquipItem/
        // SetWornItem: empirically, each of those three specific strings,
        // bound here as this exact method (same signature, same or even
        // empty body), reproducibly corrupts something elsewhere in the
        // Lua state - AsCreature(x).xai:someMethod() on an unrelated
        // creature starts throwing "attempt to index field 'xai' (a
        // userdata value)" on every subsequent run. Confirmed it's the
        // string, not the logic: an identical-signature method with a
        // nonsense name ("FooBarBaz") and the exact same body is 100%
        // stable across repeated fresh runs, and swapping only the name
        // back to any of the three above reintroduces the corruption
        // deterministically. Root cause not identified (smells like a
        // hash collision in sol2 or LuaJIT's own string interning, since
        // it reproduces independent of parameter types/count/body content
        // and depends only on the bound name string) - if this resurfaces
        // when renaming/adding usertype methods elsewhere, suspect this
        // class of bug before assuming a logic error, and verify any
        // fix (or any new method name) with several repeated fresh runs,
        // not just one - this does not reproduce on every single run of
        // a build that has NOT changed, only across genuinely different
        // registered-string sets.
        "PutOnBody", [](XCreature& cr, int bodypart, int slot, void* item_ptr) {
            XItem* item = (XItem*)item_ptr;
            XBodyPart* bp = cr.GetBodyPart((BODY_PART)bodypart, slot);

            // Not every creature has every slot.
            if (!bp) {
                std::cerr << "world: " << cr.name << " has no body part "
                          << bodypart << " to put " << item->toString()
                          << " on" << std::endl;

                return;
            }

            if (bp->Item()) {
                auto old_item = bp->UnWear();

                if (auto it = cr.contain.find(old_item); it != cr.contain.end()) {
                    cr.contain.erase(it);
                }

                old_item->Invalidate();
            }

            bp->Wear(item);
        }
    );
}

XCreature::XCreature()
{
    total_cr++;

    nx = -1;
    ny = -1;
    added_DV = 0;
    added_PV = 0;
    added_slow_digestion = 0;
    digestion_phase = 0;
    added_HIT = 0;
    added_DMG = 0;
    added_range = 0;
    carried_weight = 0;

    base_nutrio = 500;
    nutrio = 5000;
    nutrio_speed = 10;

    experience = 0;
    level = 1;
    RNG = 3;

    creature_size = Size::NORMAL;
    creature_person_type = XCreature::HE;

    xai = std::make_unique<XStandardAI>(this);
    md = new XModifier();
    m = new XMagic();
    sk = new XSkills();
    wsk = new XCombatSkills();

    weight = 1000;

    creature_class = CRC_NONE;

    tactics = TS_NORMAL;
    group_id = GID_NONE;
    food_feeling = FF_NORMAL;
}

void XCreature::OnInvalidate()
{
    components.clear();

    contain.InvalidateAll();

    if (action_data.item) {
        action_data.item->Invalidate();
        action_data.item = nullptr;
    }

    delete sk;
    sk = nullptr;

    if (md) {
        delete md;
        md = nullptr;
    }

    delete m;
    m = nullptr;

    delete wsk;
    wsk = nullptr;

    delete xai.release();

    event_handler.clear();

    // remove perished creature from the group members list
    if (group_id != GID_NONE) {
        auto group = group_members.equal_range(group_id);

        for (auto el = group.first; el != group.second;) {
            if (el->second == this) {
                el = group_members.erase(el);
            } else {
                ++el;
            }
        }
    }

    total_cr--;

    XBaseObject::OnInvalidate();
}

void XCreature::Regenerate()
{
    if (HP < GetMaxHP()) {
        XSkill * xsk = sk->GetSkill(XSkill::Skill::HEALING);
        int val = 1;

        if (xsk) {
            val += xsk->GetLevel();
        }

        if (vRand(20) < val) {
            int rest = MAX_HP / 100 + 1;

            if (xsk) {
                xsk->UseSkill();
                rest *= vRand((int)xsk->GetMastery() + 1);
            }

            onHeal(rest);
        }
    }

    if (PP < GetMaxPP()) {
        XSkill * xsk = sk->GetSkill(XSkill::Skill::CONCENTRATION);
        int val = 1;

        if (xsk) {
            val += xsk->GetLevel();
        }

        if (vRand() % 20 < val) {
            int rest = MAX_PP / 100 + 1;

            if (xsk) {
                xsk->UseSkill();
                rest *= (int)xsk->GetMastery();
            }

            onRestorePP(rest);
        }
    }
}

int XCreature::onHeal(int _hp)
{
    int last_HP = HP;
    int max_HP = GetMaxHP();
    HP += _hp;

    if (HP > max_HP) {
        HP = max_HP;
    }

    return last_HP >= max_HP ? 0 : 1;
}

int XCreature::onRestorePP(int _pp)
{
    int last_PP = PP;
    int max_PP = GetMaxPP();
    PP += _pp;

    if (PP > max_PP) {
        PP = max_PP;
    }

    return last_PP >= max_PP ? 0 : 1;
}

void XCreature::setGroupID(const GROUP_ID& gid)
{
    if (gid == GID_NONE) return;

    group_id = gid;
    group_members.insert(std::make_pair(gid, this));
}

std::vector<XCreature*> XCreature::getGroupMembers() const
{
    std::vector<XCreature*> result{};

    if (group_id != GID_NONE) {
        auto [begin, end] = group_members.equal_range(group_id);
        for (auto it = begin; it != end; ++it) {
            result.push_back(it->second);
        }
    }

    return result;
}

void XCreature::stopAction()
{
    if (action_data.action == A_USE_TOOL) {
        if (auto* tool = dynamic_cast<XTool*>(action_data.item.get())) {
            tool->onUse(XTool::FINISH, this);
        }
    } else {
        // Back into the pack, so an interrupted meal or an unfinished
        // book is not lost. Only if it still exists: a corpse rots on
        // its own schedule (XCorpse::Run) and can be invalidated while
        // it is the half-eaten thing in hand, and this reference is the
        // last one keeping the memory alive. Putting that back would
        // leave a dead item in the pack the player never picked up, to
        // be eaten again later.
        if (action_data.item && action_data.item->isValid()) {
            contain.insert(action_data.item);
        }
    }

    action_data.action = A_MOVE;
    action_data.item = nullptr;

    // prevents hero to continue automove when attaked by ghosts...
    isDisturb = 0;
}

int XCreature::continueEat()
{
    auto* food = dynamic_cast<XAnyFood*>(action_data.item.get());

    // Not just "is it food" - is it still there. A corpse can rot away
    // between one bite and the next.
    if (!food || !food->isValid()) {
        stopAction();

        return 1;
    }

    assert(food->kind & ItemKind::FOOD);
    int res = food->onEat(this);

    if (res != 2) {
        action_data.action = A_MOVE;
        action_data.item = nullptr;
    }

    return 1;
}

int XCreature::Eat(XAnyFood * food)
{
    int res = food->onEat(this);

    if (res) {
        if (res == 2) {
            action_data.action = A_EAT;
            action_data.item = XItem::Own(food);
            return 1;
        } else {
            return 1;
        }
    } else {
        return 0;
    }
}

int XCreature::DecNutrio()
{
    int rate = nutrio_speed;

    // Something worn is keeping the wearer full for longer. Half the rate,
    // taken exactly: an odd rate alternates between its two halves rather
    // than rounding down every turn, which would be rather more than half.
    if (added_slow_digestion > 0) {
        rate = (nutrio_speed + digestion_phase) / 2;
        digestion_phase ^= 1;

        if (rate < 1) {
            rate = 1;
        }
    }

    nutrio -= rate;

    if (nutrio < 0) {
        Die(nullptr);
        return 0;
    }

    return 1;
}

bool XCreature::Run()
{
    if (action_data.action == A_EAT) {
        continueEat();
    } else if (action_data.action == A_READ) {
        continueRead();
    } else if (action_data.action == A_USE_TOOL) {
        continueUseItem();
    } else {
        NewMove();
    }

    // safety net: when killed by the actions above indicate the object
    // must be removed from the scheduler and deleted.
    if (!isValid())
        return false;

    if (md && md->Run(this)) {
        int athletics = sk->GetLevel(XSkill::Skill::ATHLETICS);

        if (GetCarryState() >= CSTATE_STRAINED) {
            if (vRand(3000 / (5 + athletics)) == 0) {
                GainAttr(XStats::STR, 1);
                sk->UseSkill(XSkill::Skill::ATHLETICS, 10);
            }
        } else if (vRand(6000 / (5 + athletics)) == 0) {
            GainAttr(XStats::DEX, 1);
            sk->UseSkill(XSkill::Skill::ATHLETICS, 10);
        }

        DoMove();
    }

    if (ttm <= 0) {
        ttm += GetSpeed();
    }

    return isValid();
}

// A door in the way, and hands to open it with. A creature with no hand to
// pull with still cannot: a rat is stopped by a closed door, as it should be.
bool XCreature::OpenTheWay()
{
    if (nx == x && ny == y) {
        return false;
    }

    auto* door = dynamic_cast<XDoor *>(l->map->GetSpecial(nx, ny));

    if (!door || door->isOpened || !GetBodyPart(BP_HAND)) {
        return false;
    }

    door->Switch();

    if (isInVisibleArea()) {
        msgwin.Add(fmt::format("{} {} the door.", GetNameEx(CRN_T1), GetVerb("open")));
    }

    return true;
}

void XCreature::DoMove()
{
    if (l->map->XGetMovability(nx, ny) == 2 && (nx != x || ny != y)) {
        Attack();
    } else if (TestMove() || (x == nx && y == ny)) {
        Move();
    } else {
        // Blocked. If it is a door and this one has hands, working it is
        // what the turn is spent on; the step itself comes next turn.
        OpenTheWay();
    }
}

void XCreature::Move()
{
    // Demo/attract mode ("-demo") has no real hero - whichever creature is
    // flagged main_creature (see SetMainCreature(), Lua-callable) stands in
    // for one, so the same view-refresh sequence XHero drives itself needs
    // to run here too. Was XBeelzvile-specific (creature/unique.cpp) before
    // being generalized to any main_creature, since it's demo-mode
    // plumbing, not creature-specific behavior.
    if (Game.isDemo() && this == main_creature) {
        HideOldView();
        ShowNewView();
    }

    if (wants_move_hook && !event_handler.empty()) {
        sol::state_view lua(XLua::State());
        lua[event_handler](LuaEvent::PRE_MOVE, (void*)this);
    }

    XMapObject * tobj = l->map->GetSpecial(x, y);

    if (auto* ttrap = dynamic_cast<XTrap *>(tobj)) {
        //we can move easy from pit or web
        if ((nx != x || ny != y) && !ttrap->MoveOut(this)) {
            nx = x;
            ny = y;

            if (!isValid()) {
                return;
            }
        }
    }

    XAnyPlace * new_place = l->map->GetPlace(nx, ny);
    XAnyPlace * old_place = l->map->GetPlace(x, y);

    if (old_place && new_place) {
        new_place->onCreatureMove(this);
    } else if (new_place && !old_place) {
        new_place->onCreatureEnter(this);
    } else if (old_place && !new_place) {
        old_place->onCreatureLeave(this);
    }

    //check if die when moved.
    if (!isValid()) {
        return;
    }

    Regenerate();
    l->map->ResMonster(x, y);
    l->map->SetMonster(nx, ny, this);

    int flag = 1;

    if (x == nx && y == ny) {
        flag = 0;
    }

    x = nx;
    y = ny;

    if (flag) {
        XMapObject * obj = l->map->GetSpecial(x, y);

        if (auto* mtrap = dynamic_cast<XTrap *>(obj)) {
            mtrap->MoveIn(this);
        } else if (auto* mtel = dynamic_cast<XTeleport *>(obj)) {
            mtel->MoveIn(this);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////

// The creature's own view of its location: what it can see stops at
// anything the map says light does not pass, and everything seen is
// marked on the map so the screen can draw it lit.
namespace {

class XCreatureView final : public XFieldOfView
{
    public:
        XCreatureView(XCreature* _mover, XMap* _map, bool _lit) : mover(_mover), map(_map), lit(_lit) {}

    protected:
        [[nodiscard]] bool BlocksLight(const int x, const int y) const override
        {
            if (x < 0 || x >= map->len || y < 0 || y >= map->hgt) {
                return true;
            }

            return map->GetVisibility(x, y) == 0;
        }

        void MarkVisible(const int x, const int y) override
        {
            if (x < 0 || x >= map->len || y < 0 || y >= map->hgt) {
                return;
            }

            if (lit) {
                map->SetVisible(x, y);
            } else {
                map->ResVisible(x, y);
            }

            // Anything hostile in view is worth waking up for.
            if (XCreature* tcr = map->GetMonster(x, y);
                lit && tcr && tcr != mover && mover->isCreatureVisible(tcr) && tcr->xai->isEnemy(mover)) {
                mover->isDisturb = 0;
            }
        }

    private:
        XCreature* mover;
        XMap* map;
        bool lit;
};

} // namespace

// How far this creature sees here: its own eyes, or the light of the
// place if that carries further - daylight over a valley, against a
// torch in a cave.
int XCreature::GetSightRange() const
{
    return std::max(const_cast<XCreature*>(this)->GetVisibleRadius(), l->sight_range);
}

void XCreature::HideOldView()
{
    XCreatureView view(this, l->map, false);

    view.Compute(x, y, GetSightRange());
}

void XCreature::ShowNewView()
{
    XCreatureView view(this, l->map, true);

    view.Compute(nx, ny, GetSightRange());
}

void XCreature::PutStatus()
{
#ifdef __EMSCRIPTEN__
    be_status_rows(size_y - 3, 3); // RVIP: the Status window
#endif
    vSetAttr(xLIGHTGRAY);

    for (int row = size_y - 3; row < size_y; row++) {
        vGotoXY(0, row);
        vClrEol();
    }

    vGotoXY(0, size_y - 3);
    vPutS(name);

    vGotoXY(0, size_y - 2);
    vPutS(fmt::format("DV/PV:{}/{}  ", GetDV(), GetPV()));

    vGotoXY(15, size_y - 3);
    vPutS(fmt::format(
        "St:{:<2} Dx:{:<2} To:{:<2} Le:{:<2} Wi:{:<2} Ma:{:<2} Pe:{:<2} Ch:{:<2} Sp:{:<3} L:{}",
        GetStats(XStats::STR),
        GetStats(XStats::DEX),
        GetStats(XStats::TOU),
        GetStats(XStats::LEN),
        GetStats(XStats::WIL),
        GetStats(XStats::MAN),
        GetStats(XStats::PER),
        GetStats(XStats::CHR),
        100000 / GetSpeed(),
        l->GetBriefName()));

    vGotoXY(14, size_y - 2);
    vPutS(fmt::format("HP:{}({})  PP:{}({})  ", HP, GetMaxHP(), PP, GetMaxPP()));

    vGotoXY(38, size_y - 2);
    vPutS(fmt::format("Exp({}){}", level, experience));

#ifdef _DEBUG
    vGotoXY(60, size_y - 2);
    vPutS(fmt::format("x:y[{}:{}]", x, y));
#endif

    vGotoXY(0, size_y - 1);

    if (nutrio > base_nutrio * 18) {
        vPutS("<SEVERITY_SEVERE>overfed! <TEXT>");
    } else if (nutrio > base_nutrio * 14) {
        vPutS("bloated ");
    } else if (nutrio > base_nutrio * 10 && nutrio <= base_nutrio * 14) {
        vPutS("satiated ");
    } else if (nutrio > base_nutrio * 8 && nutrio <= base_nutrio * 10) {
        vPutS("");
    } else if (nutrio > base_nutrio * 6 && nutrio <= base_nutrio * 8) {
        vPutS("hungry ");
    } else if (nutrio > base_nutrio * 4 && nutrio <= base_nutrio * 6) {
        vPutS("<SEVERITY_NOTABLE>very hungry <TEXT>");

        if (action_data.action != A_EAT) {
            stopAction();
        }
    } else if (nutrio > base_nutrio && nutrio <= base_nutrio * 4) {
        vPutS( "<SEVERITY_SEVERE>weak <TEXT>");

        if (action_data.action != A_EAT) {
            stopAction();
        }
    } else if (nutrio <= base_nutrio) {
        vPutS("<SEVERITY_SEVERE>dying! <TEXT>");

        if (action_data.action != A_EAT) {
            stopAction();
        }
    }

    switch (action_data.action) {
        case A_READ	:
            vPutS("[reading] ");
            break;

        case A_EAT	:
            vPutS("[eating] ");
            break;

        case A_USE_TOOL	:
            vPutS("[using tool] ");
            break;

        // Moving, attacking and casting take one turn each: there is no
        // action in progress to announce.
        default:
            break;
    }

    CARRY_STATE cstate = GetCarryState();

    switch (cstate) {
        case CSTATE_NORMAL:
            break;

        case CSTATE_BURDENED:
            vPutS("burdened ");
            break;

        case CSTATE_STRAINED:
            vPutS("strained ");
            break;

        case CSTATE_OVERBURDEN:
            vPutS("overburdened ");
            break;

        default :
            break;
    };

    vPutS(md->toString());
    vSetAttr(xLIGHTGRAY);
}

void XCreature::NewMove()
{
    // See the matching comment in Move() - same demo-mode main_creature
    // stand-in, same generalized-from-XBeelzvile reasoning.
    if (Game.isDemo() && this == main_creature) {
        l->map->Center(x, y);
        l->map->Put(this);
        PutStatus();
        vRefresh();
        msgwin.ClrMsg();
    }

    if (wants_move_hook && !event_handler.empty()) {
        sol::state_view lua(XLua::State());
        lua[event_handler](LuaEvent::AI_TURN, (void*)this);
    }

    xai->Move();
}

int XCreature::GetSpeed()
{
    int speed = ttmb;

    if (nutrio < base_nutrio * 8 && nutrio > base_nutrio * 4) {
        speed = (int)(speed * 0.92);
    } else if (nutrio < base_nutrio * 2) {
        speed = (int)(speed * 1.2);
    } else if (nutrio > base_nutrio * 12) {
        speed = (int)(speed * 1.1);
    }

    int str = stats->Get(XStats::STR);

    if (carried_weight >= str * 120 && carried_weight < str * 200) {
        speed = (int)(speed * 1.1);
    } else if (carried_weight >= str * 200 && carried_weight < str * 280) {
        speed = (int)(speed * 1.3);
    } else if (carried_weight >= str * 280) {
        speed = (int)(speed * 2);
    }

    return speed;
}

int XCreature::TestMove()
{
    if (l->map->XGetMovability(nx, ny) == 0) {
        return 1;
    } else {
        return 0;
    }
}

int XCreature::GetHIT()
{
    int tht = to_hit + added_HIT;
    return tht + GetTacticsHITBonus();
}

int XCreature::GetDV(XCreature * attacker)
{
    int tdv = added_DV + dv + GetTacticsDVBonus() + GetShieldDVBonus();

    // Dodging is stepping out of the way of a blow you saw coming, so there is
    // nothing to dodge from an attacker this creature cannot see - the same
    // asymmetric test a backstab uses from the other side.
    // A null attacker means nobody in particular is asking (the status line
    // drawing DV, a missile working out its odds, the strength rating).
    if (const int dodge = sk->GetLevel(XSkill::Skill::DODGE);
        dodge > 0 && (!attacker || isCreatureVisible(attacker))) {
        tdv += dodge;
    }

    return tdv < 1 ? 1 : tdv;
}

int XCreature::GetShieldDVBonus()
{
    for (auto& xbp: components)
    {
        XItem* i = xbp->Item();

        if (i && i->kind == ItemKind::SHIELD) {
            int shld_skl = wsk->GetDV(wsk->Best(CombatRole::SHIELD));
            int shield_dv = i->dv;

            if (i->dv < shld_skl) {
                return i->dv + shield_dv;
            } else {
                return shld_skl + shield_dv;
            }
        }
    }

    return 0;
}

int XCreature::GetDMG()
{
    //this function don't include 'hand' damage
    //i.e. calculate additional damage. i.e. +dmg
    return added_DMG + GetTacticsDMGBonus();
}

int XCreature::GetTacticsDVBonus()
{
    switch (tactics) {
        case TS_COWARD	:
            return (3 * (GetStats(XStats::DEX) + sk->GetLevel(XSkill::Skill::TACTICS))) / 2;
            break;

        case TS_DEFENSIVE	:
            return GetStats(XStats::DEX) + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_NORMAL	:
            return (2 * GetStats(XStats::DEX)) / 3 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_AGGRESSIVE	:
            return GetStats(XStats::DEX) / 3 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_BERSERKER	:
            return GetStats(XStats::DEX) / 10 + sk->GetLevel(XSkill::Skill::TACTICS);
            break; //compensate DV given by Dx

        default	:
            assert(0);
    }

    return 0;
}

int XCreature::GetTacticsHITBonus()
{
    switch (tactics) {
        case TS_COWARD	:
            return GetStats(XStats::DEX) / 10 + sk->GetLevel(XSkill::Skill::TACTICS) - 5;
            break;

        case TS_DEFENSIVE	:
            return GetStats(XStats::DEX) / 7 + sk->GetLevel(XSkill::Skill::TACTICS) - 3;
            break;

        case TS_NORMAL	:
            return GetStats(XStats::DEX) / 4 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_AGGRESSIVE	:
            return GetStats(XStats::DEX) / 3 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_BERSERKER	:
            return GetStats(XStats::DEX) / 2 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        default	:
            assert(0);
    }

    return 0;
}

int XCreature::GetTacticsDMGBonus()
{
    switch (tactics) {
        case TS_COWARD	:
            return GetStats(XStats::STR) / 20 + sk->GetLevel(XSkill::Skill::TACTICS) - 3;
            break;

        case TS_DEFENSIVE	:
            return GetStats(XStats::STR) / 10 + sk->GetLevel(XSkill::Skill::TACTICS) - 1;
            break;

        case TS_NORMAL	:
            return GetStats(XStats::STR) / 7 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_AGGRESSIVE	:
            return GetStats(XStats::STR) / 5 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        case TS_BERSERKER	:
            return GetStats(XStats::STR) / 2 + sk->GetLevel(XSkill::Skill::TACTICS);
            break;

        default	:
            assert(0);
    }

    return 0;
}

int XCreature::GetPV()
{
    return pv + added_PV + GetStats(XStats::TOU) / 10;
}

int XCreature::GainAttr(XStats::Id st, int val)
{
    int cur = stats->Get(st);
    int max = max_stats.Get(st);

    if (val > 0) {
        if (cur < max) {
            if (cur + val > max) {
                val = max - cur;
            }

            stats->Modify(st, val);

            if (isHero()) {
                switch (st) {
                    case XStats::STR:
                        msgwin.Add("You feel stronger!");
                        break;

                    case XStats::DEX:
                        msgwin.Add("You are becoming more graceful!");
                        break;

                    case XStats::TOU:
                        msgwin.Add("Your health increases!");
                        break;

                    case XStats::MAN:
                        msgwin.Add("You feel power surging through your body!");
                        break;

                    case XStats::WIL:
                        msgwin.Add("You feel more powerful!");
                        break;

                    case XStats::LEN:
                        msgwin.Add("You feel smarter!");
                        break;

                    case XStats::PER:
                        msgwin.Add("You feel more perceptive!");
                        break;

                    case XStats::CHR:
                        msgwin.Add("Your beauty improves!");
                        break;

                    default:
                        break;
                }
            }

            return 1;
        }
    } else {
        if (cur - val > 1) {
            stats->Modify(st, val);

            if (isHero()) {
                switch (st) {
                    case XStats::STR:
                        msgwin.Add("Your muscles weaken!");
                        break;

                    case XStats::DEX:
                        msgwin.Add("You feel clumsy!");
                        break;

                    case XStats::TOU:
                        msgwin.Add("You feel like you might be getting sick!");
                        break;

                    case XStats::MAN:
                        msgwin.Add("You feel power draining from your body!");
                        break;

                    case XStats::WIL:
                        msgwin.Add("You feel diminished!");
                        break;

                    case XStats::LEN:
                        msgwin.Add("Thinking becomes more difficult!");
                        break;

                    case XStats::PER:
                        msgwin.Add("Your senses dull!");
                        break;

                    case XStats::CHR:
                        msgwin.Add("Your features harden!");
                        break;

                    default:
                        break;
                }
            }

            return 1;
        }
    }

    return 0;
}

void XCreature::GainResist(const RESISTANCE& rs, int val)
{
    resistances->ChangeResistance(rs, val);

    // What the body notices, if world/resistances.lua wrote anything for
    // this one. Thirteen of the eighteen had no text at all before, and
    // said nothing; a row that still says nothing still says nothing.
    if (const ResistanceStats* row = FindResistance(rs)) {
        const std::string& text = val > 0 ? row->gained : row->lost;

        if (!text.empty()) {
            msgwin.Add(text);
        }
    }
}

int XCreature::GetStats(XStats::Id st)
{
    assert(st > XStats::UNKNOWN && st < XStats::COUNT);

    int res = stats->Get(st) + added_stats.Get(st);
    return res > 0 ? res : 1;
}

int XCreature::GetResistance(RESISTANCE tr)
{
    assert(resistances);
    return resistances->GetResistance(tr) + added_resists.GetResistance(tr);

}

void XCreature::Die(XCreature* killer)
{
    assert(isValid());

    // LastStep() below drops the map cell's shared_ptr reference (pMonster),
    // which may be the only thing keeping this object alive. Keep ourselves
    // alive for the rest of this function regardless.
    auto self = shared_from_this();

    sol::state_view lua(XLua::State());

    if (!event_handler.empty()) {
        // this/killer stay void*, not XCreature* -  killer can legitimately be
        // nullptr (e.g. DecNutrio()'s Die(nullptr))
        lua[event_handler](LuaEvent::DIE, (void*)this, (void*)killer);
    }

    // World-level death notification, in addition to the actor's own handler
    // above. Content that has to know about deaths it never placed a handler
    // on - kill tallies, faction bookkeeping, a bounty board - defines
    // OnCreatureDie() in world/; a world that defines none pays one global
    // lookup per death. The creature's class comes along as an argument
    // rather than through a new XCreature binding, deliberately: see the
    // string-interning hazard documented at XCreature::RegisterLua().
    //
    // Protected, unlike the handler above: a fault in a bookkeeping script
    // must not take the game down in the middle of a death.
    if (sol::protected_function on_die = lua["OnCreatureDie"]; on_die.valid()) {
        if (const auto result = on_die((void*)this, (void*)killer, creature_class);
            !result.valid()) {
            const sol::error err = result;
            std::cerr << "world: OnCreatureDie: " << err.what() << std::endl;
        }
    }

    // Unwear everything first, firing onUnWear() side effects - the items
    // themselves stay in contain regardless (see XBodyPart::Wear()), so
    // the single drop loop below picks up worn and carried items alike.
    for (auto& bp: components) {
        if (bp->Item()) {
            bp->UnWear();
        }
    }

    for (auto item: contain) {
        // Never drop an already-invalid item as loot - putting one on the
        // ground leaves a corpse-of-an-item in a cell's item_list.
        // XItem::OnInvalidate() now detaches carried items from contain
        // itself, so this should be unreachable; kept as cheap
        // defence-in-depth for the one state known to be unrecoverable
        // downstream (~XMapTile tolerates it, but only just).
        if (item->isValid()) {
            item->Drop(l, x, y);
        }
    }

    // Drop() moves each item onto the ground but leaves it in contain -
    // without clearing it here, Invalidate() below would walk contain again
    // and delete every item just dropped, instead of leaving it as loot.
    contain.clear();

    LastStep();

    if (killer && killer != this && !dynamic_cast<XFakeCreature*>(killer)) {
        xai->onDie(killer);
        killer->religion.KillCreature(killer, this);
        killer->AddExp(GetExp());
    }

    Invalidate();
}

bool XCreature::DropItem(XItem* i)
{
    XAnyPlace * place = l->map->GetPlace(x, y);
    bool flag = true;

    if (place) {
        flag = place->onCreatureDropItem(this, i);
    }

    if (flag) {
        // Adjust weight
        UnCarryItem(i);

        // Drop() (via XMap::PutItem -> XItem::Own()) establishes the
        // ground's item_list as a second owner of i before we let go of
        // contain's reference below - if contain was i's only reference,
        // erasing it first (before the item is anywhere else) would run
        // Own()'s deleter on a still-valid item and invalidate it outright
        // instead of just moving it to the ground.
        i->Drop(l, x, y);

        if (auto it = contain.find(i); it != contain.end()) {
            contain.erase(it);
        }
    }

    return flag;
}

bool XCreature::PickUpItem(XItem* i)
{
    XAnyPlace * place = l->map->GetPlace(x, y);
    bool flag = true;

    if (place) {
        flag = place->onCreaturePickItem(this, i);
    }

    if (flag) {
        if (CarryItem(i)) {
            i->x = -1;
            i->y = -1;

            // If picked item is a missile the creature is shooting with, add
            // it to quiver instead of backpack
            XBodyPart * xbp = GetBodyPart(BP_MISSILE);

            if (xbp && xbp->Item() && xbp->Item()->Compare(i)) {
                xbp->Item()->Concat(i);
            } else {
                contain.insert(XItem::Own(i));
            }

            return true;
        } else {
            // if we can't pick item, then drop it
            if (isHero()) {
                msgwin.ClrMsg();
                msgwin.Add(fmt::format("{} is too heavy for you!", i->toSentence(XItem::Article::DEFINITE)));
            }

            int tx = i->x;
            int ty = i->y;
            i->x = -1;
            i->y = -1;

            if (place) {
                place->onCreatureDropItem(this, i);
            }

            i->x = tx;
            i->y = ty;

            return false;
        }
    } else {
        return false;
    }
}

XCreature::Gender XCreature::GetGender()
{
    switch (creature_person_type) {
        case XCreature::HE:
        case XCreature::NAMED_HE:
        case XCreature::MALE_YOU:
            return XCreature::MALE;
            break;

        case XCreature::SHE:
        case XCreature::NAMED_SHE:
        case XCreature::FEMALE_YOU:
            return XCreature::FEMALE;
            break;

        default:
            break;
    }

    return XCreature::NEUTER;
}

int XCreature::GetMaxHP()
{
    return MAX_HP + (MAX_HP * GetStats(XStats::TOU)) / 20;
}

int XCreature::GetMaxPP()
{
    return MAX_PP + (MAX_PP * GetStats(XStats::MAN)) / 10;
}

int XCreature::GetExp() const
{
    return base_exp + experience / 10;
}

void XCreature::AddExp(unsigned long exp)
{
    experience += exp;

    while (ExpOfLevel(level) <= experience) {
        IncLevel();
    }
}

void XCreature::IncLevel()
{
    MAX_HP += vRand((GetStats(XStats::TOU) / 5) + 1) + 1;
    MAX_PP += vRand((GetStats(XStats::MAN) / 2) + 1) + 1;
    level++;
}

std::weak_ptr<XCreature> XCreature::ToWeakPtr(XCreature * cr)
{
    // A creature isn't shared_from_this()-safe until it's been placed on
    // the map for the first time (see XMap::SetMonster's birth path) -
    // notably, that's after its own constructor has already returned, so
    // this can be reached for a creature referencing itself mid-construction
    // (e.g. XAnyCreature equipping its own starting gear). Guard against
    // that rather than letting shared_from_this() throw std::bad_weak_ptr.
    if (cr && cr->isValid() && !cr->weak_from_this().expired()) {
        return std::static_pointer_cast<XCreature>(cr->shared_from_this());
    }

    return {};
}

unsigned long XCreature::ExpOfLevel(const int lev) const
{
    return static_cast<unsigned long>(2.0 * base_exp * std::pow(static_cast<float>(lev), 2.5f));
}

// A weapon heavier than its wielder can comfortably manage is hard to aim.
// The measure is 30 * strength against the weapon's weight, halved again
// when the other hand is full too - there is then no free hand left to
// steady the blow. At or above that weight the penalty grows with the
// logarithm of how far over the creature is; below it there is no bonus
// for swinging something light, hence the clamp at zero.
int XCreature::GetUnwieldyHITPenalty(XItem* weapon)
{
    XItem * h1 = GetItem(BP_HAND, 0);
    XItem * h2 = GetItem(BP_HAND, 1);
    int mult = (h1 && h2) ? 2 : 1;
    float f = (float)(5.0 * log((300.0 * GetStats(XStats::STR)) / (10.0 * (weapon->weight) * mult)));
    return std::min((int)f, 0);
}

// The same weapon also lands with less of the wielder behind it: a blow
// you are fighting to control does not carry your weight into the target.
// It costs half of what the aim costs and no more, because the weapon's
// own mass arrives whether or not you meant it to.
int XCreature::GetUnwieldyDMGPenalty(XItem* weapon)
{
    return GetUnwieldyHITPenalty(weapon) / 2;
}

XBodyPart* XCreature::GetRNDBodyPart()
{
    int value = 0;
    for (auto& bp: components) {
        value += bp->GetPartSize();
    }

    int v = value > 0 ? vRand() % value : 0;

    for (auto& bp: components) {
        v -= bp->GetPartSize();

        if (v <= 0)
            return bp.get();
    }

    return nullptr;
}

XBodyPart* XCreature::GetRNDBodyPart(ItemKind kind, RBP_FLAG rbpf)
{
    if (rbpf == RBP_BLOCK && kind & ItemKind::SHIELD) {
        auto bpi = std::find_if(
            components.begin(),
            components.end(),
            [](const std::unique_ptr<XBodyPart>& xbp) { return xbp->Item() && xbp->Item()->kind & ItemKind::SHIELD; }
        );

        if (bpi != components.end() && (vRand() % 100 < 5 * wsk->GetLevel(wsk->Best(CombatRole::SHIELD)) + 5)) {
            return bpi->get();
        }
    }

    int count = 0;
    for (auto& xbp: components) {
        if (xbp->GetProperKind() & kind) {
            count++;
        }
    }

    if (count == 0) {
        return nullptr;
    }

    int n = vRand() % count;

    count = 0;
    for (const auto& xbp: components) {
        if (xbp->GetProperKind() & kind) {
            if (n == count) {
                return xbp.get();
            }
            count++;
        }
    }

    assert(0);
    return nullptr;
}

const char* XCreature::GetWoundMsg(int flag)
{
    float rel = (float)(GetMaxHP()) / ((float)HP);

    if (rel <= 1.0) {
        if (flag) {
            return "";
        } else {
            return "not wounded";
        }
    } else if (rel < 1.3) {
        if (flag) {
            return "slightly wound";
        } else {
            return "slightly wounded";
        }
    } else if (rel < 2.0) {
        if (flag) {
            return "wound";
        } else {
            return "wounded";
        }
    } else if (rel < 3.0) {
        if (flag) {
            return "seriously wound";
        } else {
            return "seriously wounded";
        }
    } else {
        if (flag) {
            return "critically wound";
        } else {
            return "critically wounded";
        }
    }
}

void XCreature::MoveStairWay()
{
    XCreature * tc = this;
    XLocation * xl = l;

    XMapObject * spec = xl->map->GetSpecial(tc->x, tc->y);
    XStairWay * way = dynamic_cast<XStairWay *>(spec);

    if (way) {
        XLocation * tgtloc = Game.Location(way->ln).get();

        // A world script may name a location that was never built, or
        // build a stairway with nothing leading back - XLocation::
        // ValidateWorld()/ValidateWays() have already named both - but
        // the engine must not walk off a map over it.
        if (!tgtloc) {
            return;
        }

        int tgt_x = way->dest_x;
        int tgt_y = way->dest_y;

        if (tgt_x < 0 || tgt_y < 0) {
            const auto free_xy = tgtloc->GetFreeXY();

            if (!free_xy) {
                return;
            }

            tgt_x = free_xy->x;
            tgt_y = free_xy->y;
        }

        int n_x = tgt_x;
        int n_y = tgt_y;

        if (tgtloc->map->XGetMovability(tgt_x, tgt_y) != 0) {
            for (int i = -1; i < 2; i++)
                for (int j = -1; j < 2; j++)
                    if (tgtloc->map->XGetMovability(tgt_x + i, tgt_y + j) == 0) {
                        n_x = i + tgt_x;
                        n_y = j + tgt_y;
                    }
        }

        if (!tgtloc->map->GetMonster(n_x, n_y)) {
            // tc is ordinarily the creature whose own turn is executing
            // (protected by the scheduler's own strong ref for the
            // duration of Run()), same as every other LastStep()-then-
            // FirstStep() call site - but unlike those, this one is also
            // reachable from XStandardAI::MoveTo()'s cross-location
            // branch for a *companion* crossing a stairway to follow its
            // leader.
            auto tc_keepalive = std::static_pointer_cast<XCreature>(tc->shared_from_this());
            tc->LastStep();
            tc->FirstStep(n_x, n_y, tgtloc);
            tc->l = tgtloc;
            tc->action_data.action = A_MOVE;

            if (tc->isHero()) {
                tgtloc->visited_by_hero = 1;
                tgtloc->map->Put(tc);
                vRefresh();
            }

            return;
        } else if (tc->isHero()) {
            msgwin.Add("The way is blocked.");
        }
    }

    return;
}

void XCreature::GetRangeAttackInfo(int* range, int* hit, XDice * dmg)
{
    XItem* missile = GetItem(BP_MISSILE);
    XItem* launcher = GetItem(BP_MISSILE_WEAPON);

    if (!missile || !XMissile::isProperWeapon(missile, launcher)) {
        *range = 0;
        *hit = 0;
        dmg->Setup(0, 0, 0);
        return;
    }

    int str = stats->Get(XStats::STR);

    // added_range is the sum of every worn item's RNG, maintained
    // by XItem::onWear()/onUnWear().
    //
    // That is what finally connects the accumulation: For gear where only the
    // ammo and launcher carry an rng, this computes exactly what the two
    // explicit adds did.
    *range = added_range;
    // Built from GetHIT(), the same as a melee swing: the shooter's own
    // to-hit, whatever their gear adds, and the bonus for the stance they
    // are fighting in. Ranged attacks used to take half of dexterity
    // instead and see none of the other three, so an enchanted ring or an
    // aggressive stance helped a sword and did nothing for a bow.
    //
    // The missile's own to-hit is added explicitly - and the launcher's below,
    // because the kinds onWear() folds into added_HIT cover weapons but
    // neither missiles nor launchers. They cannot simply be marked either:
    // melee to-hit is built from GetHIT() too, so a slung bow would sharpen
    // sword swings.
    *hit = GetHIT() + missile->to_hit;
    dmg->Setup(missile->dice);

    if (launcher) {
        dmg->Add(&(launcher->dice));
        *range += wsk->GetDV(launcher->wt);
        dmg->ModifyBonus(wsk->GetDMG(launcher->wt));
        *hit += launcher->to_hit + wsk->GetHIT(launcher->wt);
    } else {
        *range += RNG + str / 25;
        dmg->ModifyBonus(str / 10);
        *range += wsk->GetDV(wsk->Best(CombatRole::THROW));
        dmg->ModifyBonus(wsk->GetDMG(wsk->Best(CombatRole::THROW)));
        *hit += wsk->GetHIT(wsk->Best(CombatRole::THROW));
    }
}

int XCreature::Shoot(int tx, int ty)
{
    if (tx == x && ty == y) {
        // can't do suicide!
        return 0;
    }

    XItem* launcher = GetItem(BP_MISSILE_WEAPON);
    XItem* missile = GetItem(BP_MISSILE);

    if (!missile) {
        // there are no missile to shoot!
        return 0;
    }

    int hit = 0;
    int range = 0;
    XDice dmg;
    GetRangeAttackInfo(&range, &hit, &dmg);
    int vis1 = isVisibleArea(x, y);
    int vis2 = isVisibleArea(tx, ty);

    if (vis1 || vis2) {
        msgwin.Add(GetNameEx(CRN_T1));

        if (launcher) {
            msgwin.Add(fmt::format("{} with {}.", GetVerb("shoot"),
                launcher->GetNameEx(XItem::Article::INDEFINITE)));
        } else {
            msgwin.Add(fmt::format("{} {}.", GetVerb("throw"),
                missile->GetNameEx(XItem::Article::INDEFINITE)));
        }
    }

    // split missile
    XItem* msl = missile->MakeCopy();
    msl->quantity = 1;

    if (--missile->quantity <= 0) {
        XBodyPart* xbp = GetBodyPart(BP_MISSILE);
        auto used_up = xbp->UnWear();

        // UnWear() doesn't remove it from contain anymore (worn items stay
        // resident there - see XBodyPart::Wear()), so this must, or
        // Invalidate() below leaves a zombie entry behind: still in
        // contain, but invalid.
        if (auto it = contain.find(used_up); it != contain.end()) {
            contain.erase(it);
        }

        used_up->Invalidate();
    }

    // fly away
    MF_DATA mfd;
    mfd.arrow_type = MFT_ARROW;
    mfd.arrow_color = xBROWN;
    mfd.l = l;
    mfd.sx = x;
    mfd.sy = y;
    mfd.ex = tx;
    mfd.ey = ty;
    mfd.to_hit = hit;
    mfd.max_range = range;
    MF_RESULT res = MissileFlight(&mfd);

    if (res == MF_HIT) {
        XCreature* target = l->map->GetMonster(mfd.pt.x, mfd.pt.y);
        DAMAGE_DATA_EX dd;
        dd.damage	= dmg.Throw();
        dd.attacker	= this;

        dd.attack_name = msl->GetNameEx(XItem::Article::DEFINITE);

        dd.attack_HIT = hit;
        dd.attack_effect = msl->aet;
        dd.flags = DF_MAGIC_BOLT;
        target->InflictDamage(&dd);

        // A hit practises the class of weapon it was made with - bows,
        // crossbows, slings, or throwing for anything flung by hand.
        if (launcher) {
            wsk->UseSkill(launcher->wt);
        } else {
            wsk->UseSkill(wsk->Best(CombatRole::THROW));
        }

    } else {
        XCreature * tgt = l->map->GetMonster(tx, ty);

        if (tgt && tgt->isVisible()) {
            msgwin.Add(tgt->GetNameEx(CRN_T1));
            msgwin.Add(tgt->GetVerb("avoid"));

            // The one just loosed at them, so "the".
            msgwin.Add("the missile.");
        }
    }

    msl->Drop(l, mfd.pt.x, mfd.pt.y);
    return 1;
}

XBodyPart* XCreature::GetBodyPart(BODY_PART bp, int count)
{
    for (auto& xbp: components) {
        if (xbp->bp_uin == bp && count-- == 0) {
            return xbp.get();
        }
    }

    return nullptr;
}

bool XCreature::CanWear(const XItem* item)
{
    return std::any_of(
        components.begin(),
        components.end(),
        [item](const std::unique_ptr<XBodyPart>& bp){ return bp->Fit(item->bp) && !bp->Item(); }
    );
}

bool XCreature::Wear(XItem* item) const {
    for (const auto& bp: components) {
        if (bp->Fit(item->bp) && !bp->Item()) {
            bp->Wear(item);
            return true;
        }
    }

    return false;
}

bool XCreature::IsWorn(const XItem* item) const {
    for (const auto& bp: components) {
        if (bp->Item() == item) {
            return true;
        }
    }

    return false;
}

XItem* XCreature::GetItem(BODY_PART bp, int count)
{
    XBodyPart * xbp = GetBodyPart(bp, count);

    if (xbp) {
        return xbp->Item();
    } else {
        return nullptr;
    }
}

void XCreature::FirstStep(int _x, int _y, XLocation * _l)
{
    x = _x;
    y = _y;
    nx = _x;
    ny = _y;
    SetLocation(_l);

    assert(!l->map->GetMonster(_x, _y));

    // The very first placement is also the first point at which this
    // creature becomes shared_from_this()-safe (see XMap::SetMonster's
    // birth path). Starting gear worn/carried during this creature's own
    // constructor (XAnyCreature's equip loop) couldn't establish item/
    // bodypart owner backlinks - or, for worn items, run their onWear()
    // side effect - since shared_from_this() wasn't usable yet.
    // XBodyPart::Wear() already puts worn items in contain via
    // owner_raw->ContainItem() regardless (that doesn't need
    // shared_from_this()), so weight accounting is already correct; only
    // the owner backlinks and the deferred onWear() callback are left to
    // finish here.
    bool first_placement = weak_from_this().expired();

    l->map->SetMonster(_x, _y, this);

    if (first_placement) {
        for (auto& bp: components) {
            // Every component, not only the ones holding something. A body
            // part whose owner is never bound silently drops onWear() for
            // everything put on it afterwards.
            bp->SetOwner(this);

            if (XItem* worn = bp->Item()) {
                worn->onWear(this);
            }
        }

        for (auto item: contain) {
            item->SetOwner(this);
        }
    }
}

void XCreature::LastStep()
{
    l->map->ResMonster(x, y);
}

int XCreature::continueRead()
{
    auto* book = dynamic_cast<XBook*>(action_data.item.get());

    if (!book || !book->isValid()) {
        stopAction();

        return 1;
    }

    book->onRead(this);

    if (book->left_to_read <= 0) {
        book->UnCarry();
        book->Invalidate();
        action_data.action = A_MOVE;
        action_data.item = nullptr;

        if (vRand(5) == 0) {
            GainAttr(XStats::LEN, 1);
        }
    }

    return 1;
}

int XCreature::Read(XItem * item)
{
    XSkill * skill = sk->GetSkill(XSkill::Skill::LITERACY);

    if (!skill) {
        if (isHero()) {
            msgwin.Add("You are illiterate!");
        }

        return 0;
    }

    if (item->kind & ItemKind::SCROLL) {
        auto* scroll = dynamic_cast<XScroll*>(item);

        if (!scroll) {
            return 0;
        }

        skill->UseSkill();
        scroll->onRead(this);
        item->UnCarry();
        item->Invalidate();

        if (vRand(10) == 0) {
            GainAttr(XStats::LEN, 1);
        }

        return 1;
    } else if (item->kind & ItemKind::BOOK) {
        auto* book = dynamic_cast<XBook*>(item);

        if (!book) {
            return 0;
        }

        book->onRead(this);

        if (book->left_to_read <= 0) {
            item->UnCarry();
            item->Invalidate();
        } else {
            action_data.action = A_READ;
            action_data.item = XItem::Own(item);
        }

        return 1;
    }

    return 0;
}

void XCreature::SetEventHandler(const std::string& handler)
{
    event_handler = handler;
}

void XCreature::EnableMoveHandler()
{
    wants_move_hook = true;
}

void XCreature::DisableMoveHandler()
{
    wants_move_hook = false;
}

void XCreature::SaveModifier(cereal::JSONOutputArchive& ar) const
{
    ar(*md);
}

void XCreature::LoadModifier(cereal::JSONInputArchive& ar)
{
    md = new XModifier();
    ar(*md);
}

void XCreature::FixupCreatureInfo()
{
    if (!isHero()) { //skip restoing of descriptions and other for hero
        XCreatureStorage::RestoreCreatureInfo(this);
    }
}

void XCreature::FixupXaiOwner()
{
    if (xai) {
        xai->SetOwner(this);
    }
}

void XCreature::NotifyLuaEventHandler(LuaEvent event)
{
    XLocation::lua_int_buffer = &lua_ints;
    XLocation::lua_int_index = 0;

    if (!event_handler.empty()) {
        sol::state_view lua(XLua::State());
        lua[event_handler](event);
    }
}

int XCreature::GetCreatureStrength()
{
    int tdv = GetDV();
    int tpv = GetPV();

    if (tdv <= 0) {
        tdv = 1;
    }

    if (tpv <= 0) {
        tpv = 1;
    }

    int dv_pv_bonus = ((tdv * tpv * tpv) / 10 + (dv * pv * pv));
    int thit = GetHIT() / 10;
    int tdmg = (dice.GetCount() * dice.GetSides() + dice.GetBonus() + GetDMG());

    if (thit <= 0) {
        thit = 1;
    }

    if (tdmg <= 0) {
        tdmg = 1;
    }

    int hit_dmg_bonus = thit * tdmg * GetMaxHP();

    return 20 + hit_dmg_bonus + dv_pv_bonus;
}

int XCreature::GetTarget(TARGET_REASON tr, XPoint * pt, int /*max_range*/, XObject** /*back*/)
{
    switch (tr) {
        case TR_ATTACK_TARGET:
            return xai->GetTargetPos(pt);

        case TR_ATTACK_DIRECTION: {
            // A touch spell wants a step rather than a place:
            // XEffect::Make() adds what comes back here to the caster's
            // own position. The hero is asked which way; a creature
            // answers with the way to whatever it is fighting.
            XPoint target;

            if (!xai->GetTargetPos(&target)) {
                return 0;
            }

            pt->x = (target.x > x) - (target.x < x);
            pt->y = (target.y > y) - (target.y < y);

            return 1;
        }

        // Every other reason asks the player something - a quantity, an
        // item - and only XHero, which overrides this, can ask. A
        // creature answers nothing.
        default:
            break;
    }

    return 0;
}

bool XCreature::Chat(XCreature* chatter, const char* msg)
{
    if (event_handler.empty()) {
        return false;
    }

    sol::state_view lua(XLua::State());
    sol::protected_function_result result = lua[event_handler](LuaEvent::CHAT, (void*)this, (void*)chatter, std::string(msg));

    return XLua::ResultToBool(result, event_handler);
}

std::shared_ptr<XItem> XCreature::ContainItem(XItem * item)
{
    if (CarryItem(item)) {
        // contain.insert() (XItemList::insert, item/item.h) returns an
        // iterator to whichever XItem this ended up as: `item` itself on
        // a fresh insert, or a pre-existing, stackable-equal item it got
        // Concat()-ed into instead (see the declaration comment on
        // ContainItem in creature.h for why that distinction matters).
        return *contain.insert(XItem::Own(item)).first;
    }

    return nullptr;
}

bool XCreature::CarryItem(XItem * item)
{
    if (item->GetOwner().lock().get() == this) {
        return 1;
    }

    carried_weight += item->weight * item->quantity;

    if (GetCarryState() == CSTATE_DIE) {
        carried_weight -= item->weight * item->quantity;
        return false;
    } else {
        item->SetOwner(this);
        return true;
    }
}

void XCreature::UnCarryItem(XItem * item)
{
    if (auto o = item->GetOwner().lock()) {
        assert(o.get() == this);
        carried_weight -= item->weight * item->quantity;
    }

    item->SetOwner(nullptr);
}

int XCreature::CarryValue(CARRY_STATE cs)
{
    int str = stats->Get(XStats::STR) + added_stats.Get(XStats::STR);

    switch (cs) {
        case CSTATE_NORMAL :
            return str * 120;
            break;

        case CSTATE_BURDENED :
            return str * 200;
            break;

        case CSTATE_STRAINED :
            return str * 280;
            break;

        case CSTATE_OVERBURDEN :
            return str * 360;
            break;

        case CSTATE_DIE :
            return str * 600;
            break;

        default :
            assert(0);
            break;
    };

    return 0;
}

CARRY_STATE XCreature::GetCarryState()
{
    if (carried_weight < CarryValue(CSTATE_NORMAL)) {
        return CSTATE_NORMAL;
    } else if (carried_weight < CarryValue(CSTATE_BURDENED)) {
        return CSTATE_BURDENED;
    } else if (carried_weight < CarryValue(CSTATE_STRAINED)) {
        return CSTATE_STRAINED;
    } else if (carried_weight < CarryValue(CSTATE_OVERBURDEN)) {
        return CSTATE_OVERBURDEN;
    } else {
        return CSTATE_DIE;
    }
}

int XCreature::GetVisibleRadius()
{
    int perception = stats->Get(XStats::PER);

    if (perception < 5) {
        return 3;
    } else if (perception < 10) {
        return 4;
    } else if (perception < 20) {
        return 5;
    } else if (perception < 50) {
        return 6;
    } else if (perception < 90) {
        return 7;
    } else {
        return 8;
    }
}

bool XCreature::onGiveItem(XCreature* giver, XItem* item)
{
    if (event_handler.empty()) {
        return false;
    }

    sol::state_view lua(XLua::State());
    sol::protected_function_result result = lua[event_handler](LuaEvent::GIVE_ITEM, (void*)this, (void*)giver, (void*)item);

    return XLua::ResultToBool(result, event_handler);
}

int XCreature::MoneyOp(int money_count)
{
    XItem* money = nullptr;

    for (auto it: contain) {
        if (it->kind & ItemKind::MONEY) {
            money = it.get();
            break;
        }
    }

    if (money) {
        if (money_count >= 0) {
            money->quantity += money_count;

            return money->quantity;
        }

        if (money->quantity + money_count > 0) {
            money->quantity += money_count;

            return money->quantity;
        }

        if (money->quantity + money_count == 0) {
            // Invalidate() must run before erase() now that contain holds
            // money's real shared_ptr ownership (opposite of the old
            // raw-pointer ordering): contain can be money's only reference,
            // and erasing it while money is still valid would run Own()'s
            // deleter on a still-valid item, re-entering Invalidate() while
            // we're still about to call it ourselves below. Invalidate()
            // itself doesn't free money - contain still holds it alive at
            // that point - so money->kind stays safe to read for the erase's
            // tree walk immediately after.
            money->Invalidate();

            if (auto it = contain.find(money); it != contain.end()) {
                contain.erase(it);
            }

            return 0;
        }

        return money->quantity + money_count;
    }

    if (money_count > 0) {
        contain.insert(XItem::Own(new XMoney(money_count)));

        return money_count;
    }

    return money_count;
}

const char* XCreature::GetGenderStr()
{
    XCreature::Gender g = GetGender();

    if (g == XCreature::MALE) {
        return "male";
    } else if (g == XCreature::FEMALE) {
        return "female";
    } else {
        return "neuter";
    }
}

const std::string XCreature::GetNameEx(CR_NAME_TYPE crn)
{
    if (isVisible()) {
        // Every inner switch answers all four CR_NAME_TYPEs, so the breaks
        // below are never reached. They are there so that a value from
        // outside the enum drops out to the assert instead of quietly
        // picking up the next person type's answer.
        switch (creature_person_type) {
            case XCreature::YOU:
            case XCreature::MALE_YOU:
            case XCreature::FEMALE_YOU:
            case XCreature::THEY_YOU:
                switch (crn) {
                    case CRN_T1:
                        return "you";

                    case CRN_T2:
                        return "you";

                    case CRN_T3:
                        return "you";

                    case CRN_T4:
                        return "your";
                }

                break;

            case XCreature::NAMED_HE:
                switch (crn) {
                    case CRN_T1:
                        return name;

                    case CRN_T2:
                        return "he";

                    case CRN_T3:
                        return "him";

                    case CRN_T4:
                        return "his";
                }

                break;

            case XCreature::NAMED_SHE:
                switch (crn) {
                    case CRN_T1:
                        return name;

                    case CRN_T2:
                        return "she";

                    case CRN_T3:
                        return "her";

                    case CRN_T4:
                        return "hers";
                }

                break;

            case XCreature::NAMED_IT:
                switch (crn) {
                    case CRN_T1:
                        return name;

                    case CRN_T2:
                        return "it";

                    case CRN_T3:
                        return "it";

                    case CRN_T4:
                        return "its";
                }

                break;

            case XCreature::NAMED_THEY:
                switch (crn) {
                    case CRN_T1:
                        return name;

                    case CRN_T2:
                        return "they";

                    case CRN_T3:
                        return "them";

                    case CRN_T4:
                        return "their";
                }

                break;

            case XCreature::THEY:
                switch (crn) {
                    case CRN_T1:
                        return fmt::format("the {}", name);

                    case CRN_T2:
                        return "they";

                    case CRN_T3:
                        return "them";

                    case CRN_T4:
                        return "their";
                }

                break;

            case XCreature::HE:
                switch (crn) {
                    case CRN_T1:
                        return fmt::format("the {}", name);

                    case CRN_T2:
                        return "he";

                    case CRN_T3:
                        return "him";

                    case CRN_T4:
                        return "his";
                }

                break;

            case XCreature::SHE:
                switch (crn) {
                    case CRN_T1:
                        return fmt::format("the female {}", name);

                    case CRN_T2:
                        return "she";

                    case CRN_T3:
                        return "her";

                    case CRN_T4:
                        return "hers";
                }

                break;

            case XCreature::IT:
                switch (crn) {
                    case CRN_T1:
                        return fmt::format("the {}", name);

                    case CRN_T2:
                        return "it";

                    case CRN_T3:
                        return "it";

                    case CRN_T4:
                        return "its";
                }
        }
    } else {
        switch (crn) {
            case CRN_T1:
                return "someone";

            case CRN_T2:
                return "it";

            case CRN_T3:
                return "it";

            case CRN_T4:
                return "its";
        }
    }

    assert(0);
    return "";
}

std::string XCreature::GetVerb(std::string verb) const
{
    // Second person and singular they both take the verb uninflected:
    // "you bite", "they bite" - only he/she/it gets the "s".
    if (creature_person_type & (XCreature::YOU | XCreature::THEY)) {
        return verb;
    }

    return ThirdPerson(std::move(verb));
}

std::string XCreature::ThirdPerson(std::string verb)
{
    // Nothing to inflect - GetWoundMsg() answers with an empty verb for
    // a creature that took no visible harm.
    if (verb.empty()) {
        return verb;
    }

    // The last word carries it, so a verb that arrives with an adverb in
    // front of it still comes out right: "slightly wound" -> "slightly
    // wounds".
    const char last_char = verb.back();

    if (last_char == 's' || last_char == 'h') {
        return verb.append("es");
    }

    return verb.append("s");
}
