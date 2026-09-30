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

// What a key does, once NewMove() has decided which key it was: the world
// commands, the ones that act on something other than the pack.

#include "port/rvip_sound.h"
#include <cctype>
#include <iostream>
#include <memory>
#include <vector>
#include <fmt/format.h>

#include "creature/skeep_ai.h"
#include "engine/xlua.h"
#include "creature/xhero.h"
#include "game/game.h"
#include "game/quest.h"
#include "helpers/manual.h"
#include "helpers/msgwin.h"
#include "item/item_misc.h"
#include "item/itemf.h"
#include "item/xmissile.h"
#include "item/xmissileweapon.h"
#include "item/xpotion.h"
#include "magic/modifier.h"
#include "map/map_objects.h"

void XHero::Move()
{
    turn_count++;

    // What lies underfoot is worth saying when you arrive on it, not on
    // every turn you spend standing there - reading a book on a tile with
    // an amulet on it said so once per turn for as long as the reading
    // took. This guard is the original one; a 2003 fix for the walk
    // command (23a9a1c) replaced it with `if (1)` and pushed the
    // condition down onto the line below, which left the announcements
    // firing every turn.
    if (nx != x || ny != y) {
        if (l->map->GetItemCount(nx, ny) > 0 || l->map->GetSpecial(nx, ny)) {
            isDisturb = 0;
        }

        if (l->map->GetSpecial(nx, ny)) {
            XMapObject *spec = l->map->GetSpecial(nx, ny);
            msgwin.Add(fmt::format("There is {} here.", spec->GetName(this)));
        }

        if (l->map->GetItemCount(nx, ny) > 1) {
            msgwin.Add("There is a heap of items here.");
        } else if (l->map->GetItemCount(nx, ny) == 1) {
            XItemList* ilist = (l->map->GetItemList(nx, ny));
            XItem* item = ilist->begin()->get();
            XAnyPlace* place = l->map->GetPlace(nx, ny);

            msgwin.Add(fmt::format("There {} {} here.",
                item->IsPlural() ? "are" : "is",
                place ? place->onShowItem(item) : item->toSentence()));
        }
    }

    HideOldView();
    ShowNewView();

    // After the view is redrawn, so the field of view being asked about is
    // this turn's.
    SenseUnseen();

    if (action_data.action == A_EAT || action_data.action == A_READ
        || action_data.action == A_USE_TOOL) {
        l->map->Center(x, y);
        l->map->Put(this);
        PutStatus();
        vRefresh();
        msgwin.ClrMsg();
    }

    if (DecNutrio()) {
        XCreature::Move();
    }
}

// Something standing in plain view that the hero cannot make out - an
// invisible creature on a lit tile within sight. Being killed by something
// there was never any way to notice is not a fight, so the fact that it is
// there is said, though not what it is or where.
//
// Said when one arrives rather than every turn it stays: the same rule the
// announcement of what lies underfoot follows, and for the same reason.
// A creature that leaves sight and comes back is worth saying again.
void XHero::SenseUnseen()
{
    const int radius = GetVisibleRadius();
    XCreature* nearest = nullptr;
    int nearest_range = 0;

    for (int i = x - radius; i <= x + radius; i++) {
        for (int j = y - radius; j <= y + radius; j++) {
            // Bounds are this call's business: GetVisible() answers false
            // for a cell off the map, and only once it has answered true
            // is GetMonster() (which asserts on one) safe to ask.
            if (!l->map->GetVisible(i, j)) {
                continue;
            }

            XCreature* cr = l->map->GetMonster(i, j);

            if (!cr || cr == this || isCreatureVisible(cr)) {
                continue;
            }

            const int range = (i - x) * (i - x) + (j - y) * (j - y);

            if (!nearest || range < nearest_range) {
                nearest = cr;
                nearest_range = range;
            }
        }
    }

    if (!nearest) {
        sensed_unseen = false;

        return;
    }

    if (sensed_unseen) {
        return;
    }

    sensed_unseen = true;

    // What an unseen creature of that sort feels like is content's to say,
    // and a world that defines nothing simply stays quiet. The class comes
    // along as an argument rather than through a new XCreature binding -
    // see the string-interning hazard at XCreature::RegisterLua(). Called
    // protected: a fault in it must not take down a fight in progress.
    sol::state_view lua(XLua::State());

    if (sol::protected_function on_sense = lua["OnSenseUnseen"]; on_sense.valid()) {
        const auto result = on_sense((void*)nearest, nearest->creature_class);

        if (!result.valid()) {
            const sol::error err = result;
            std::cerr << "world: OnSenseUnseen: " << err.what() << std::endl;

            return;
        }

        if (const sol::optional<std::string> said = result;
            said && !said->empty()) {
            msgwin.Add(*said);
        }
    }
}

void XHero::OpenDoor()
{
    XMapObject* spec = l->map->GetSpecial(x, y);

    if (spec && (dynamic_cast<XGrave *>(spec) || dynamic_cast<XFurniture *>(spec) || dynamic_cast<XOuterObject *>(spec))) {
        spec->onOuterUse(this);
        return;
    }

    int o_door = 0;
    int c_door = 0;
    int cd_x = -1;
    int cd_y = -1;

    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            if (!(i == 0 && j == 0)) {
                XMapObject* door = l->map->GetSpecial(x + i, y + j);
                auto* xdoor = dynamic_cast<XDoor *>(door);

                if (xdoor && xdoor->isOpened == 0) {
                    c_door++;
                    cd_x = x + i;
                    cd_y = y + j;
                } else if (xdoor && xdoor->isOpened == 1) {
                    o_door++;
                }
            }
        }
    }

    if (c_door == 0 && o_door == 0) {
        msgwin.Add("There is no door here.");
    }

    if (c_door == 0 && o_door == 1) {
        msgwin.Add("The door is already open.");
    }

    if (c_door == 0 && o_door > 1) {
        msgwin.Add("The doors are already open.");
    }

    if (c_door == 1) {
        msgwin.Add("You have opened the door.");
        XMapObject* door = l->map->GetSpecial(cd_x, cd_y);
        dynamic_cast<XDoor *>(door)->Switch();
        return;
    }

    if (c_door > 1) {
        XPoint pt;
        XMapObject* door;

        if (x == nx && y == ny) {
            if (!WhichDirection(&pt)) {
                return;
            }

            door = l->map->GetSpecial(x + pt.x, y + pt.y);
        } else {
            door = l->map->GetSpecial(nx, ny);
        }

        auto* xdoor = dynamic_cast<XDoor *>(door);

        if (xdoor && xdoor->isOpened) {
            msgwin.Add("The door is already open.");
        } else if (xdoor && xdoor->isOpened == 0) {
            msgwin.Add("You have opened the door.");
            xdoor->Switch();
        } else {
            msgwin.Add("There is no door here.");
        }
    }
}

void XHero::CloseDoor()
{
    int o_door = 0;
    int c_door = 0;
    int od_x = -1;
    int od_y = -1;

    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            if (!(i == 0 && j == 0)) {
                XMapObject* spec = l->map->GetSpecial(x + i, y + j);
                auto* xdoor = dynamic_cast<XDoor *>(spec);

                if (xdoor && xdoor->isOpened) {
                    o_door++;
                    od_x = x + i;
                    od_y = y + j;
                } else if (xdoor && xdoor->isOpened == 0) {
                    c_door++;
                }
            }
        }
    }

    if (c_door == 0 && o_door == 0) {
        msgwin.Add("There is no door here.");
    }

    if (o_door == 0 && c_door == 1) {
        msgwin.Add("The door is already closed.");
    }

    if (o_door == 0 && c_door > 1) {
        msgwin.Add("The doors are already closed.");
    }

    if (o_door == 1) {
        msgwin.Add("You have closed the door.");
        XMapObject * spec = l->map->GetSpecial(od_x, od_y);
        LastStep();
        dynamic_cast<XDoor *>(spec)->Switch();
        FirstStep(x, y, l);
        return;
    }

    if (o_door > 1) {
        XPoint pt;

        if (!WhichDirection(&pt, 0)) {
            return;
        }

        XMapObject* spec = l->map->GetSpecial(x + pt.x, y + pt.y);
        auto* xdoor = dynamic_cast<XDoor *>(spec);

        if (xdoor && xdoor->isOpened == 0) {
            msgwin.Add("The door is already closed.");
        } else if (xdoor && xdoor->isOpened) {
            msgwin.Add("You have closed the door.");
            LastStep();
            xdoor->Switch();
            FirstStep(x, y, l);
        } else {
            msgwin.Add("There is no door here.");
        }
    }
}

int XHero::XShoot()
{
    XItem* missile = GetItem(BP_MISSILE);
    const auto missile_w = dynamic_cast<XMissileWeapon *>(GetItem(BP_MISSILE_WEAPON));

    // if no missile, try to load them
    if (!missile) {
        for (auto it: contain) {
            if (it->kind & ItemKind::MISSILE && XMissile::isProperWeapon(it.get(), missile_w)) {
                msgwin.ClrMsg();
                msgwin.Add(fmt::format("Load {}", it->toString()));
                msgwin.Add("["
                    "<KEY>Y<TEXT>, "
                    "<KEY>N<TEXT>, "
                    "<KEY>Esc<TEXT>]?");
                vRefresh();
                const int ch = vGetch();

                if (ch == 'y' || ch == 'Y' || ch == ' ' || ch == KEY_ENTER) {
                    msgwin.ClrMsg();
                    vRefresh();
                    XBodyPart * bp = GetBodyPart(BP_MISSILE);

                    if (bp->Item()) {
                        return 0;
                    }

                    // Stays in contain - it's simply also worn now (see
                    // XBodyPart::Wear()).
                    bp->Wear(it.get());
                    RVIP_SOUND("wear");
                    break;
                }

                if (ch == KEY_ESC) {
                    msgwin.ClrMsg();
                    vRefresh();
                    return 0;
                }
            }
        }

        msgwin.ClrMsg();
        vRefresh();
    }

    missile = GetItem(BP_MISSILE);

    if (!missile || !XMissile::isProperWeapon(missile, missile_w)) {
        if (missile_w) {
            // "ammo" is not a thing you can have one of, and one shoots
            // with a bow rather than from one.
            msgwin.Add(fmt::format("You need proper ammunition to shoot with {}.",
                missile_w->GetNameEx(XItem::Article::DEFINITE)));
        } else {
            msgwin.Add("You need something to throw.");
        }

        return 0;
    }

    XPoint pt;
    int hit = 0;
    int range = 0;
    XDice dmg;
    GetRangeAttackInfo(&range, &hit, &dmg);

    if (Targeting(range, &pt) != ABORT) {
        Shoot(pt.x, pt.y);

        XCreature* tgt = l->map->GetMonster(pt.x, pt.y);

        if (tgt && !(pt.x == x && pt.y == y)) {
            target = XCreature::ToWeakPtr(tgt);
        } else {
            target.reset();
        }

        return 1;
    }

    return 0;
}

int XHero::XCast(std::optional<std::reference_wrapper<std::ofstream>> file)
{
    int ch = '!';

    while (true) {
        XGuiList list;
        list.SetCaption("<DECORATION>###<TEXT> Cast Spell <DECORATION>###");
        if (m->spells.empty()) {
            list.AddItem(new XGuiItem_Text("You do not know any spells", 0));
        } else {
            for (auto& spell : m->spells) {
                list.AddItem(new XGuiItem_SimpleSelect(spell->toString()), 0);
            }
        }

        if (file) {
            list.Put(file);
            return 0;
        }

        ch = list.Run(1);

        if (ch == -1) {
            ch = list.GetLastKey();

            if (ch == 'z' || ch == KEY_ESC || ch == 'Z' || ch == ' ') {
                return 0;
            }
        } else {
            for(auto& spell : m->spells) {
                if (ch == 0) {
                    if (m->Cast(spell.get(), this) == CONTINUE) {
                        return 0;
                    }

                    last_cast = spell.get();
                    break;
                }

                ch--;
            }

            return 1;
        }
    }

    return 0;
}

void XHero::LookAt()
{
    XPoint pt;

    if (!SelectPosition(&pt, 1)) {
        return;
    }

    XCreature* xcr = l->map->GetMonster(pt.x, pt.y);

    XGuiList list;

    if (xcr) {
        std::string str;
        if (xcr->isHero()) {
            str = fmt::format(
                "<DECORATION>### <TEXT>'{}{}<TEXT>' {}, the {} {} {}<DECORATION> ###",
                SCOLOR(xcr->color), xcr->view, xcr->name, GetGenderStr(), GetRaceStr(), GetProfessionStr());
        } else {
            str = fmt::format(
                "<DECORATION>### <TEXT>'{}{}<TEXT>' {}<DECORATION> ###",
                SCOLOR(xcr->color), xcr->view, xcr->GetNameEx(CRN_T1));
        }

        list.SetCaption(str.c_str());

        if (xcr->isHero()) {
            str = fmt::format("<EMPHASIS>You are {}.",
                xcr->GetWoundMsg());
        } else {
            str = fmt::format("<EMPHASIS>{} is {}.",
                xcr->GetNameEx(CRN_T2),
                xcr->GetWoundMsg());

            str[2] = toupper(str[2]);
        }

        list.AddItem(new XGuiItem_Text(str, 0), 0);

        bool iflag = false;

        for (auto& xbp: xcr->components) {
            if (xbp->Item()) {
                if (!iflag) {
                    if (xcr->isHero()) {
                        str = "<EMPHASIS>You are wearing the following items:";
                    } else {
                        str = fmt::format("<EMPHASIS>{} is wearing the following items:", xcr->GetNameEx(CRN_T2));
                        str[2] = toupper(str[2]);
                    }

                    list.AddItem(new XGuiItem_Text(str, 0), 0);
                }

                list.AddItem(new XGuiItem_Text(xbp->Item()->toString(), 0), 0);
                iflag = true;
            }
        }

        list.AddItem(new XGuiItem_Text("", 0), 0);

        if (!xcr->isHero()) {
            list.AddItem(new XGuiItem_Text(xcr->creature_description, 0), 0);
        }
    } else {
        list.AddItem(new XGuiItem_Text(l->map->GetDescription(pt.x, pt.y), 0), 0);
    }

    list.Run();
}

void XHero::ChatWithMonster()
{
    int creature_count = 0;
    const XCreature* last_creature = nullptr;

    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            if (!(i == 0 && j == 0) && l->map->GetMonster(x + i, y + j)) {
                creature_count++;
                last_creature = l->map->GetMonster(x + i, y + j);
            }
        }
    }

    if (creature_count == 0) {
        msgwin.Add("There was no reply.");
    } else if (creature_count == 1) {
        last_creature->xai->Chat(this, "");
    } else {
        XPoint pt;
        WhichDirection(&pt);

        if (l->map->GetMonster(x + pt.x, y + pt.y)) {
            (l->map->GetMonster(x + pt.x, y + pt.y))->xai->Chat(this, "");
        }
    }
}

void XHero::PayBill()
{
    XCreature* shopkeeper = ShopkeeperHere();

    if (!shopkeeper) {
        msgwin.Add("You can pay only in shops.");
        return;
    }

    const auto pai = dynamic_cast<XShopKeeperAI *>(shopkeeper->xai.get());

    // Repeatedly show what's owed and let the player
    // pick one item at a time to settle.
    while (true) {
        int total = static_cast<int>(pai->debt.debtor_sum);

        for (const auto& item: pai->debt.unpaid_items) {
            total += item->GetValue() * item->quantity;
        }

        if (total <= 0) {
            msgwin.Add(fmt::format("You owe nothing to {}.", shopkeeper->name));
            return;
        }

        if (pai->debt.unpaid_items.empty()) {
            // Nothing itemized left to browse - just a flat balance,
            // usually carried over from a previous visit. Settle it in
            // one shot rather than showing an empty list.
            msgwin.Add(fmt::format("You owe {} a total of {}gp. Pay it off?", shopkeeper->name, total));

            if (!GetTarget(TR_YES_NO)) {
                return;
            }

            if (MoneyOp(0) < total) {
                msgwin.Add("You don't have enough money!");
                return;
            }

            MoneyOp(-total);
            pai->debt.debtor_sum = 0;
            msgwin.Add(fmt::format("You paid off your debt to {}.", shopkeeper->name));
            return;
        }

        XGuiList list;
        list.SetCaption("<DECORATION>###<TEXT> Bill <DECORATION>###");
        list.SetFooter(fmt::format(
            "<TEXT>Total owed: <VALUE>{}gp<TEXT>   Your money: <VALUE>{}gp",
            total, MoneyOp(0)));

        for (const auto& item: pai->debt.unpaid_items) {
            list.AddItem(new XGuiItem_Inventory(item.get(), false, true), 0);
        }

        const int item_number = list.Run();

        if (item_number == -1) {
            return;
        }

        const auto picked = pai->debt.unpaid_items[item_number];
        const int price = picked->GetValue() * picked->quantity;

        if (MoneyOp(0) < price) {
            msgwin.Add(fmt::format("You don't have enough money for {}.",
                picked->toSentence()));
            continue;
        }

        MoneyOp(-price);
        pai->debt.unpaid_items.erase(pai->debt.unpaid_items.begin() + item_number);
        msgwin.Add(fmt::format("You paid {}gp for {}.", price,
            picked->toSentence()));
    }
}

void XHero::SetTactics()
{
    while (true) {
        msgwin.ClrMsg();

        std::string s = "Change tactics: ";

        switch (tactics) {
            case TS_COWARD	:
                s.append("Coward ");
                break;

            case TS_DEFENSIVE	:
                s.append("Defensive ");
                break;

            case TS_NORMAL	:
                s.append("Normal ");
                break;

            case TS_AGGRESSIVE	:
                s.append("Aggressive ");
                break;

            case TS_BERSERKER	:
                s.append("Berserker ");
                break;

            default :
                assert(0);
        }

        vGotoXY(0, 0);
        vPutS(s);

        s = fmt::format(" (DV:{:+}, HIT:{:+}, DMG:{:+})      ", GetTacticsDVBonus(), GetTacticsHITBonus(), GetTacticsDMGBonus());
        vPutS(s);

        vGotoXY(0, 1);

        vPutS("<TEXT>[<KEY>+<TEXT>,<KEY>-"
            "<TEXT>] - change, [<KEY>Z<TEXT>] - exit");

        vRefresh();

        const int ch = vGetch();

        if (ch == 'Z' || ch == 'z' || ch == KEY_ESC) {
            msgwin.ClrMsg();
            break;
        }

        if (ch == '+' && tactics < TS_BERSERKER) {
            ChangeTactics(static_cast<TACTICS_STATE>(tactics + 1));
        }

        if (ch == '-' && tactics > TS_COWARD) {
            ChangeTactics(static_cast<TACTICS_STATE>(tactics - 1));
        }
    }
}

int XHero::UseOuterObject()
{
    XMapObject* spec = l->map->GetSpecial(nx, ny);

    if (spec) {
        if (!spec->onOuterUse(this)) {
            msgwin.Add("You can't use this.");
            return 0;
        }

        return 1;
    }

    msgwin.Add("Nothing to use.");

    return 0;
}

// God mode's step-anywhere. The cursor is not held to what the hero can see:
// SelectPosition() scrolls the level along under it, so the far corner of a
// large map is as easy to reach as the next room. Costs no time.
void XHero::GodJump()
{
    XPoint pt;
    const int chosen = SelectPosition(&pt, 0);

    // SelectPosition() leaves its own instructions across the two message
    // rows; anything said here would be written over the top of them.
    msgwin.ClrMsg();

    if (chosen) {
        // FirstStep() insists on an empty cell, and there is no sensible
        // way to stand inside rock or on top of somebody in any case.
        if (l->map->XGetMovability(pt.x, pt.y) == 0) {
            LastStep();
            FirstStep(pt.x, pt.y, l);
        } else {
            msgwin.Add("There is no room for you there.");
        }
    }

    // However the choice ended, and wherever the cursor wandered off to
    // on the way, the view comes back to the hero.
    l->map->ForceRecenter(x, y);
}

int XHero::OrderCompanion()
{
    std::vector<XCreature*> companions_list;

    for (int i = -10; i < 11; i++)
        for (int j = -10; j < 11; j++) {
            if (isVisibleArea(x + i, y + j)) {
                XCreature* cr = l->map->GetMonster(x + i, y + j);
                auto cr_companion = cr ? cr->xai->companion.lock() : nullptr;

                if (cr_companion && cr_companion->isHero()) {
                    companions_list.push_back(cr);
                }
            }
        }

    if (companions_list.empty()) {
        msgwin.Add("You don't have a companion to give orders to.");
        return 0;
    }

    XCreature* slave = nullptr;

    if (companions_list.size() > 1) {
        std::string list;
        int index = 0;

        for (auto it: companions_list) {
            list.append(fmt::format("[{c}] {}\n",
                64 + index, it->GetNameEx(CRN_T1)));
            index++;
        }

        msgwin.Add(list);
        vRefresh();
        const int ch = vGetch();

        if (ch == KEY_ESC || ch == ' ') {
            return 0;
        }

        index = 96;

        for (auto it: companions_list) {
            if (ch == index || ch == (index - 32)) {
                slave = it;
            }

            index++;
        }
    } else {
        slave = *companions_list.begin();
    }

    if (!slave) {
        return 0;
    }

    msgwin.ClrMsg();
    msgwin.Add(
        "[<KEY>a<TEXT>] - attack, "
        "[<KEY>f<TEXT>] - follow me, "
        "[<KEY>w<TEXT>] - wait");

    vRefresh();
    const int ch = vGetch();
    msgwin.ClrMsg();
    vRefresh();

    if (ch == 'a' || ch == 'A') {
        XPoint pt;

        if (!Targeting(GetVisibleRadius(), &pt)) {
            return 0;
        }

        XCreature * tgt = l->map->GetMonster(pt.x, pt.y);

        if (!tgt) {
            return 0;
        }

        if (tgt == slave) {
            return 0;
        }

        msgwin.Add("You command");
        msgwin.Add(slave->GetNameEx(CRN_T1));
        msgwin.Add("to attack");
        msgwin.Add(tgt->GetNameEx(CRN_T1) + ".");
        slave->xai->SetOrderedEnemy(tgt);
        slave->xai->companion_command = CC_ATTACK;
        slave->xai->ResAIFlag(XStandardAI::GUARD_AREA);
        return 1;
    } else if (ch == 'f' || ch == 'F') {
        msgwin.Add("You command");
        msgwin.Add(slave->GetNameEx(CRN_T1));
        msgwin.Add("to come to you.");
        slave->xai->SetOrderedEnemy(nullptr);
        slave->xai->companion_command = CC_FOLLOW;
        slave->xai->ResAIFlag(XStandardAI::GUARD_AREA);
        return 1;
    } else if (ch == 'w' || ch == 'W') {
        msgwin.Add("You command");
        msgwin.Add(slave->GetNameEx(CRN_T1));
        msgwin.Add("to wait.");
        slave->xai->SetOrderedEnemy(nullptr);
        slave->xai->companion_command = CC_WAIT;
        XRect tr(slave->x, slave->y, slave->x + 1, slave->y + 1);
        slave->xai->SetArea(tr, slave->l->id);
        slave->xai->SetAIFlag(XStandardAI::GUARD_AREA);
        return 1;
    }

    return 0;

}

void XHero::doSacrifice()
{
    while (true) {
        std::shared_ptr<XItem> item;

        XItemList* tmpquae = l->map->GetItemList(x, y);

        // Inventory() takes the chosen item out of whichever list it came
        // from, so remember which one: an offering the player thinks
        // better of has to go back where it was found, not into the pack.
        XItemList* from = &contain;

        if (tmpquae->empty() || l->map->GetPlace(x, y)) {
            item = Inventory(&contain, ItemKind::ALL, IF_HIDE_WORN);
        } else {
            from = tmpquae;
            item = Inventory(tmpquae, ItemKind::ALL, IF_HIDE_WORN);
        }

        std::shared_ptr<XItem> drop_item = item;

        if (item) {
            // One offering's worth of messages at a time. Cleared here,
            // after the choosing rather than before it, so the last
            // offering still reads while this one is picked - the same
            // place XHero::DropItem() clears. Without it the prompts and
            // the answers pile up across a whole armful until the two
            // message lines overflow into "(more)".
            msgwin.ClrMsg();

            if (item->quantity > 1) {
                XPoint pt(0, item->quantity);
                msgwin.Add("How much?");
                const int res = GetTarget(TR_HOW_MUCH_SAFE, &pt, item->quantity);

                if (res == 0) {
                    from->insert(item);
                    break;
                }

                if (res != item->quantity) {
                    drop_item = XItem::Own(item->MakeCopy());
                    drop_item->quantity = res;
                    item->quantity -= res;
                    from->insert(item);
                }
            }

            Sacrifice(drop_item.get());
        } else {
            break;
        }
    }
}

void XHero::Pray()
{
    RVIP_SOUND("pray");

    XGuiList list;

    // One section per god a world declares, in the order it declared
    // them. What is on offer, and how much of it a follower of this
    // standing may ask for, comes from the god's own rows.
    struct Offer {
        DEITY deity;
        const DeityHelp* help;
    };

    std::vector<Offer> offers;

    for (const auto& row : deities_db) {
        const DeityRank* rank = religion.GetRank(row.id);
        const std::string rank_name = rank ? rank->name : std::string("unknown");

        list.AddItem(new XGuiItem_Text(fmt::format("<VALUE>{} <TEXT>({}<TEXT>)",
            row.name, rank_name), 0));

        const auto available = religion.AvailableHelp(row.id);

        if (available.empty()) {
            list.AddItem(new XGuiItem_Text("< No help available >", 0));
        } else {
            for (const DeityHelp* help : available) {
                list.AddItem(new XGuiItem_Text(help->name, 1));
                offers.push_back({row.id, help});
            }
        }

        list.AddItem(new XGuiItem_Text("", 0));
    }

    const int res = list.Run();

    if (res < 0 || static_cast<size_t>(res) >= offers.size()) {
        return;
    }

    religion.Pray(offers[res].deity, *offers[res].help, this);
}

int XHero::LearnRecipe(const PotionName pn1, const PotionName pn2, const PotionName pn3)
{
    for (const auto& it : recipe_list)
        if (it->pn1 == pn1 && it->pn2 == pn2) {
            return 0;
        }

    if (XAlchemy::isValidRecipe(pn1, pn2, pn3)) {
        recipe_list.push_back(std::make_unique<XAlchemyRecipe>(pn1, pn2, pn3));
        msgwin.Add("You have learned a new alchemy recipe.");
    }

    return 1;
}
