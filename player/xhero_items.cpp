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

// The item lists, and the commands that work through one: choosing from a
// pack or a pile, and what happens to what was chosen.
//
// The file-scope statics below are this file's own: the inventory
// remembers which list it last showed and how far down it was scrolled, so
// that reopening it lands where the player left it.

#include "port/rvip_sound.h"
#include <memory>
#include <fmt/format.h>

#include "creature/skeep_ai.h"
#include "creature/xhero.h"
#include "game/game.h"
#include "game/quest.h"
#include "helpers/manual.h"
#include "helpers/msgwin.h"
#include "item/item_misc.h"
#include "item/itemf.h"
#include "item/xpotion.h"
#include "item/xtool.h"
#include "magic/modifier.h"
#include "map/map_objects.h"

auto smask = "[|{}'=!?\"\\%]]$X";

ItemKind kind_list[] = {
    ItemKind::HAT | ItemKind::BODY | ItemKind::BOOTS | ItemKind::GLOVES | ItemKind::CLOAK | ItemKind::SHIELD,
    ItemKind::WEAPON, ItemKind::MISSILEW, ItemKind::MISSILE,
    ItemKind::NECK, ItemKind::RING, ItemKind::POTION, ItemKind::SCROLL, ItemKind::BOOK,
    ItemKind::WAND, ItemKind::FOOD, ItemKind::LIGHTSOURCE,
    ItemKind::TOOL, ItemKind::MONEY, ItemKind::ALL
};

ItemKind output_items_mask[] = {
    ItemKind::HAT, ItemKind::BODY, ItemKind::CLOAK, ItemKind::GLOVES, ItemKind::BOOTS, ItemKind::SHIELD,
    ItemKind::WEAPON, ItemKind::NECK, ItemKind::RING, ItemKind::MISSILEW, ItemKind::MISSILE, ItemKind::POTION,
    ItemKind::SCROLL, ItemKind::BOOK, ItemKind::WAND, ItemKind::FOOD, ItemKind::LIGHTSOURCE, ItemKind::TOOL, ItemKind::MONEY
};

auto output_items_ext = "[[[[[[|'={}!?\"\\%]]$";

const char* output_items_name[] = {
    "Helmets", "Armours", "Cloaks", "Gloves", "Boots", "Shields",
    "Weapon", "Necklaces", "Rings", "Missile weapon", "Missiles", "Potions",
    "Scrolls", "Books", "Wands", "Food", "Light sources", "Tools", "Money"
};

// first_item must be 0 if need to start from first item
static int first_item = 0;

static XItemList* pLastList = nullptr;

const char* part_names[] = {"",
        "Head", "Necklace", "Body", "Cloak",
        "Left hand", "Left ring", "Gloves",
        "Boots", "Light source", "Tool", "Missile weapon", "Missile", "eof"
    };

std::shared_ptr<XItem> XHero::Inventory(XItemList* item_list, ItemKind mask, const INVENTORY_FLAG flag, const int ret_item_count,
    XItemFilter* ifiltr, const std::optional<std::reference_wrapper<std::ofstream>> file) const
{
    // Only while standing in a shop is there anything unpaid to mark.
    const XCreature* shopkeeper = ShopkeeperHere();
    const auto* keeper = shopkeeper
        ? dynamic_cast<const XShopKeeperAI*>(shopkeeper->xai.get()) : nullptr;

    // What this list will show. Everything that walks the list asks it -
    // the count, the drawing, and the walk that turns the chosen line
    // back into an item.
    const auto shows = [this, mask, flag, ifiltr](XItem* item) {
        if ((flag & IF_HIDE_WORN) && IsWorn(item)) {
            return false;
        }

        return (ifiltr && ifiltr(item)) || static_cast<bool>(item->kind & mask);
    };

    while (true) {
        XGuiList list;

        if (&contain == item_list) {
            list.SetCaption("<DECORATION>###<TEXT> Inventory <DECORATION>###");
        } else {
            list.SetCaption("<DECORATION>###<TEXT> Items <DECORATION>###");
        }

        list.SetFooter("<TEXT>filtr: <DECORATION>[<VALUE>[|{}'=!?\"\\%]$X<DECORATION>]");

        //count items for show
        int all_item_count = 0;

        for (const auto& it : *item_list) {
            if (shows(it.get())) {
                all_item_count++;
            }
        }

        if (all_item_count == 0) {
            if ((mask == ItemKind::ALL) || (mask == ItemKind::UNKNOWN))
                if (&contain == item_list) {
                    list.AddItem(new XGuiItem_Text("<TEXT>You have no such items."), 0);
                } else {
                    list.AddItem(new XGuiItem_Text("<TEXT>There are no such items."), 0);
                } else {
                for (size_t oi = 0; oi < std::size(output_items_name); oi++) {
                    if (output_items_mask[oi] & mask) {
                        std::string msg;
                        if (&contain == item_list) {
                            msg = fmt::format("<TEXT>You have no {}.", output_items_name[oi]);
                        } else {
                            msg = fmt::format("<TEXT>There are no {}.", output_items_name[oi]);
                        }

                        list.AddItem(new XGuiItem_Text(msg), 0);
                    }
                }
            }
        } else {
            ItemKind last_mask = ItemKind::UNKNOWN;

            for (const auto& item: *item_list) {
                if (shows(item.get())) {
                    // we need to show item group name (e.g. boots, weapons etc.)
                    if (item->kind != last_mask) {
                        // skip output empty string for first item in inventory
                        if (last_mask != ItemKind::UNKNOWN) {
                            list.AddItem(new XGuiItem_Text(""), 0);
                        }

                        last_mask = item->kind;

                        for (size_t oi = 0; oi < std::size(output_items_name); oi++) {
                            if (output_items_mask[oi] & last_mask) {
                                auto str = fmt::format(
                                    "<VALUE>{} <DECORATION>('<VALUE>{}<DECORATION>')",
                                    output_items_name[oi], output_items_ext[oi]);
                                list.AddItem(new XGuiItem_Text(str), 0);
                            }
                        }
                    }

                    //output item
                    list.AddItem(new XGuiItem_Inventory(item.get(), IsWorn(item.get()), false,
                                                       keeper ? keeper->UnpaidQuantity(item.get()) : 0), 0);
                }
            }
        }

        //output list
        if (pLastList != item_list) {
            pLastList = item_list;
            first_item = 0;
        }

        if (file) {
            list.Put(file);
            return nullptr;
        }

        // RVIP: a preselected item (item actions from the `i` menu) is
        // taken without showing the list; a prompt of that command that
        // doesn't show it, or comes after it, gets nothing.
        int item_number;
        if (rvip_pre_state == 2 && rvip_pre_oneshot) {
            return nullptr;
        }
        if (rvip_pre_state == 1) {
            item_number = -1;
            int n = 0;
            for (const auto& it : *item_list) {
                if (shows(it.get())) {
                    if (it.get() == rvip_pre) { item_number = n; break; }
                    n++;
                }
            }
            if (item_number < 0) {
                return nullptr;
            }
            rvip_pre_state = 2;
        } else {
            list.EnableCursor();
            item_number = list.Run(1, first_item);
            first_item = list.GetTopItemIndex();
            rvip_pick_key = list.GetLastKey();
        }

        if (item_number == -1 || (flag & IF_VIEW_ONLY)) { //there was no item selected
            const int ch = list.GetLastKey();

            if (!(flag & IF_FIXED_MASK)) {
                for (unsigned int i = 0; i < strlen(smask); i++)
                    if (ch == smask[i]) {
                        mask = kind_list[i];
                    }
            }

            if (ch == 0 || ch == KEY_ESC || ch == 'z' || ch == 'v' || ch == 'V' || ch == 'Z' || ch == ' ') {
                break;
            }

            if ((flag & IF_CURSOR) && !(!(flag & IF_FIXED_MASK) && strchr(smask, ch) && ch)) {
                break;
            }
        } else {
            auto selected_it = item_list->begin();
            int stop_flag = -1;

            while (selected_it != item_list->end()) {
                if (shows(selected_it->get()) && ++stop_flag == item_number) {
                    break;
                }

                ++selected_it;
            }

            // Only reachable if the drawing and this walk disagreed about
            // which items the list holds, which is what `shows` exists to
            // prevent.
            assert(selected_it != item_list->end());

            if (selected_it == item_list->end()) {
                break;
            }

            // Capture the shared_ptr before any erase below - item_list
            // can be ritem's only reference, and erasing it while it's
            // still valid would run XItem::Own()'s deleter and invalidate
            // it outright instead of just handing it to the caller.
            std::shared_ptr<XItem> ritem = *selected_it;

            if (flag & IF_NO_ERASE) {
                return ritem;
            }

            item_list->erase(selected_it);

            if (ret_item_count <= 0) {
                return ritem;
            }

            if (ritem->quantity <= ret_item_count) {
                return ritem;
            }

            auto sitem = XItem::Own(ritem->MakeCopy());
            sitem->quantity = ret_item_count;
            ritem->quantity -= ret_item_count;
            item_list->insert(ritem);

            return sitem;

        }
    }

    return nullptr;
}

void XHero::Equipment(const std::optional<std::reference_wrapper<std::ofstream>> file)
{
    XBodyPart* xqsa[30]; //this array save us from typing hard algorithm
    first_item = 0;

    while (true) {
        int was_hand = 0;
        int was_ring = 0;
        int counter = 0;

        XGuiList list;

        list.SetCaption("<DECORATION>###<TEXT> Equipment <DECORATION>###");
        list.SetFooter("<DECORATION>[<VALUE>V<DECORATION>]<TEXT> - show inventory.");
        list.EnableCursor();

        for (auto& xbp: components) {
            xqsa[counter] = xbp.get();

            // Determine name of body part
            std::string_view part_name;
            if (xbp->Fit(BP_HAND) || xbp->Fit(BP_RING)) {
                if (xbp->Fit(BP_HAND)) {
                    part_name = (was_hand == 0) ? part_names[xbp->bp_uin] : "Right hand";
                    was_hand++;
                }
                if (xbp->Fit(BP_RING)) {
                    part_name = (was_ring == 0) ? part_names[xbp->bp_uin] : "Right ring";
                    was_ring++;
                }
            } else {
                part_name = part_names[xbp->bp_uin];
            }

            // Left column
            std::string left = fmt::format("<TEXT>{}", part_name);
            const auto visible = static_cast<size_t>(x_strlen(left.c_str()));
            if (visible < 20)
                left.append(20 - visible, ' ');

            // Right column
            std::string right;
            if (xbp->Item()) {
                right = fmt::format("<DECORATION>: <TEXT>{}",
                                    xbp->Item()->toString());
            } else {
                right = "<DECORATION>: <DECORATION>-";
            }

            const std::string buf = left + right;
            list.AddItem(new XGuiItem_SimpleSelect(buf));
            counter++;
        }

        if (file) {
            list.Put(file);
            return;
        }

        if (int ch = list.Run(1); ch == -1) {
            ch = list.GetLastKey();

            if (ch == 0 || ch == KEY_ESC || ch == ' ' || ch == 'z' || ch == 'Z') {
                break;
            }

            if (ch == 'v' || ch == 'V') {
                Inventory(&contain, ItemKind::ALL, IF_VIEW_ONLY);
            }
        } else {
            const int n = ch;
            XItem* witem = xqsa[n]->Item();

            if (witem != nullptr) {
                // Worn items stay resident in contain the whole time
                // they're worn (see XBodyPart::Wear()) - UnWear() just
                // clears the slot, nothing needs putting back.
                xqsa[n]->UnWear();
            } else {
                // IF_NO_ERASE: picking something to wear must not remove
                // it from contain - it's meant to stay visible there,
                // worn or not.
                std::shared_ptr<XItem> picked = Inventory(&contain, xqsa[n]->GetProperKind(), static_cast<INVENTORY_FLAG>(IF_FIXED_MASK | IF_NO_ERASE));

                if (picked) {
                    if (xqsa[n]->bp_uin == BP_HAND) {
                        xqsa[n]->Wear(picked.get());
                        RVIP_SOUND("wear");
                    } else if (xqsa[n]->bp_uin == picked->bp) {
                        xqsa[n]->Wear(picked.get());
                        RVIP_SOUND("wear");
                    }
                }
            }
        }
    }
}

void XHero::EatFood()
{
    if (nutrio > base_nutrio * 16) {
        msgwin.Add("You can't eat any more!");
    } else {
        first_item = 0;

        std::shared_ptr<XItem> food;
        XItemList* tmpquae = l->map->GetItemList(x, y);

        if (!tmpquae->empty()) {
            food = Inventory(tmpquae, ItemKind::FOOD, IF_FIXED_MASK, 1);

            XAnyPlace* place = l->map->GetPlace(x, y);

            if (place && food && !place->onCreaturePickItem(this, food.get())) {
                vRefresh();
                return;
            }

        }

        if (!food) {
            food = Inventory(&contain, ItemKind::FOOD, IF_FIXED_MASK, 1);
        }

        vRefresh();

        if (food) {
            XCreature::Eat(dynamic_cast<XAnyFood *>(food.get()));
        }
    }
}

void XHero::ReadAll()
{
    first_item = 0;
    std::shared_ptr<XItem> item = Inventory(&contain, ItemKind::BOOK | ItemKind::SCROLL, IF_FIXED_MASK, 1);

    if (item) {
        if (item->kind & ItemKind::SCROLL) {
            if (!XCreature::Read(item.get())) {
                contain.insert(item);
            }
        } else if (item->kind & ItemKind::BOOK) {
            if (!XCreature::Read(item.get())) {
                contain.insert(item);
            }
        }
    }

    vRefresh();
}

void XHero::DrinkPotion()
{
    first_item = 0;
    auto pot_sp = Inventory(&contain, ItemKind::POTION, IF_FIXED_MASK, 1);
    auto pot = dynamic_cast<XPotion *>(pot_sp.get());

    if (pot) {
        RVIP_SOUND("quaff");
        pot->onDrink(this);
        pot->UnCarry();
        pot->Invalidate();
    }
}

void XHero::DropItem()
{
    std::shared_ptr<XItem> item;
    first_item = 0;

    while (contain.begin() != contain.end()
           && (item = Inventory(&contain, ItemKind::ALL, IF_HIDE_WORN))) {
        std::shared_ptr<XItem> drop_item = item;

        if (item->quantity > 1) {
            msgwin.ClrMsg();
            XPoint pt(0, item->quantity);
            msgwin.Add("How much?");
            const int res = GetTarget(TR_HOW_MUCH, &pt, item->quantity);

            if (res == 0) {
                contain.insert(item);
                break;
            }

            if (res != item->quantity) {
                drop_item = XItem::Own(item->MakeCopy());
                drop_item->quantity = res;
                item->quantity -= res;
                contain.insert(item);
            }
        }

        if (!XCreature::DropItem(drop_item.get())) {
            contain.insert(drop_item);
            return;
        }

        msgwin.Add(fmt::format("{} drops {}.", name, drop_item->toSentence()));
    }
}

void XHero::PickItem()
{
    XItemList* tmpquae = l->map->GetItemList(x, y);

    if (tmpquae->empty()) {
        XMapObject* obj = l->map->GetSpecial(x, y);

        // Pick() may destroy obj (self-eviction from its map cell) -
        // safe without a keepalive here because eviction goes through
        // XObject's deferred-release graveyard, which keeps the object
        // allocated until between turns.
        XItem* picked = (obj && obj->isValid()) ? dynamic_cast<XItem *>(obj->Pick(this)) : nullptr;

        if (!picked) {
            msgwin.Add("There is nothing to pick up here.");
        } else {
            const auto tit = XItem::Own(picked);

            auto desc = tit->toSentence();
            if (PickUpItem(tit.get())) {
                msgwin.Add(fmt::format("You pick up {}.", desc));
            } else {
                tit->Invalidate();
            }
        }
    } else if (tmpquae->size() == 1) {
        std::shared_ptr<XItem> tit = *(tmpquae->begin());
        tmpquae->erase(tit);

        if (PickUpItem(tit.get())) {
            msgwin.Add(fmt::format("You pick up {}.", tit->toSentence()));
        } else {
            tmpquae->insert(tit);
        }
    } else {
        std::shared_ptr<XItem> tit;
        // The item actually picked up, tracked separately from the loop's
        // own `tit` - the while condition's last assignment to `tit` is
        // whatever made the loop exit (an empty shared_ptr from Inventory()
        // when the player cancels selection, or a rejected-and-returned
        // item), not necessarily the one successful pickup, so `nitem == 1`
        // does not imply `tit` still points at it.
        std::shared_ptr<XItem> last_picked;
        int nitem = 0;

        while (!tmpquae->empty() && (tit = Inventory(tmpquae))) {
            if (PickUpItem(tit.get())) {
                nitem++;
                last_picked = tit;
            } else {
                // we can't pick item, so return it back
                tmpquae->insert(tit);
                vRefresh();
                vGetch();
            }
        }

        if (nitem == 1) {
            msgwin.Add(fmt::format("You pick up {}.", last_picked->toSentence()));
        } else if (nitem > 1) {
            msgwin.Add("You pick up a heap of items.");
        }
    }
}

void XHero::OpenChest()
{
    XItemList* tq = l->map->GetItemList(x, y);
    int chest_count = 0;
    XChest* last_chest = nullptr;

    for (const auto& it : *tq) {
        if (it->kind == ItemKind::CHEST) {
            last_chest = dynamic_cast<XChest *>(it.get());
            chest_count++;
        }
    }

    if (chest_count > 1) {
        auto chest_sp = Inventory(tq, ItemKind::CHEST, IF_NONE, 1);
        last_chest = dynamic_cast<XChest *>(chest_sp.get());

        if (!last_chest) {
            return;
        }

        std::shared_ptr<XItem> it;

        do {
            it = Inventory(&last_chest->contain);

            if (it) {
                last_chest->weight -= it->weight;

                if (!ContainItem(it.get())) {
                    it->Drop(l, x, y);
                }
            }
        } while (it);

        tq->insert(chest_sp);
    } else if (chest_count == 1) {
        msgwin.Add("Do you wish to open the chest?");

        if (GetTarget(TR_NO_YES)) {
            std::shared_ptr<XItem> it;

            do {
                it = Inventory(&last_chest->contain);

                if (it) {
                    last_chest->weight -= it->weight;

                    if (!ContainItem(it.get())) {
                        it->Drop(l, x, y);
                    }
                }
            } while (it);

        }
    } else {
        msgwin.Add("There is no chest here.");
    }
}

void XHero::GiveItem()
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
        msgwin.Add("There is no creature to give anything to.");
        return;
    }

    if (creature_count > 1) {
        XPoint pt;
        WhichDirection(&pt);

        if (l->map->GetMonster(x + pt.x, y + pt.y)) {
            last_creature = l->map->GetMonster(x + pt.x, y + pt.y);
        }
    }

    if (auto item = Inventory(&contain, ItemKind::ALL, IF_HIDE_WORN)) {
        if (item->kind & ItemKind::MONEY) {
            contain.insert(item);
            last_creature->xai->onGiveItem(this, item.get());
        } else {
            int res = item->quantity;

            if (res > 1) {
                XPoint pt(1, res);
                res = GetTarget(TR_HOW_MUCH, &pt, res);
            }

            bool flag = true;
            std::shared_ptr<XItem> gitem;

            if (res < item->quantity && res > 0) {
                gitem = XItem::Own(item->MakeCopy());
                gitem->quantity = res;
                item->quantity -= res;
                contain.insert(item);
            } else {
                gitem = item;
            }

            if (res > 0) {
                carried_weight -= item->weight * res;

                // Cleared up front so "onGiveItem() answered true" splits
                // unambiguously below into its two real meanings: a handler
                // that wants to actually keep the item (e.g
                // XCreature::ContainItem()) sets a new owner via CarryItem()
                // while it runs, one that just consumes it for a quest
                // (Yohji's rat tail, Todin's weapon) does not touch owner at
                // all - gitem->GetOwner() below tells the two apart. Left
                // pointing at the hero, a pure-consume handler would look
                // "claimed" and the leak this guards against would be back.
                gitem->SetOwner(nullptr);
                flag = last_creature->xai->onGiveItem(this, gitem.get());
            }

            if (!flag || res == 0) {
                // Only a refused give has weight to put back, and it is the
                // weight of everything that left: res == 0 means the "how
                // much?" prompt was answered with nothing, which never
                // reached the subtraction above, and a partial give that
                // came back used to be credited with a single item's weight
                // however large the heap offered had been.
                if (!flag) {
                    carried_weight += gitem->weight * gitem->quantity;
                }

                contain.insert(gitem);
                gitem->SetOwner(this);
            } else if (!gitem->GetOwner().lock()) {
                // Kept, and genuinely unclaimed - nothing above puts it
                // anywhere else, so gitem and item (the same object,
                // whole-stack gives alias one another) are about to drop
                // their last reference. XItem::Own()'s deleter would normally
                // catch that and Invalidate() it, but an item that was sitting
                // on the ground rather than in a creature's contain when the
                // game was last saved never got Own()'s deleter in the first
                // place - Cereal constructs its own shared_ptr for that case,
                // a plain one with no such safety net, and every later
                // reference (including the one PickUpItem() just handed this
                // function via shared_from_this()) aliases that same control
                // block. Invalidate() explicitly rather than trust the
                // deleter, same idiom XItem::Concat() and PickItem()'s reject
                // path already use - it sets is_valid false itself, so
                // whichever deleter actually runs, ~XObject()'s assert sees
                // it already cleared.
                gitem->Invalidate();
            }
        }
    }
}

int XHero::UseTool()
{
    if (const XBodyPart* tbp = GetBodyPart(BP_TOOL))
    {
        if (auto tool = dynamic_cast<XTool *>(tbp->Item())) {
            UseItem(tool);
            return 1;
        }
    }

    msgwin.Add("You have no tool.");

    return 0;
}

void XHero::MixPotions()
{
    auto item1 = Inventory(&contain, ItemKind::POTION, IF_FIXED_MASK, 1);
    auto pot1 = dynamic_cast<XPotion *>(item1.get());

    if (pot1) {
        auto item2 = Inventory(&contain, ItemKind::POTION, IF_FIXED_MASK, 1);
        auto pot2 = dynamic_cast<XPotion *>(item2.get());

        if (pot2) {
            const PotionName pn = XAlchemy::GetPotionName(pot1->pn, pot2->pn);
            const PotionDescription* pr = PotionDescription::GetRec(pot1->pn);
            int val = sk->GetLevel(XSkill::Skill::ALCHEMY) * 8 + 30 - pr->alchemy_power * 10;

            if (pn != PN_NONE && vRand(100) < val) {
                const auto pot = new XPotion(pn);
                sk->UseSkill(XSkill::Skill::ALCHEMY, 3);
                msgwin.Add(fmt::format("You have mixed {}.", pot->toSentence()));
                CarryItem(pot);
                contain.insert(XItem::Own(pot));
            } else {
                msgwin.Add("You failed to mix a new potion.");
            }

            pot1->Invalidate();
            pot2->Invalidate();
        }
    }
}
