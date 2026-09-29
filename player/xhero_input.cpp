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

// Reading the player: the turn's key loop, and every question that asks
// where or which way. GetTarget() is XCreature's, overridden here to ask
// the person - the base class asks the AI instead, which is what lets a
// spell work the same for both.

#include <algorithm>
#include <cmath>
#include <memory>
#include <fmt/format.h>

#ifdef __EMSCRIPTEN__
#include "port/be_web.h"
#endif
#include "creature/skeep_ai.h"
#include "creature/xhero.h"
#include "engine/xapi.h"
#include "engine/xarchive.h"
#include "game/game.h"
#include "game/quest.h"
#include "helpers/manual.h"
#include "helpers/msgwin.h"
#include "item/item_misc.h"
#include "item/itemf.h"
#include "magic/modifier.h"
#include "map/map_objects.h"

void XHero::NewMove()
{
    int moved = 0;
    int w_pressed = 0;

    if (isHero()) {
        for (int i = -1; i < 2; i++)
            for (int j = -1; j < 2; j++) {
                XMapObject* obj = l->map->GetSpecial(x + i, y + j);
                auto* trap = dynamic_cast<XTrap *>(obj);

                if (trap && trap->Check(this)) {
                    if (isDisturb > 0) {
                        isDisturb = 0;
                    }
                }
            }
    }

    // Resting is for mending, so it ends when there is nothing left to mend.
    if (isDisturb > 0 && last_char == '5' && HP >= GetMaxHP() && PP >= GetMaxPP()) {
        isDisturb = 0;
        msgwin.Add("You feel fully rested.");
    }

    while (!moved) {
        // Something may be holding the hero still - a paralysis, or
        // whatever else world/modifiers.lua says stops a creature moving.
        const bool held = md->Prevents("move").has_value();
        moved = 1;

        l->map->Center(x, y);
        l->map->Put(this);
        PutStatus();
        vRefresh();

        if (held) {
            moved = 1;
            continue;
        }

        nx = x;
        ny = y;

        int ch;

        // RVIP: an item action from the `i` list is over.
        rvip_pre = nullptr;
        rvip_pre_state = 0;

        if (--isDisturb <= 0) {
            vGotoXY(x + SCR_X - l->map->wx, y + SCR_Y - l->map->wy);
            // RVIP: an explore or stair walk in progress picks the key;
            // after an item action the list reopens unless a hostile is
            // in view.
            ch = ExploreStep();
            if (!ch && rvip_reopen) {
                rvip_reopen = false;
                if (!HostileInView()) {
                    ch = 'i';
                }
            }
            if (!ch) {
                vGotoXY(x + SCR_X - l->map->wx, y + SCR_Y - l->map->wy);
#ifdef __EMSCRIPTEN__
                be_at_cmd(true);
#endif
                ch = vGetch();
#ifdef __EMSCRIPTEN__
                be_at_cmd(false);
#endif
            }
        } else {
            ch = last_char;
        }

        msgwin.ClrMsg();

        // RVIP: Enter opens the command menu, `i` the inventory with its
        // item menus; both hand back the key of the command to run.
        if (ch == KEY_ENTER || ch == '\n') {
            ch = CommandMenu();
        }
        if (ch == 'i') {
            ch = InventoryMenu();
            rvip_reopen = rvip_pre_state == 1;
            if (!ch) {
                moved = 0;
                continue;
            }
        }

        int dx = 0;
        int dy = 0;

        if (vCheckForCursorKey(ch, &dx, &dy)) {
            if (GetCarryState() == CSTATE_OVERBURDEN) {
                msgwin.Add("You can't while overburdened.");
                moved = 0;
                continue;
            }

            nx = x + dx;
            ny = y + dy;
        } else
            switch (ch) {
                case 'w' :
                    if (const auto why = md->Prevents("run")) {
                        msgwin.Add(*why);
                        moved = 0;
                        continue;
                    }

                    if (GetCarryState() == CSTATE_OVERBURDEN) {
                        msgwin.Add("You can't while overburdened.");
                        moved = 0;
                        continue;
                    }

                    isDisturb = 30;
                    last_char = vGetch();

                    if (!vCheckForCursorKey(last_char, nullptr, nullptr)) {
                        isDisturb = 0;
                    }

                    w_pressed = 1;

                    moved = 0;
                    continue;

                case 'Q' :
                    msgwin.Add("Are you sure you want to QUIT the game ");

                    if (GetTarget(TR_YES_NO)) {
                        _exit_flag = 1;
                    } else {
                        moved = 0;
                    }

                    break;

                case '<' : {
                    XMapObject* spec = l->map->GetSpecial(x, y);

                    if (dynamic_cast<XStairWay *>(spec) && spec->view == '<') {
                        for (int q = -10; q < 10; q++)
                            for (int w = -10; w < 10; w++) {
                                l->map->ResVisible(x + w, y + q);
                            }

                        MoveStairWay();
                    } else {
                        // RVIP: walk to the nearest known stair up.
                        moved = 0;
                        if (ExploreStart(2)) {
                            continue;
                        }
                    }

                    break;
                }

                case '>' : {
                    XMapObject* spec = l->map->GetSpecial(x, y);

                    if (dynamic_cast<XStairWay *>(spec) && spec->view == '>') {
#ifdef __EMSCRIPTEN__
                        // RVIP autosave: once, before going down, quietly.
                        XGame::hero_guid = guid();
                        XArchive::StoreGame();
#endif
                        for (int q = -10; q < 10; q++)
                            for (int w = -10; w < 10; w++) {
                                l->map->ResVisible(x + w, y + q);
                            }

                        MoveStairWay();
                    } else {
                        // RVIP: walk to the nearest known stair down.
                        moved = 0;
                        if (ExploreStart(3)) {
                            continue;
                        }
                    }

                    break;
                }

                case 'H' : // RVIP: auto-explore
                    moved = 0;
                    if (ExploreStart(1)) {
                        continue;
                    }
                    break;

                case '~' :
                    isDisturb = 500;
                    last_char = '5';
                    w_pressed = 1;
                    break;

                case 'e' :
                    moved = 0;
                    Equipment();
                    break;

                case 'x' :
                    moved = 0;
                    ExpList();
                    break;

                case '@' :
                    moved = 0;
                    InfoList();
                    break;

                case '[' :
                    moved = 0;
                    CreateScreenShot();
                    break;

                case 'D' :
                    DrinkPotion();
                    break;

                case ',' :
                    PickItem();
                    break;

                case 'd' :
                    DropItem();
                    break;

                case 'o' :
                    OpenDoor();
                    break;

                case '_' :
                case 's' :
                    doSacrifice();
                    break;

                case 'O' :
                    OpenChest();
                    break;

                case 'c' :
                    CloseDoor();
                    break;

                case 'l' :
                    LookAt();
                    moved = 0;
                    break;

                case 'E' :
                    EatFood();
                    break;

                case 't' :
                    moved = XShoot();
                    break;

                case 'Z' :
                    moved = XCast();
                    break;

                case KEY_CTRL_Z:
                    moved = RepeatCast();
                    break;

                case '#' :
                    MagicLevelList();
                    moved = 0;
                    break;

                case 'A' :
                    SkillsList(SKF_LIST_SKILL);
                    moved = 0;
                    break;

                case 'a' :
                    moved = UseSkill();
                    break;

                case 'W' :
                    WarSkillsList();
                    moved = 0;
                    break;

                case 'q' :
                    XQuest::quest.ShowQuests();
                    moved = 0;
                    break;

                case '?' :
                    XManual::Run();
                    moved = 0;
                    break;

                case 'r' :
                    ReadAll();
                    break;

                case 'R' :
                    ShowRecipes();
                    moved = 0;
                    break;

                case '!' :
                    MixPotions();
                    break;

                case 'C' :
                    ChatWithMonster();
                    break;

                case 'g' :
                    GiveItem();
                    break;

                case 'T' :
                    SetTactics();
                    moved = 0;
                    break;

                case 'S' :
                    SaveGame();
                    moved = 0;
                    break;

                case 'u' :
                    moved = UseTool();
                    break;

                case 'U' :
                    moved = UseOuterObject();
                    break;

                case 'P' :
                    PayBill();
                    break;

                case 'p' :
                    Pray();
                    break;

                case 'M' :
                    msgwin.ShowHistory();
                    moved = 0;
                    break;

                case '0' :
                    l->map->ForceRecenter(x, y);
                    moved = 0;
                    break;

                case KEY_CTRL_F:
                    if (XGame::isGodMode) {
                        XGame::isMapRevealed = !XGame::isMapRevealed;
                    }

                    moved = 0;
                    break;

                case KEY_CTRL_W:
                    if (XGame::isGodMode) {
                        GodJump();
                    }

                    moved = 0;
                    break;

                case KEY_CTRL_I:
                    if (XGame::isGodMode) {
                        XGame::isSpectator = !XGame::isSpectator;
                        msgwin.Add(XGame::isSpectator
                            ? "Nothing can see you."
                            : "You are among them again.");
                    }

                    moved = 0;
                    break;

                case KEY_CTRL_T:
                    ActivateTrap();
                    break;

                case KEY_CTRL_O:
                    moved = OrderCompanion();
                    break;

                default :
                    moved = 0;
                    break;
            }

        if (w_pressed) {
            run_way_count = PossibleWayCount(nx, ny);
            w_pressed = 0;
        } else if (isDisturb > 0 && run_way_count != PossibleWayCount(x, y)) {
            isDisturb = 0;
            moved = 0;
            continue;
        }

        if (isDisturb > 0 && moved && l->map->XGetMovability(nx, ny) == 1) {
            int ax = abs(nx - x);
            int ay = abs(ny - y);

            if (ax == 0 && ay == 0) {
                isDisturb = 0;
                break;
            }

            if (ax == 1 && ay == 0) {
                if (l->map->XGetMovability(x, y + 1) == 0 && l->map->XGetMovability(x, y - 1) == 1
                    && PossibleWayCount(x, y) == 2) {
                    last_char = '2';
                    continue;
                }

                if (l->map->XGetMovability(x, y + 1) == 1 && l->map->XGetMovability(x, y - 1) == 0
                    && PossibleWayCount(x, y) == 2) {
                    last_char = '8';
                    continue;
                }
            } else {
                if (ax == 0 && ay == 1) {
                    if (l->map->XGetMovability(x + 1, y) == 0 && l->map->XGetMovability(x - 1, y) == 1
                        && PossibleWayCount(x, y) == 2) {
                        last_char = '6';
                        continue;
                    }

                    if (l->map->XGetMovability(x + 1, y) == 1 && l->map->XGetMovability(x - 1, y) == 0
                        && PossibleWayCount(x, y) == 2) {
                        last_char = '4';
                        continue;
                    }
                } else {
                    isDisturb = 0;
                    continue;
                }
            }
        }

        // Walking into a wall is not a move.
        //
        // A closed door is deliberately not this case - opening it is the
        // action, and that does take the turn. Nor is a creature in the way
        // (movability 2): that is an attack.
        //
        // Only when not running: a run into a wall is already dealt with
        // just above, which either turns the corner or stops the run.
        if (moved && isDisturb <= 0 && (nx != x || ny != y)
            && l->map->XGetMovability(nx, ny) == 1) {
            const auto* door = dynamic_cast<XDoor *>(l->map->GetSpecial(nx, ny));

            if (!door || door->isOpened) {
                nx = x;
                ny = y;
                moved = 0;
                continue;
            }
        }
    }

    if (XQuest::quest.hero_die) {
        return;
    }

    if (l->map->XGetMovability(nx, ny) == 2 && (nx != x || ny != y)) {
        XCreature* cr = l->map->GetMonster(nx, ny);

        if (!cr->xai->isEnemy(this)) {
            msgwin.Add(fmt::format("Are you sure you want to attack {}", cr->name));

            if (!GetTarget(TR_YES_NO)) {
                if ((cr->xai->GetAIFlag() & XStandardAI::NO_SWAP)) { //we can't swap with some guardians
                    nx = x;
                    ny = y;
                } else {
                    XLocation * tl = l;

                    // Unlike a creature moving under its own turn (always kept
                    // alive by the scheduler's own shared_ptr for the whole
                    // turn), cr is a bystander here - its map cell is the only
                    // thing keeping it alive. cr->LastStep() below drops that
                    // reference; without this guard, that can delete cr right
                    // out from under the cr->FirstStep() call two lines down.
                    auto cr_keepalive = std::static_pointer_cast<XCreature>(cr->shared_from_this());
                    cr->LastStep();
                    LastStep();
                    cr->x = x;
                    cr->y = y;
                    cr->FirstStep(x, y, tl);
                    FirstStep(nx, ny, tl);
                }
            }
        }

        isDisturb = 0;
        action_data.action = A_ATTACK;
    } else {
        XMapObject* spec = l->map->GetSpecial(nx, ny);
        auto* door = dynamic_cast<XDoor *>(spec);

        if (door && !door->isOpened) {
            OpenDoor();
            nx = x;
            ny = y;
        }
    }
}

int XHero::WhichDirection(XPoint* pt, const int flag)
{
    msgwin.Add("Which direction [123456789, z]?");
    vRefresh();

    while (true) {
        const int ch = vGetch();

        if (vCheckForCursorKey(ch, &pt->x, &pt->y)) {
            if (!flag && pt->x == 0 && pt->y == 0) {
                continue;
            }

            return 1;
        }

        if (ch == 'z' || ch == KEY_ESC) {
            vRefresh();
            msgwin.ClrMsg();
            return 0;
        }
    }
}

int XHero::Targeting(int range, XPoint * pt)
{
    // target may have died independently since the last shot; lock once and
    // reuse for the rest of this function rather than re-locking per use.
    auto target_sp = target.lock();

    if (target_sp && !target_sp->isValid()) {
        target.reset();
        target_sp = nullptr;
    }

    const XCreature* tgt = nullptr;
    int dist = 10000;

    for (int i = -7; i <= 7; i++)
        for (int j = -7; j <= 7; j++) {
            int cx = x + j;
            int cy = y + i;

            if (cx < 0 || cx >= l->map->len || cy < 0 || cy >= l->map->hgt) {
                continue;
            }

            if (l->map->GetMonster(cx, cy) && l->map->GetMonster(cx, cy)->isVisible()
                && l->map->GetMonster(cx, cy)->xai->isEnemy(this)
                && std::max(abs(i), abs(j)) < dist && !(j == 0 && i == 0)) {
                dist = std::max(abs(i), abs(j));
                tgt = l->map->GetMonster(cx, cy);
            }
        }

    int tx = x;
    int ty = y;

    if (tgt && !target_sp) {
        tx = tgt->x;
        ty = tgt->y;
    } else if (target_sp && target_sp->isVisible()) {
        tx = target_sp->x;
        ty = target_sp->y;
    }

    while (true) {
        auto xrng = std::sqrt(static_cast<float>((tx - x) * (tx - x) + (ty - y) * (ty - y)));
        float cos_alpha;
        float sin_alpha;

        if (xrng > 0) {
            cos_alpha = static_cast<float>(tx - x) / xrng;
            sin_alpha = static_cast<float>(ty - y) / xrng;
        } else {
            cos_alpha = 0;
            sin_alpha = 0;
        }

        auto mx = static_cast<float>(x);
        auto my = static_cast<float>(y);

        l->map->Center(tx, ty);
        l->map->Put(this);

        int flag = 1;
        int trange = range;

        while (sqrt((tx - mx) * (tx - mx) + (ty - my) * (ty - my)) >= 0.5f) {
            trange--;
            mx += cos_alpha;
            my += sin_alpha;

            if ((std::lround(mx) != x || std::lround(my) != y) && flag) {
                if (trange >= 0) {
                    l->map->PutChar(std::lround(mx), std::lround(my), '*', xYELLOW);
                } else {
                    l->map->PutChar(std::lround(mx), std::lround(my), '*', xRED);
                }
            }

            if ((l->map->GetMovability(std::lround(mx), std::lround(my)) >= XTileType::Movability::WALL
                || !l->map->GetVisible(std::lround(mx), std::lround(my)))
                && flag) {
                if (trange >= 0) {
                    l->map->PutChar(std::lround(mx), std::lround(my), 'X', xYELLOW);
                } else {
                    l->map->PutChar(std::lround(mx), std::lround(my), 'X', xRED);
                }

                flag = 0;
            }

            xrng -= 1;
        }

        if (trange >= 0 && flag) {
            l->map->PutChar(tx, ty, 'X', xYELLOW);
        } else {
            l->map->PutChar(tx, ty, 'X', xRED);
        }

        vRefresh();

        int ch = vGetch();

        if (ch == ' ' || ch == 't') {
            pt->x = tx;
            pt->y = ty;
            return 1;
        }

        if (ch == KEY_ESC) {
            return ABORT;
        }

        int dx = 0;
        int dy = 0;

        if (vCheckForCursorKey(ch, &dx, &dy)) {
            tx = tx + dx;

            if (tx < 0) {
                tx = 0;
            }

            if (tx >= l->map->len) {
                tx = l->map->len - 1;
            }

            ty = ty + dy;

            if (ty < 0) {
                ty = 0;
            }

            if (ty >= l->map->hgt) {
                ty = l->map->hgt - 1;
            }
        }
    }
}

int XHero::SelectPosition(XPoint * pt, int flag)
{
    int tx = x;
    int ty = y;

    msgwin.ClrMsg();
    vGotoXY(0, 0);

    if (flag) {
        vPutS("press [<KEY>1<TEXT>..<KEY>9"
            "<TEXT>] - for move cursor, ["
            "<KEY>z<TEXT>, <KEY>esc<TEXT>] - for exit");
    } else {
        // Without the description line, space is what picks the spot
        // rather than what asks about it - so say so.
        vPutS("press [<KEY>1<TEXT>..<KEY>9"
            "<TEXT>] - for move cursor, [<KEY>space<TEXT>] - to choose, ["
            "<KEY>z<TEXT>, <KEY>esc<TEXT>] - for exit");
    }

    while (1) {
        l->map->Center(tx, ty);
        l->map->Put(this);
        l->map->PutChar(tx, ty, 'X', xYELLOW);

        int more_info_flag = 0;
        vGotoXY(0, 1);

        if (flag) {
            if (l->map->GetVisible(tx, ty)) {
                XCreature * cr = l->map->GetMonster(tx, ty);
                XMapObject * m_obj = l->map->GetSpecial(tx, ty);
                const unsigned int item_count = l->map->GetItemCount(tx, ty);

                if (cr && cr->isVisible()) {
                    more_info_flag = 1;
                    vPutS(fmt::format("{} ({}) [<KEY>space<TEXT>] - more.",
                        cr->name, cr->GetWoundMsg()));
                } else if (item_count == 1) {
                    vPutS("item");
                } else if (item_count > 1) {
                    vPutS("some items");
                } else if (m_obj) {
                    vPutS(m_obj->GetName(this));
                } else {
                    vPutS(l->map->GetDescription(tx, ty));
                }
            } else {
                vPutS("invisible at this time.");
            }

            vClrEol();
        }

        vRefresh();
        const int ch = vGetch();

        if (!flag || more_info_flag) {
            if (ch == ' ' || ch == 't') {
                pt->x = tx;
                pt->y = ty;
                return 1;
            }
        }

        if (ch == KEY_ESC || ch == 'z' || ch == 'Z') {
            return 0;
        }

        if (ch == KEY_RESIZE) {
            l->map->ForceRecenter(tx, ty);
            continue;
        }

        int dx = 0;
        int dy = 0;

        if (vCheckForCursorKey(ch, &dx, &dy)) {
            tx = tx + dx;
            ty = ty + dy;

            if (tx < 0) {
                tx = 0;
            }

            if (tx >= l->map->len) {
                tx = l->map->len - 1;
            }

            if (ty < 0) {
                ty = 0;
            }

            if (ty >= l->map->hgt) {
                ty = l->map->hgt - 1;
            }
        }
    }
}

int XHero::GetTarget(const TARGET_REASON tr, XPoint* pt, int max_range, XObject** back)
{
    int flag = 0;
    char ch;
    int cx;
    int cy;
    char in_buf[256];
    int value;
    int gets_flag = 0;

    switch (tr) {
        case TR_ATTACK_DIRECTION:
            flag = WhichDirection(pt);
            break;

        case TR_ATTACK_TARGET:
            return Targeting(max_range, pt);
            break;

        case TR_YES_NO:
            msgwin.Add("(Yes/<KEY>N<TEXT>o)?");
            vRefresh();
            ch = vGetch();
            msgwin.ClrMsg();

            if (ch == 'Y' || ch == 'y') {
                return 1;
            }

            return 0;
            break;

        case TR_NO_YES:
            msgwin.Add("(<KEY>Y<TEXT>es/No)?");
            vRefresh();
            ch = vGetch();
            msgwin.ClrMsg();

            if (ch == 'N' || ch == 'n') {
                return 0;
            }

            return 1;
            break;

        case TR_HOW_MUCH:
        case TR_HOW_MUCH_SAFE:
            if (max_range < pt->x) {
                max_range = pt->x;
            }

            if (max_range > pt->y) {
                max_range = pt->y;
            }

            vGetCursorPos(&cx, &cy);

            if (cx > 60) {
                cy++;
                cx = 0;
            }

            if (cy > 2) {
                msgwin.ClrMsg();
                cx = 0;
                cy = 0;
            }

            // What an empty answer means, said plainly - and for the safe
            // sort it means none, so that a Return arriving where an
            // Escape was meant stops rather than commits.
            if (tr == TR_HOW_MUCH_SAFE) {
                msgwin.Add("(Enter = none)? ");
            } else {
                msgwin.Add(fmt::format("(Enter = {})? ", max_range));
            }

            vPutS(" ");
            vRefresh();
            gets_flag = vGetS(in_buf, 9);

            if (gets_flag == 1 && strlen(in_buf) == 0) {
                return tr == TR_HOW_MUCH_SAFE ? 0 : max_range;
            }

            if (gets_flag == 0) {
                return 0;
            }

            value = atoi(in_buf);

            if (value < pt->x) {
                return pt->x;
            }

            if (value > pt->y) {
                return pt->y;
            }

            return value;
            break;

        case TR_STEAL_ITEM:
            assert(back);

            if (GetTarget(TR_ATTACK_DIRECTION, pt)) {
                // IF_NO_ERASE: *back only carries a raw XObject* across to
                // the caller (UseSteal(), in magic/skill.cpp) - it can't
                // preserve a shared_ptr, so the item must stay owned by its
                // current container (cr->contain / the ground item_list)
                // rather than being erased here, or it would be destroyed
                // before the caller ever sees it. UseSteal() erases it from
                // the source itself, once the theft actually succeeds.
                std::shared_ptr<XItem> item;
                XCreature *cr = l->map->GetMonster(x + pt->x, y + pt->y);

                if (cr) {
                    if (cr->xai->isEnemy(this)) {
                        msgwin.Add("You couldn't steal anything from the enemy.");
                        return 0;
                    }

                    if ((item = Inventory(&cr->contain, ItemKind::ALL, IF_NO_ERASE))) {
                        *back = item.get();
                        return 1;
                    }

                    return 0;
                }

                XAnyPlace* pl = l->map->GetPlace(x + pt->x, y + pt->y);

                auto pl_owner = pl ? pl->GetOwner().lock() : nullptr;

                if (pl_owner && l->map->GetItemCount(x + pt->x, y + pt->y) > 0) {
                    if (pl_owner->xai->isEnemy(this)) {
                        msgwin.Add("You can't.");
                        return 0;
                    }

                    item = Inventory(l->map->GetItemList(x + pt->x, y + pt->y), ItemKind::ALL, IF_NO_ERASE);

                    if (item) {
                        *back = item.get();
                        return 1;
                    }

                    return 0;
                }
            } else {
                return 0;
            }

            break;
        default: ;
    }

    return flag;
}
