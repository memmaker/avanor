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
#include <fmt/format.h>

#include "creature/creature.h"
#include "game/game.h"
#include "helpers/msgwin.h"
#include "magic/brand.h"
#include "magic/modifier.h"

void XCreature::Attack()
{
    assert(isValid());
    XCreature * target = l->map->GetMonster(nx, ny);
    assert(target);

    // Keep target allocated for the rest of this function: the first
    // MeleeAttack() below can kill it, and XCreature::Die() only keeps
    // itself alive for the duration of its own call (see its `self =
    // shared_from_this()`) - once it returns, target may already be fully
    // delete()d via shared_ptr, not just marked invalid. target->isValid()
    // further down needs a real, if now-invalidated, object to read rather
    // than freed memory.
    auto target_keepalive = XCreature::ToWeakPtr(target).lock();

    XItem * it1 = GetItem(BP_HAND, 0);
    XItem * it2 = GetItem(BP_HAND, 1);
    int flag = 0;

    if (it1 && it1->kind & ItemKind::WEAPON) {
        MeleeAttack(target, it1);
        flag++;
    }

    if (it2 && it2->kind & ItemKind::WEAPON && target->isValid()) {
        MeleeAttack(target, it2);
        flag++;
    }

    if (flag == 0) {
        MeleeAttack(target, nullptr);
    }
}

int XCreature::MeleeAttack(XCreature * target, XItem * weapon)
{
    assert(isValid());

    int res = 0;
    int tohit;
    int tdam;
    BrandSet aet;

    if (weapon) {
        res += (wsk->GetUseTime(weapon->wt) * GetSpeed()) / 1000;
        tohit = GetHIT() + wsk->GetHIT(weapon->wt) + GetUnwieldyHITPenalty(weapon);
        tdam = weapon->dice.Throw() + wsk->GetDMG(weapon->wt) + GetUnwieldyDMGPenalty(weapon) + GetDMG();
        aet = weapon->aet;
    } else {
        res += (wsk->GetUseTime(wsk->Best(CombatRole::UNARMED)) * GetSpeed()) / 1000;
        tohit = GetHIT() + wsk->GetHIT(wsk->Best(CombatRole::UNARMED));
        tdam = dice.Throw() + GetDMG() + wsk->GetDMG(wsk->Best(CombatRole::UNARMED));

        for (const auto& tit: *melee_attack) {
            if (vRand(100) < tit.prob) {
                aet.Add(tit.br_attack);
            }
        }
    }

    // A backstab is a blow the victim was not ready for. The engine has
    // no facing or awareness model, so there are exactly two ways of
    // being caught out: the attacker cannot be seen at all (invisible,
    // and the victim has no way to see invisible), or the victim did not
    // count the attacker as an enemy until this moment. Both have to be
    // read here, before InflictDamage() - its first statement is
    // onWasAttacked(), which makes the victim an enemy.
    const int backstab_level = sk->GetLevel(XSkill::Skill::BACKSTABBING);
    const bool backstab = weapon && backstab_level > 0
                          && (!target->isCreatureVisible(this) || !target->xai->isEnemy(this))
                          && vRand(100) < backstab_level * 5 + 5;

    DAMAGE_DATA_EX dd{};
    dd.damage	= tdam;
    dd.attacker	= this;
    dd.attack_HIT	= tohit;
    dd.attack_effect	= aet;
    dd.flags	= DF_MAGIC_BOLT;
    dd.weapon	= weapon;
    dd.backstab	= backstab;

    if (target->InflictDamage(&dd)) {
        if (weapon) {
            wsk->UseSkill(weapon->wt);
        } else {
            wsk->UseSkill(wsk->Best(CombatRole::UNARMED));
        }
    }

    return res;
}

int XCreature::onMagicDamage(const int dmg, const RESISTANCE tr)
{
    assert(isValid());
    const int damage = dmg - (dmg * GetResistance(tr)) / 100;
    return damage < 0 ? 0 : damage;
}

// One convention, honestly: what comes back is the effect's own damage and
// nothing else, and `applied` says whether any of it was even attempted.
//
// This used to answer `dmg` - the caller's own base damage - whenever no
// effect produced anything, which conflated three different situations that
// need three different answers: an attack with no brand at all, a brand this
// function does not implement, and a brand the target resists completely.
// Returning the base for all three doubled every unbranded melee blow in the
// game (the caller added the result to the damage it had just passed in), and
// made a creature immune to fire take full damage from a bolt of fire while a
// half-resistant one took half.
int XCreature::CauseEffect(int dmg, const BrandSet& brands, bool* applied)
{
    int damage = 0;
    bool matched = false;

    for (const BRAND& id : brands) {
        const BrandStats* row = FindBrand(id);

        if (!row) {
            continue;
        }

        // An element the attack carries always counts as applied, even
        // when resistance leaves nothing of it - that zero is the whole
        // point.
        if (!row->element.empty()) {
            matched = true;
            damage += onMagicDamage(dmg, row->element);
        }

        // A slayer brand against something it does not slay has not
        // applied at all - unlike a resisted element, there is nothing
        // here to reduce.
        if (row->slays.Has(creature_class)) {
            matched = true;
            damage += dmg * 3;
        }
    }

    // A brand that declares neither - hellfire, death, six of the eight
    // slayers - matches nothing here and leaves the weapon behaving as a
    // plain one.
    if (applied) {
        *applied = matched;
    }

    return damage;
}

void XCreature::CausePostEffect(int dmg, const BrandSet& brands, XCreature * attacker)
{
    for (const BRAND& id : brands) {
        const BrandStats* row = FindBrand(id);

        if (!row) {
            continue;
        }

        if (!row->inflicts.empty()) {
            md->Add(row->inflicts, dmg, this, attacker);
        }
    }

    // What makes draining life different from simply hurting somebody:
    // half of what the victim loses, rounded up, the attacker gains.
    // onHeal() will not take them past their own maximum, so this tops a
    // wounded spectre up rather than inflating it.
    //
    // Only what the blow actually took counts - dmg here is what came off
    // the victim's HP, after resistances and PV - so draining someone
    // well armoured returns little. And like every other effect in this
    // function it needs the victim to have survived: InflictDamage() only
    // gets this far when HP is still above zero, so a killing blow drains
    // nothing.
    //
    // This one stays in the engine rather than going out to a brand row:
    // it is the only effect here that reaches back to the attacker.
    if (brands.Has(BR_DRAIN_LIFE) && attacker && attacker->isValid() && dmg > 0) {
        if (attacker->onHeal((dmg + 1) / 2) && attacker->isVisible()) {
            msgwin.Add(fmt::format("{} {} stronger.",
                attacker->GetNameEx(CRN_T1),
                attacker->GetVerb("look")));
        }
    }

    //this should be rewrited once.
    if (vRand(1000) < 200) {
        md->Add(MOD_WOUND, dmg / 4, this, attacker);
    }

    if (vRand(1000) < 150) {
        md->Add(MOD_STUN, dmg / 4, this, attacker);
    }

    if (vRand(1000) < 50) {
        md->Add(MOD_CONFUSE, dmg / 4, this, attacker);
    }
}

MF_RESULT XCreature::MissileFlight(MF_DATA * mfd)
{
    XMap * tmap = mfd->l->map;
    int range = mfd->max_range;
    float fdx = float(mfd->ex - mfd->sx);
    float fdy = float(mfd->ey - mfd->sy);
    float xrng = (float)sqrt(fdx * fdx + fdy * fdy);
    float cos_alpha = 0, sin_alpha = 0;

    if (xrng > 0) {
        cos_alpha = fdx / xrng;
        sin_alpha = fdy / xrng;
    }

    float mx = (float)mfd->sx;
    float my = (float)mfd->sy;

    // Where the missile ends up. A missile with no range to fly never
    // enters the loop below, and then it stops where it was launched.
    int epx = mfd->sx;
    int epy = mfd->sy;
    int fl_range = 0;

    bool self_flag = false;

    if (fdx == fdy && fdy == 0.0) {
        self_flag = true;
    }

    while ((range > 0 /*&& (fabs(mfd->ex - mx) >= 0.5f || fabs(mfd->ey - my) >= 0.5f)*/) || self_flag) {
        self_flag = false;
        range--;
        fl_range++;
        float nmx = mx + cos_alpha;
        float nmy = my + sin_alpha;
        epx = std::lround(nmx);
        epy = std::lround(nmy);

        if (tmap->GetMovability(epx, epy) >= XTileType::Movability::WALL) {
            epx = std::lround(mx);
            epy = std::lround(my);
            break;
        }

        if (tmap->GetVisible(epx, epy) && __animation_flag) {
            tmap->Put(main_creature);

            if (mfd->arrow_type == MFT_BALL) {
                tmap->PutChar(epx, epy, '*', mfd->arrow_color);
            } else {
                int ttx = std::lround(nmx - mx);
                int tty = std::lround(nmy - my);

                if (ttx * tty == 1) {
                    tmap->PutChar(epx, epy, '\\', mfd->arrow_color);
                } else if (ttx * tty == -1) {
                    tmap->PutChar(epx, epy, '/', mfd->arrow_color);
                } else if (ttx == 0) {
                    tmap->PutChar(epx, epy, '|', mfd->arrow_color);
                } else if (tty == 0) {
                    tmap->PutChar(epx, epy, '-', mfd->arrow_color);
                }
            }

            vRefresh();
            vDelay(__animation_flag);
        }

        mx = nmx;
        my = nmy;


        //if (fabs(mfd->ex - mx) < 0.5f && fabs(mfd->ey - my) < 0.5f)
        if (epx == mfd->ex && epy == mfd->ey) {
            XCreature * tgt = tmap->GetMonster(epx, epy);

            if (tgt) {
                int tdv = tgt->GetDV() + fl_range * tgt->GetDV() / 6;
                int tht = mfd->to_hit > 0 ? mfd->to_hit : 1;

                if (tdv < 0) {
                    tht -= tdv;
                    tdv = 0;
                }

                int p = (100 * tht + 1) / (tht + tdv + 1);

                if (vRand(100) <= p) {
                    mfd->pt.x = epx;
                    mfd->pt.y = epy;
                    return MF_HIT;
                }
            }
        }
    }

    mfd->pt.x = epx;
    mfd->pt.y = epy;
    return MF_AVOID;
}

void XCreature::continueUseItem()
{
    assert(isValid());

    auto* tool = dynamic_cast<XTool*>(action_data.item.get());

    if (!tool || !tool->isValid()) {
        action_data.action = A_MOVE;
        action_data.item = nullptr;

        return;
    }

    assert(tool->kind & ItemKind::TOOL);
    RESULT res = tool->onUse(XTool::PROGRESS, this);

    if (res != CONTINUE) {
        action_data.action = A_MOVE;
        action_data.item = nullptr;
    }
}

int XCreature::UseItem(XTool* tool)
{
    assert(isValid());
    RESULT res = tool->onUse(XTool::START, this);

    if (res) {
        if (res == CONTINUE) {
            action_data.action = A_USE_TOOL;
            action_data.item = XItem::Own(tool);
            return 1;
        } else {
            return 1;
        }
    } else {
        return 0;
    }

    return 1;
}


void XCreature::Sacrifice(XItem * item)
{
    assert(isValid());
    religion.SacrificeItem(this, item, DEITY_NONE);
}

int XCreature::isCreatureVisible(XCreature * cr)
{
    assert(isValid());

    if (!cr) {
        return 0;
    }

    // Spectator mode: the hero is not there as far as anybody is concerned.
    if (XGame::isSpectator && cr->isHero() && !isHero()) {
        return 0;
    }

    // Can the creature see the unseen?
    if (cr == this || cr->GetResistance(RS_INVISIBLE) <= 0
        || GetResistance(RS_SEE_INVISIBLE) > 0) {
        return 1;
    } else {
        return 0;
    }

}

bool XCreature::isVisible()
{
    assert(isValid());

    if (l->map->GetVisible(x, y)) {
        if (XCreature::main_creature->isCreatureVisible(this)) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

// What the attacker is said to do with what it is holding. The word
// belongs to the weapon and is stated with the rest of its row
// (world/items/weapons.lua); a weapon that names none simply hits, and
// bare hands attack.
std::string XCreature::GetMeleeAttackMsg(XItem * weapon)
{
    if (!weapon) {
        return "attack";
    }

    const ItemTemplate* row = gi_weapon.Find(weapon->it);

    if (row && !row->verb.empty()) {
        return row->verb;
    }

    return "hit";
}

int XCreature::InflictDamage(DAMAGE_DATA_EX * pData)
{
    //make creature hostle
    //
    // ... but never towards itself. A creature can legitimately damage
    // itself - most directly by aiming a bolt spell or scroll at its own
    // tile, which XEffect::Bolt() happily delivers. Reacting to that
    // would mean adding itself to its own personal-enemy list, which is
    // meaningless and trips ReactToAttacker()'s assert(attacker !=
    // ai_owner) - aborting the game the moment the player targets
    // themselves.
    if (pData->attacker && pData->attacker != this) {
        xai->onWasAttacked(pData->attacker);
    }

    //stop current action
    stopAction();

    bool vis1 = isVisible();
    bool vis2 = pData->attacker ? pData->attacker->isVisible() : vis1;

    //this attack can be avoided

    int v = 0; //v by default less then p (there will 100% hit)
    int p = 1;
    int todv = 1;

    if (pData->flags & DF_AFFECT_HIT) {
        //get creature overall DV
        todv = GetDV(pData->attacker);
        int tohit = pData->attack_HIT;

        if (tohit < 0) {
            todv -= tohit; // a - (-b) == a + b;
            tohit = 0;
        }

        p = ((tohit + 1) * 20) / (tohit + todv + 1);
        v = vRand(20); /*0..19*/
    }

    if (pData->attacker && pData->attacker != this) {
        if (pData->attacker->isHero()) {
            RVIP_SOUND(v <= p ? "hit" : "miss");
        } else if (isHero() && v <= p) {
            RVIP_SOUND("mon_hit");
        }
    }

    if (v <= p) {
        //oh yeah. hited!

        //get random part of body
        XBodyPart * txbp = GetRNDBodyPart();

        //requested base damage...
        int dmg = pData->damage;

        //critical hit (hit into vital spot)
        bool critical_hit = false;
        bool ignore_armour = false;

        if (pData->attacker && vRand(100) < (2 + pData->attacker->sk->GetLevel(XSkill::Skill::FINDWEAKNESS))) {
            pData->attacker->sk->UseSkill(XSkill::Skill::FINDWEAKNESS);
            critical_hit = true;
            dmg *= 3;
        }

        // Trained here rather than where the blow was decided, so that a
        // swing that misses entirely neither triples anything nor counts
        // as practice.
        if (pData->backstab) {
            pData->attacker->sk->UseSkill(XSkill::Skill::BACKSTABBING, 3);
            dmg *= 3;
        }

        // The stance the attacker was holding is in both halves of this
        // blow - GetTacticsHITBonus() helped it land, GetTacticsDMGBonus()
        // is in the damage - so a blow that lands is practice at holding
        // it. On the same terms as the two above: landed, not swung.
        if (pData->attacker) {
            pData->attacker->sk->UseSkill(XSkill::Skill::TACTICS);
        }

        // What the attack's brand does, on top of or instead of the blow
        // itself - see CauseEffect() for why those two are not the same.
        bool effect_applied = false;
        const int effect = CauseEffect(dmg, pData->attack_effect, &effect_applied);

        if (!pData->attack_name.empty()) {
            // A spell, a missile or a trap. When it is elemental the
            // element is the whole of it, so resistance takes from the
            // whole of it - down to nothing against something immune.
            // Carrying no element it keeps the damage it arrived with.
            if (effect_applied) {
                dmg = effect;
            }
        } else {
            // A weapon or an unarmed blow: whatever the brand adds is on
            // top of what the weapon itself does, and an unbranded weapon
            // adds nothing.
            dmg += effect;
        }

        //always count intrinsic PV
        dmg -= pv;

        if (pData->flags & DF_AFFECT_PV) {
            //get initial pv of armour
            int xpv = 0;

            if (txbp && txbp->Item()) {
                xpv = txbp->Item()->pv;
            }

            if (!pData->attacker || vRand(100) >= (2 + pData->attacker->sk->GetLevel(XSkill::Skill::FINDWEAKNESS)) || xpv == 0) {

                //if it is a body, then we should count PV of a cloak.
                if (txbp && txbp->bp_uin == BP_BODY) {
                    XItem * xtmp = GetItem(BP_CLOAK);

                    if (xtmp) {
                        xpv += xtmp->pv;
                    }
                }

                //some dmg absorbed by armour
                dmg -= xpv;
            } else {
                pData->attacker->sk->UseSkill(XSkill::Skill::FINDWEAKNESS);
                ignore_armour = true;
            }
        }

        // The blow and what it did are one sentence, so whatever finishes
        // it takes the subject the first half took: the attack, when it
        // has a name of its own ("a small ball of fire ... and kills
        // you"), and otherwise whoever struck ("you ... and kill it").
        // Reading the person off the attacker in both cases is what used
        // to produce "You hit the spider. And kills it."
        const auto clause_verb = [&pData](const std::string& verb) {
            return pData->attack_name.empty()
                ? pData->attacker->GetVerb(verb)
                : ThirdPerson(verb);
        };

        if (vis1 || vis2) {
            // Only a blow that lands on somebody other than the hero is
            // told as one sentence. Everything else starts a new one, so
            // this is the only case where the full stop waits for the
            // clause below - without that wait the message window sees a
            // finished sentence and capitalises the "and" that follows.
            const bool clause_follows = !isHero() && (dmg > 0 || isVisible());

            auto str = fmt::format("{}{} {} {}{}{}",
                !pData->attack_name.empty() ? pData->attack_name : pData->attacker->GetNameEx(CRN_T1),
                critical_hit ? " exactly" : "",
                !pData->attack_name.empty()
                    ? std::string("hits")
                    : pData->attacker->GetVerb(pData->backstab
                                                   ? "backstab"
                                                   : GetMeleeAttackMsg(pData->weapon)),
                GetNameEx(CRN_T1),
                ignore_armour ? ", penetrating a piece of armour" : "",
                clause_follows ? "" : "."
                );

            msgwin.Add(str);
        }

        if (dmg <= 0) {
            if (isHero()) {
                if (!pData->attack_name.empty()) {
                    msgwin.Add(pData->attack_name);
                } else {
                    msgwin.Add(pData->attacker->GetNameEx(CRN_T1));
                }

                msgwin.Add("does not manage to harm you.");
            } else if (isVisible()) {
                msgwin.Add(fmt::format("but does not manage to harm {}.",
                    GetNameEx(CRN_T3)));
            }
        } else {
            HP -= dmg;

            if (HP > 0) {
                if ((vis1 || vis2) && !isHero()) {
                    msgwin.Add(fmt::format("and {} {}.",
                        clause_verb(GetWoundMsg(1)),
                        GetNameEx(CRN_T3)));
                }

                CausePostEffect(dmg, pData->attack_effect, pData->attacker);
            } else {
                // and kill IT!!!
                if ((vis1 || vis2) && !isHero()) {
                    // An undead is destroyed rather than killed, and which
                    // sorts read that way is content's to say, not the
                    // engine's - see :Slain() in world/creature_classes.lua.
                    const CreatureClassStats* crow = FindCreatureClass(creature_class);
                    const std::string verb = crow ? crow->slain_verb : "kill";

                    auto str = fmt::format("and {} {}.",
                        clause_verb(verb),
                        GetNameEx(CRN_T3)
                    );

                    msgwin.Add(str);
                }

                Die(pData->attacker);
                cr_kiled++; //temporary counter special for statistic
            }
        }

        return 1;
    } else { // avoided!!!
        //first of all check if save was caused by shield
        int sb = GetShieldDVBonus();

        if (sb > 0 && vRand(todv) < sb) {
            if (vis1 || vis2) {
                if (!pData->attack_name.empty()) {
                    //Kobold shaman cast firbolt.
                    //You deflect small ball of fire with your shield
                    msgwin.Add(GetNameEx(CRN_T1));
                    msgwin.Add(GetVerb("deflect"));
                    msgwin.Add(pData->attack_name);
                    msgwin.Add("with");
                    msgwin.Add(GetNameEx(CRN_T4));
                    msgwin.Add("shield.");
                } else {
                    // Kobold attacks you.
                    // You block kobold with your shield.
                    msgwin.Add(fmt::format("{} {} {}.",
                        pData->attacker->GetNameEx(CRN_T1),
                        pData->attacker->GetVerb("attack"),
                        GetNameEx(CRN_T1)));

                    msgwin.Add(fmt::format("{} {} {} with {} shield.",
                        GetNameEx(CRN_T1),
                        GetVerb("block"),
                        pData->attacker->GetNameEx(CRN_T1),
                        GetNameEx(CRN_T4)));
                }
            }

            wsk->UseSkill(wsk->Best(CombatRole::SHIELD));
        } else {
            // It was not shield (miss or avoid)
            if (vis1 || vis2) {
                if (!pData->attack_name.empty()) {
                    msgwin.Add(fmt::format("{} {} {}.",
                        GetNameEx(CRN_T1),
                        GetVerb("avoid"),
                        pData->attack_name));
                } else {
                    msgwin.Add(fmt::format("{} {} {}, but {}.",
                        pData->attacker->GetNameEx(CRN_T1),
                        pData->attacker->GetVerb("attack"),
                        GetNameEx(CRN_T1),
                        pData->attacker->GetVerb("miss")));
                }
            }

            // Practice, on the same terms the bonus is given:
            // an attack that was seen and avoided.
            if (!pData->attacker || isCreatureVisible(pData->attacker)) {
                sk->UseSkill(XSkill::Skill::DODGE);
            }

            // The other half of what tactics is for: GetTacticsDVBonus()
            // is part of what this attack had to beat, so turning one
            // aside is practice at the stance as much as landing a blow
            // is. Trained whether or not the attacker was seen - keeping
            // a guard up does not require watching who tried.
            sk->UseSkill(XSkill::Skill::TACTICS);
        }
    }

    return 0;
}
