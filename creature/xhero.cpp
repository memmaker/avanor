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

// The hero as a creature: what the rest of the engine sees of the player.
// Everything the player *does* - the key loop, the lists, the panels, the
// commands - is the player's interface rather than the hero's nature, and
// lives in the xhero_*.cpp beside this one.

#include "port/rvip_sound.h"
#include <memory>
#include <vector>
#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>
#include <fmt/format.h>

#include "creature/skeep_ai.h"
#include "creature/xhero.h"
#include "game/game.h"
#include "game/quest.h"
#include "helpers/msgwin.h"
#include "item/item_misc.h"
#include "item/itemf.h"
#include "magic/modifier.h"
#include "map/map_objects.h"

REGISTER_CLASS(XHero);

CEREAL_REGISTER_TYPE(XHero);

CEREAL_REGISTER_POLYMORPHIC_RELATION(XCreature, XHero);

int _exit_flag = 0;

static std::vector<MELEE_ATTACK> hero_melee;

XHero::XHero(NewCharacter)
{
    melee_attack = &hero_melee;

    creature_person_type = XCreature::YOU;
    turn_count = 0;
    last_cast = nullptr;

    x = 5;
    y = 5;
    view = '@';
    color = xWHITE;
    name = "-=RET=-";
    RNG = 5;
    target.reset();

    auto *d = new XDice("1d3");
    dv = d->Throw();
    delete d;

    d = new XDice("1d2-1");
    pv = d->Throw();
    delete d;

    d = new XDice("1d4");
    to_hit = d->Throw();
    delete d;

    dice.Setup("1d2");

    std::string hero_body = "head neck body cloak hand hand ring ring gloves "
                            "boots light_source tool missile_weapon missile";
    XBodyPart::Create(this, hero_body);

    PlayerSetup();

    MAX_HP = stats->Get(XStats::TOU) / 2 + 3 + (XGame::isGodMode ? 1000 : 0);
    HP = GetMaxHP();

    MAX_PP = stats->Get(XStats::MAN) / 2 + 1 + (XGame::isGodMode ? 1000 : 0);
    PP = GetMaxPP();

    base_exp = static_cast<int>(GetCreatureStrength() * 0.6);

    resistances = std::make_unique<XResistance>();

    isDisturb = 0;
    last_char = '5';
    run_way_count = 0;
    target.reset();

    // What sort of creature the hero is comes from world/hero.lua, with
    // the rest of what a race means - see InitHero() there.

    base_nutrio = 1000;
    nutrio = 10000;
    nutrio_speed = 5;

}

int XHero::PossibleWayCount(const int px, const int py) const
{
    int res = 0;

    if (l->map->XGetMovability(px, py + 1) != 1) {
        res++;
    }

    if (l->map->XGetMovability(px, py - 1) != 1) {
        res++;
    }

    if (l->map->XGetMovability(px + 1, py) != 1) {
        res++;
    }

    if (l->map->XGetMovability(px - 1, py) != 1) {
        res++;
    }

    return res;
}

void XHero::Die(XCreature * killer)
{
    RVIP_SOUND("death");

    if (XGame::isGodMode) {
        // God mode entails a choice about whether I die.
        msgwin.Add("You died!!! Continue game?");

        if (GetTarget(TR_NO_YES)) {
            // Don't want to die twice, since we are cheating it!
            HP = GetMaxHP();
            md->Cure(main_creature);
            return;
        }
    }

    msgwin.Add("You died!!!");
    _exit_flag = 1;
    PutStatus();
    l->map->Put(this);
    vRefresh();

    vGetch();
    std::string str;

    if (killer == this) {
        str = fmt::format("Killed himself at {}.", l->GetFullName());
    } else if (killer) {
        str = fmt::format("Killed by {} at {}.", killer->name, l->GetFullName());
    } else {
        str = fmt::format("Died at {}.", l->GetFullName());
    }

    XQuest::quest.hero_die = 1;

    EndGame(str.c_str());
    vClrScr();
    XCreature::Die(killer);
}

XCreature* XHero::ShopkeeperHere() const
{
    XAnyPlace* place = l->map->GetPlace(x, y);

    if (!place || place->GetClassName() != "XShop") {
        return nullptr;
    }

    return place->GetOwner().lock().get();
}

void XHero::stopAction()
{
    XCreature::stopAction();
}

int XHero::RepeatCast()
{
    if (last_cast) {
        if (m->Cast(last_cast, this) == CONTINUE) {
            return 0;
        }

        return 1;
    }

    return 0;
}

int XHero::UseSkill()
{
    XSkill* skill = nullptr;
    skill = SkillsList(SKF_USE_SKILL);

    if (skill) {
        return skill->Use(this);
    } else {
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////////

void XHero::FirstStep(int _x, int _y, XLocation * _l)
{
    XCreature::FirstStep(_x, _y, _l);
    ShowNewView();
}

void XHero::LastStep()
{
    HideOldView();
    XCreature::LastStep();
}

std::shared_ptr<XItem> XHero::onIdentifyItem()
{
    auto it = Inventory(&contain);

    if (it) {
        contain.insert(it);
    }

    return it;
}

bool XHero::Chat(XCreature * /*chatter*/, const char* /*msg*/)
{
    msgwin.Add("You don't like talking to yourself.");
    return true;
}

void XHero::FixupHeroDefaults()
{
    isDisturb = 0;
    last_char = '5';
    run_way_count = 0;
    target.reset();
    last_cast = nullptr;
    melee_attack = &hero_melee;
}

void XHero::ActivateTrap()
{
    XMapObject* obj = l->map->GetSpecial(x, y);

    if (auto* trap = dynamic_cast<XTrap *>(obj)) {
        // Activate() may destroy the trap (self-eviction, kept alive
        // through this call by the deferred-release graveyard).
        trap->Activate(this);
    }
}

const char* XHero::GetRaceStr() const
{
    return race_name.c_str();
}

const char* XHero::GetProfessionStr() const
{
    return profession_name.c_str();
}

std::shared_ptr<XItem> XHero::SelectItem(XItemFilter* filter, const bool isGetAll)
{
    return Inventory(&contain, ItemKind::UNKNOWN, IF_NONE, !isGetAll, filter);
}
