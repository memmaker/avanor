/*
This file is part of "Avanor, the Land of Mystery" roguelike game

RVIP stage 3: the Enter command menu and the inventory list with a cursor
and item menus. GPL-2.0-or-later, like the rest of the game.
*/

#include <vector>
#include <fmt/format.h>

#include "creature/xhero.h"
#include "helpers/msgwin.h"
#include "helpers/xgui.h"
#include "creature/bodypart.h"
#include "item/item.h"
#include "map/map.h"

XItem* rvip_pre = nullptr;
int rvip_pre_state = 0;
bool rvip_pre_oneshot = false;
int rvip_pick_key = 0;

bool XHero::HostileInView()
{
    XMap* m = l->map;

    for (int yy = 0; yy < m->hgt; yy++)
        for (int xx = 0; xx < m->len; xx++) {
            if (!m->GetVisible(xx, yy)) {
                continue;
            }

            XCreature* cr = m->GetMonster(xx, yy);

            if (cr && cr != this && isCreatureVisible(cr) && cr->xai->isEnemy(this)) {
                return true;
            }
        }

    return false;
}

// Every command, grouped as manual/kblayout.html groups them; moves
// (digits, `w`, wait) left out. The keyset is fixed (no keymaps).
int XHero::CommandMenu()
{
    static const std::vector<XBoxEntry> cmds = {
        {0, "Moving and locations"},
        {'H', "explore"},
        {'~', "rest until something happens"},
        {'o', "open a door"},
        {'c', "close a door"},
        {'<', "go up stairs / walk to stair up"},
        {'>', "go down stairs / walk to stair down"},
        {'l', "look at a location"},
        {0, "Dealing with objects"},
        {'e', "equipment"},
        {'i', "inventory"},
        {'d', "drop an item"},
        {',', "pick up an item"},
        {'E', "eat"},
        {'D', "drink a potion"},
        {'!', "mix potions"},
        {'r', "read a book or scroll"},
        {'s', "sacrifice an item"},
        {'O', "open a chest"},
        {'g', "give an item"},
        {'u', "use your tool"},
        {'P', "quick pay"},
        {0, "Characteristics and skills"},
        {'A', "skill levels"},
        {'a', "use a skill"},
        {'q', "quests"},
        {'@', "character details"},
        {'W', "weapon skills"},
        {'x', "experience for next level"},
        {0, "Combat and spellcasting"},
        {'t', "target an opponent"},
        {'p', "pray"},
        {'T', "combat tactics"},
        {'Z', "cast a spell"},
        {KEY_CTRL_Z, "repeat last spell"},
        {'#', "magic school levels"},
        {0, "Miscellaneous"},
        {'U', "use outer objects"},
        {'R', "alchemy recipes"},
        {'C', "chat"},
        {KEY_CTRL_O, "order a companion"},
        {KEY_CTRL_T, "activate a trap here"},
        {'S', "save the game"},
        {'Q', "quit the game"},
        {'M', "message history"},
        {'0', "recenter the screen"},
        {'[', "screenshot"},
        {'?', "manual"},
    };

    return XBoxMenu("Commands", cmds);
}

namespace {

enum { ACT_WEAR = 0x10001, ACT_TAKEOFF, ACT_EXAMINE };

struct Act { int key; int label; const char* name; };

// Parts the item could be worn on: the Equipment() screen's own test.
std::vector<XBodyPart*> FitParts(XCreature* cr, XItem* it)
{
    std::vector<XBodyPart*> v;

    for (auto& bp : cr->components) {
        if (static_cast<bool>(bp->GetProperKind() & it->kind)
            && (bp->bp_uin == BP_HAND || bp->bp_uin == it->bp)) {
            v.push_back(bp.get());
        }
    }

    return v;
}

// Actions that fit, main action first. Keys are the game's commands, run
// through NewMove() with the item preselected.
std::vector<Act> Actions(XCreature* cr, XItem* it)
{
    std::vector<Act> v;
    const bool worn = cr->IsWorn(it);
    const XBodyPart* tool = cr->GetBodyPart(BP_TOOL);

    if (tool && tool->Item() == it) v.push_back({'u', 'u', "use"});
    if (it->kind & ItemKind::FOOD) v.push_back({'E', 'E', "eat"});
    if (it->kind & ItemKind::POTION) v.push_back({'D', 'D', "drink"});
    if (it->kind & (ItemKind::BOOK | ItemKind::SCROLL)) v.push_back({'r', 'r', "read"});
    if (worn) v.push_back({ACT_TAKEOFF, 'e', "take off"});
    else if (!FitParts(cr, it).empty()) v.push_back({ACT_WEAR, 'e', "wear / wield"});
    if (it->kind & ItemKind::POTION) v.push_back({'!', '!', "mix"});
    if (!worn) {
        v.push_back({'d', 'd', "drop"});
        v.push_back({'s', 's', "sacrifice"});
        v.push_back({'g', 'g', "give"});
    }
    v.push_back({ACT_EXAMINE, '*', "examine"});

    return v;
}

void Examine(XItem* it)
{
    msgwin.Add(fmt::format("{} (weight {}, value {}).", it->toString(), it->weight, it->GetValue()));
}

} // namespace

// `i`: the inventory with a cursor. Letter = main action, Shift+letter
// drops, Ctrl+letter examines, 5/Enter/Space opens the item menu, numpad
// + - * main/drop/examine. Returns a command key for NewMove() (with the
// item preselected when it is an item action), or 0.
int XHero::InventoryMenu()
{
    while (true) {
        int picked_key = 0;
        rvip_pre_state = 0;
        rvip_pre = nullptr;

        // IF_NO_ERASE: Inventory() only tells which item; the command
        // itself takes it out of the pack. IF_CURSOR: any other key
        // comes back in rvip_pick_key.
        auto sp = Inventory(&contain, ItemKind::ALL, static_cast<INVENTORY_FLAG>(IF_NO_ERASE | IF_CURSOR));
        const int how = rvip_pick_key;

        if (!sp) {
            // A key that picked nothing: 4/6 = equipment, Esc/0 = close,
            // anything else runs as a normal command.
            if (how == '4' || how == '6') {
                Equipment();
                continue;
            }
            if (how == KEY_ESC || how == 'z' || how == 'Z' || how == ' ' || how == 0) {
                return 0;
            }
            return how;
        }

        XItem* it = sp.get();
        const auto acts = Actions(this, it);
        int act;

        if (how == KEY_ENTER || how == '\n' || how == '5') {
            std::vector<XBoxEntry> e;
            for (const auto& a : acts) e.push_back({a.label, a.name});
            const int k = XBoxMenu(it->toString(), e);
            if (!k) continue;
            act = 0;
            for (const auto& a : acts) if (a.label == k) { act = a.key; break; }
        } else if (how == '-' || (how >= 'A' && how <= 'Z')) {
            act = IsWorn(it) ? ACT_TAKEOFF : 'd';
        } else if (how == '*' || (how >= 1 && how <= 26)) {
            act = ACT_EXAMINE;
        } else {
            act = acts.front().key;
        }

        switch (act) {
            case ACT_EXAMINE:
                Examine(it);
                continue;
            case ACT_TAKEOFF:
                for (auto& bp : components) if (bp->Item() == it) bp->UnWear();
                continue;
            case ACT_WEAR: {
                auto parts = FitParts(this, it);
                XBodyPart* use = parts.front();
                for (auto* bp : parts) if (!bp->Item()) { use = bp; break; }
                if (use->Item()) use->UnWear();
                use->Wear(it);
                continue;
            }
            default:
                picked_key = act;
        }

        rvip_pre = it;
        rvip_pre_state = 1;
        rvip_pre_oneshot = act != '!';
        return picked_key;
    }
}
