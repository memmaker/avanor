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

#include "port/rvip_sound.h"
#include <iostream>

#include <algorithm>
#include <iterator>

#include <fmt/format.h>
#include <cereal/archives/json.hpp>
#include <sol/sol.hpp>

#include "helpers/registry.h"
#include "creature/creature.h"
#include "helpers/msgwin.h"
#include "magic/magic.h"
#include "magic/modifiers.h"

// The school table below names one of these on almost every line.

void RegisterSpellNameEnum(sol::state_view& lua)
{
    // Which spells exist is content (world/spells.lua). What a spell can
    // belong to, and what a caster reaches for one FOR, stay engine.
    // What can be laid on a creature for a while - a wound that bleeds, a
    // quickening, a disease. Content names these when a potion or a spell
    // has no effect of its own to point at.
    lua.new_enum("SpellUse",
        "OTHER", XSpell::Use::OTHER,
        "ATTACK", XSpell::Use::ATTACK,
        "HEALING", XSpell::Use::HEALING
    );
}

namespace {

Registry<MagicSchoolStats> schools_db{"school of magic"};

}

const MagicSchoolStats* FindMagicSchool(const std::string& id)
{
    return schools_db.Find(id);
}

const std::vector<MagicSchoolStats>& AllMagicSchools()
{
    return schools_db.All();
}

bool CheckMagicSchoolExists(const std::string& id, const char* where)
{
    return schools_db.Exists(id, where);
}

MagicSchoolBuilder::MagicSchoolBuilder(MAGIC_SCHOOL id)
{
    t.id = std::move(id);
}

MagicSchoolBuilder& MagicSchoolBuilder::Called(const std::string& name)
{
    t.name = name;
    return *this;
}

void MagicSchoolBuilder::Register()
{
    if (t.name.empty()) {
        std::cerr << "world: the school of magic '" << t.id
                  << "' has no name for the character sheet" << std::endl;
        return;
    }

    schools_db.Add(t);
}

void RegisterMagicSchoolLua(sol::state_view& lua)
{
    lua.new_usertype<MagicSchoolBuilder>("MagicSchool",
        sol::constructors<MagicSchoolBuilder(MAGIC_SCHOOL)>(),
        "Called", &MagicSchoolBuilder::Called,
        "Register", &MagicSchoolBuilder::Register
    );
}

// One spell: what it is called, what casting it does, which school it
// belongs to, what it costs, and what a caster reaches for it FOR.
// Filled from world/spells.lua as that script loads.
struct SPELL_REC {
    SPELL_NAME id;
    std::string name;
    EFFECT effect{EFFECT_NONE};
    MAGIC_SCHOOL school;
    int cost{0};

    // Defaults to OTHER, so a spell content adds is inert to the AI until
    // it is said to be an attack or a heal, rather than silently
    // miscategorised.
    XSpell::Use use{XSpell::Use::OTHER};
};

Registry<SPELL_REC> spell_db{"spell"};

// The row for an id, or nullptr if content never registered one.
static const SPELL_REC* FindSpell(const std::string& id)
{
    return spell_db.Find(id);
}

// A row that is always there, so every accessor below has something to
// answer with when asked about a spell nothing defines.
static const SPELL_REC& SpellRow(const SPELL_NAME& id)
{
    static const SPELL_REC nothing;

    if (const SPELL_REC* row = FindSpell(id)) {
        return *row;
    }

    return nothing;
}

SpellBuilder::SpellBuilder(std::string id) : id(std::move(id)) {}

SpellBuilder& SpellBuilder::Called(const std::string& n)
{
    name = n;
    return *this;
}

SpellBuilder& SpellBuilder::Effect(const EFFECT& eff)
{
    effect = eff;
    return *this;
}

SpellBuilder& SpellBuilder::School(const std::string& sch)
{
    if (CheckMagicSchoolExists(sch, ("the spell '" + id + "'").c_str())) {
        school = sch;
    }

    return *this;
}

SpellBuilder& SpellBuilder::Cost(const int c)
{
    cost = c;
    return *this;
}

SpellBuilder& SpellBuilder::Use(const XSpell::Use u)
{
    use = u;
    return *this;
}

void SpellBuilder::Register()
{
    SPELL_REC row;
    row.id = id;
    row.name = name.empty() ? id : name;
    row.effect = effect;
    row.school = school;
    row.cost = cost;
    row.use = use;

    spell_db.Add(std::move(row));
}

XSpell::XSpell(const SPELL_NAME spn)
{
    // A name nothing defines - a typo in world/, where spells are written
    // by hand. Say so; the accessors answer from an empty row, so the
    // spell simply does nothing rather than crashing a caster.
    // The empty id is not a typo: it is SP_NONE, which cereal
    // placeholder-constructs an XSpell with before reading a real one in.
    if (!spn.empty() && !FindSpell(spn)) {
        std::cerr << "world: nothing defines a spell '" << spn << "'" << std::endl;
    }

    spell_name = spn;
    eff_level = 1;
    cast_count = 0;
}

const char* XSpell::GetName(const SPELL_NAME spn)
{
    return SpellRow(spn).name.c_str();
}

void XSpell::Cast()
{
    if (++cast_count >= eff_level) {
        cast_count = 0;
        GainLevel();
    }
}

EFFECT XSpell::GetEffect() const
{
    return SpellRow(spell_name).effect;
}

int XSpell::GetManaCost() const
{
    return (SpellRow(spell_name).cost * 15) / (14 + eff_level);
}

const MAGIC_SCHOOL& XSpell::GetSchool() const
{
    return SpellRow(spell_name).school;
}

XSpell::Use XSpell::GetUse() const
{
    return SpellRow(spell_name).use;
}

bool XSpell::CanReach(const int distance, const int power) const
{
    const EFFECT effect = GetEffect();

    switch (XEffect::GetReq(effect)) {
        // A bolt flies as far as the spell carries it.
        case EffectTarget::TARGET:
            return distance <= XEffect::GetRange(effect, power);

        // Touch spells land on the square the caster faces, and
        // XEffect::Make() builds that square by adding one step to the
        // caster's own position - so the target has to be standing in
        // it. Diagonals count: a step is a step.
        case EffectTarget::DIRECTION:
            return distance <= 1;

        // Anything else does not take aim at all, so there is no
        // distance at which a creature could sensibly throw it at
        // somebody.
        default:
            return false;
    }
}

std::string XSpell::toString() const
{
    return fmt::format("<VALUE>{:<21s} <TEXT>: <VALUE>{}<TEXT>pp  (Eff - {}) (to next level: {})",
        SpellRow(spell_name).name,
        GetManaCost(),
        GetEffectivity(),
        eff_level - cast_count);
}

XMagic::XMagic()
{
    // Every school starts unknown; learning a spell opens its school
    // (see Learn()), so the school list shows only what the caster
    // actually practises.
}

int XMagic::GetSpellPower(const XSpell* spell, XCreature* caster)
{
    return caster->GetStats(XStats::WIL)
        + spell->GetEffectivity()
        + caster->m->GetLevel(spell->GetSchool());
}

int XMagic::GetSpellRange(const XSpell* spell, XCreature* caster)
{
    return XEffect::GetRange(spell->GetEffect(), GetSpellPower(spell, caster));
}

RESULT XMagic::Cast(XSpell* spell, XCreature* caster, XCreature* on)
{
    const int power = GetSpellPower(spell, caster);

    if (caster->PP - spell->GetManaCost() >= 0) {
        if (caster->isHero()) {
            RVIP_SOUND("spell");
        }

        if (caster->isInVisibleArea() && !caster->isHero()) {
            // Said as it looks from outside: casting at somebody names
            // them, so a shaman patching up a warrior reads as that rather
            // than as one more spell going off.
            msgwin.Add(on && on != caster
                ? fmt::format("{} {} {} on {}.",
                    caster->GetNameEx(CRN_T1),
                    caster->GetVerb("cast"),
                    spell->GetName(),
                    on->GetNameEx(CRN_T1))
                : fmt::format("{} {} {}.",
                    caster->GetNameEx(CRN_T1),
                    caster->GetVerb("cast"),
                    spell->GetName()));
        }

        int res;

        if (on && on != caster) {
            // The long form of Make(), the one a trap or a script uses,
            // because the short one always acts on whoever cast it.
            EFFECT_DATA ed;
            ed.effect = spell->GetEffect();
            ed.caller = caster;
            ed.l = caster->l;
            ed.call_x = caster->x;
            ed.call_y = caster->y;
            ed.target = on;
            ed.target_x = on->x;
            ed.target_y = on->y;
            ed.power = power;

            res = XEffect::Make(&ed);
        } else {
            res = XEffect::Make(caster, spell->GetEffect(), power);
        }

        if (res != ABORT) {
            caster->PP -= spell->GetManaCost();
        }

        if (res == SUCCESS) {
            spell->Cast(); //increase effectivity of spell
            Train(spell->GetSchool(), 1);

            return SUCCESS;
        }

        if (res == FAIL) {
            if (caster->isVisible()) {
                msgwin.Add(caster->GetNameEx(CRN_T1));
                msgwin.Add(caster->GetVerb("waste"));
                msgwin.Add(caster->GetNameEx(CRN_T4));
                msgwin.Add("power.");
            }

            return FAIL;
        }
    } else {
        if (caster->isHero()) {
            msgwin.Add("You don't have enough mana to cast the spell.");
        }
    }

    return CONTINUE;
}

int XMagic::Train(const MAGIC_SCHOOL& school, const int count)
{
    if (school.empty() || GetLevel(school) >= MAX_LEVEL) {
        return 0;
    }

    int& counter = magic_count[school];
    counter += count;

    if (counter > GetNextLevelAt(school)) {
        counter = 0;

        return GainLevel(school);
    }

    return 0;
}

int XMagic::GainLevel(const MAGIC_SCHOOL& school, const int n)
{
    if (school.empty() || GetLevel(school) >= MAX_LEVEL) {
        return 0;
    }

    int& level = magic_level[school];
    level = std::min(level + n, MAX_LEVEL);

    return 1;
}

void XMagic::Learn(const SPELL_NAME spell)
{
    // Knowing any spell of a school makes the caster a Beginner in it.
    if (const MAGIC_SCHOOL& school = SpellRow(spell).school;
        !school.empty() && GetLevel(school) == 0) {
        magic_level[school] = 1;
    }

    for (const auto& tsp : spells) {
        if (tsp->GetSpellName() == spell) {
            tsp->GainLevel();

            return;
        }
    }

    spells.push_back(std::make_unique<XSpell>(spell));
}

XSpell* XMagic::GetSpell(const SPELL_NAME spell) const
{
    for (const auto& tsp : spells) {
        if (tsp->GetSpellName() == spell) {
            return tsp.get();
        }
    }

    return nullptr;
}

const char* mg_level_str[] = {
    "",
    "<PROGRESS_NONE>Beginner",
    "<PROGRESS_NONE>Novice",
    "<PROGRESS_NONE>Senior Novice",
    "<PROGRESS_EXPERT>Minor Adept",
    "<PROGRESS_EXPERT>Adept",
    "<PROGRESS_EXPERT>Major Adept",
    "<PROGRESS_MASTER>Master",
    "<PROGRESS_SENIOR_MASTER>Senior Master",
    "<PROGRESS_GRANDMASTER>Grand Master"
};

static_assert(std::size(mg_level_str) == XMagic::MAX_LEVEL + 1,
    "mg_level_str[] must name every rank from 0 to MAX_LEVEL");

std::string XMagic::LevelToString(const MAGIC_SCHOOL& school) const
{
    const MagicSchoolStats* row = FindMagicSchool(school);

    if (row && GetLevel(school) > 0) {
        return fmt::format("<VALUE>{:<30} {}", row->name, mg_level_str[GetLevel(school)]);
    }

    return "";
}

int XMagic::GetLevelNameLength(const int level)
{
    const std::string_view s = mg_level_str[level];
    const auto tag_end = s.find('>');

    return static_cast<int>(tag_end == std::string_view::npos ? s.size() : s.size() - tag_end - 1);
}

