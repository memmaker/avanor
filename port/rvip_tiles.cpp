// RVIP stage 4: DawnLike tiles for the map (browser build only).
//
// XMap::Put() asks here which sprite each map cell shows, from the game's
// own data: terrain id (autotiled against the real level), map object
// class, item content id / type / potion appearance / kind, monster id or
// class, hero race. The slots come from port/dawn_map.inc (web/mkdawn.py).
// be_tile() keeps each cell's choice with the char+colour it was drawn
// with; be_web.cpp sends a tile only while the screen cell still shows that
// char+colour, so menus drawn over the map stay text.
#include "port/rvip_tiles.h"

#include <string>
#include <unordered_map>

#include "creature/creature.h"
#include "creature/xhero.h"
#include "item/item.h"
#include "item/xherb.h"
#include "item/xpotion.h"
#include "map/map.h"
#include "map/map_objects.h"

namespace {

const std::unordered_map<std::string, int> slots = {
#include "port/dawn_map.inc"
};

int Slot(const std::string& key)
{
    const auto it = slots.find(key);
    return it == slots.end() ? -1 : it->second;
}

int KindSlot(ItemKind kind)
{
    static const std::pair<ItemKind, const char*> kinds[] = {
        {ItemKind::HAT, "HAT"}, {ItemKind::NECK, "NECK"}, {ItemKind::BODY, "BODY"},
        {ItemKind::CLOAK, "CLOAK"}, {ItemKind::WEAPON, "WEAPON"}, {ItemKind::SHIELD, "SHIELD"},
        {ItemKind::GLOVES, "GLOVES"}, {ItemKind::RING, "RING"}, {ItemKind::BOOTS, "BOOTS"},
        {ItemKind::MISSILEW, "MISSILEW"}, {ItemKind::MISSILE, "MISSILE"}, {ItemKind::POTION, "POTION"},
        {ItemKind::SCROLL, "SCROLL"}, {ItemKind::BOOK, "BOOK"}, {ItemKind::WAND, "WAND"},
        {ItemKind::FOOD, "FOOD"}, {ItemKind::LIGHTSOURCE, "LIGHTSOURCE"}, {ItemKind::TOOL, "TOOL"},
        {ItemKind::GEM, "GEM"}, {ItemKind::MONEY, "MONEY"}, {ItemKind::CHEST, "CHEST"},
        {ItemKind::OTHER, "OTHER"},
    };

    for (const auto& k : kinds) {
        if (kind & k.first) {
            return Slot(std::string("k:") + k.second);
        }
    }

    return Slot("k:OTHER");
}

bool IsWall(const XMap* map, int x, int y)
{
    if (x < 0 || y < 0 || x >= map->len || y >= map->hgt) {
        return false;
    }

    const XMapTile* c = map->Cell(x, y);

    return c && (std_tile_data[c->n].movability == XTileType::Movability::WALL
                 || dynamic_cast<XDoor*>(c->pSpecialObject.get()));
}

bool SameFloor(const XMap* map, int x, int y, int n)
{
    if (x < 0 || y < 0 || x >= map->len || y >= map->hgt) {
        return true;
    }

    const XMapTile* c = map->Cell(x, y);

    return !c || c->n == n;
}

} // namespace

int RvipTerrainTile(const XMap* map, int x, int y)
{
    const XMapTile* c = map->Cell(x, y);

    if (!c || c->n == XTileType::NONE) {
        return -1;
    }

    const std::string& id = std_tile_data[c->n].id_name;
    int base;

    if ((base = Slot("t:" + id)) >= 0) {
        return base;
    }

    if ((base = Slot("tf:" + id)) >= 0) {
        const int n = c->n;
        return base + (SameFloor(map, x, y - 1, n) ? 0 : 8) + (SameFloor(map, x, y + 1, n) ? 0 : 4)
             + (SameFloor(map, x - 1, y, n) ? 0 : 2) + (SameFloor(map, x + 1, y, n) ? 0 : 1);
    }

    if ((base = Slot("tw:" + id)) >= 0) {
        return base + (IsWall(map, x, y - 1) ? 8 : 0) + (IsWall(map, x, y + 1) ? 4 : 0)
             + (IsWall(map, x - 1, y) ? 2 : 0) + (IsWall(map, x + 1, y) ? 1 : 0);
    }

    return -1;
}

int RvipObjectTile(XMapObject* o)
{
    if (auto* s = dynamic_cast<XStairWay*>(o)) {
        // Its direction is kept only as its glyph (set from the type).
        return Slot(s->view == '<' ? "o:up" : "o:down");
    }

    if (auto* d = dynamic_cast<XDoor*>(o)) {
        return Slot(d->isOpened ? "o:door_open" : "o:door_closed");
    }

    if (auto* t = dynamic_cast<XTrap*>(o)) {
        const int s = Slot("tr:" + t->GetTrapType());
        return s >= 0 ? s : Slot("o:trap");
    }

    if (dynamic_cast<XTeleport*>(o)) {
        return Slot("o:teleport");
    }

    if (dynamic_cast<XAltar*>(o)) {
        return Slot("o:altar");
    }

    if (dynamic_cast<XGrave*>(o)) {
        return Slot("o:grave");
    }

    if (auto* l = dynamic_cast<XLuaObject*>(o)) {
        return Slot("lo:" + l->GetContentId());
    }

    return -1; // furniture, outer objects: text
}

int RvipItemTile(const XItem* ci)
{
    auto* item = const_cast<XItem*>(ci);
    int s;

    if (auto* p = dynamic_cast<XPotion*>(item)) {
        // By appearance, which is dealt out per game; never by kind.
        const PotionDescription* d = PotionDescription::GetRec(p->pn);

        if (d && (s = Slot("pc:" + d->force_color)) >= 0) {
            return s;
        }

        return Slot("i:potion");
    }

    if (auto* h = dynamic_cast<XHerb*>(item)) {
        if (h->isIdentified() && (s = Slot("p:" + h->Species())) >= 0) {
            return s;
        }

        return Slot("i:herb");
    }

    const std::string cid = item->GetContentId();

    if (!cid.empty() && (s = Slot("f:" + cid)) >= 0) {
        return s;
    }

    if ((s = Slot("f:" + item->it)) >= 0 || (s = Slot("i:" + item->it)) >= 0) {
        return s;
    }

    return KindSlot(item->kind);
}

int RvipCreatureTile(XCreature* cr)
{
    int s;

    if (auto* h = dynamic_cast<XHero*>(cr)) {
        if ((s = Slot("h:" + h->race)) >= 0) {
            return s;
        }
    }

    if ((s = Slot("c:" + cr->creature_name)) >= 0) {
        return s;
    }

    if ((s = Slot("cc:" + cr->creature_class)) >= 0) {
        return s;
    }

    return Slot("cc:other");
}

// ---- Inventory and Visible windows (RVIP stage 5) ----
// Lines for the page, from the hero's own data: names, colours (the
// game's rgb), sprite slots. Sent only when they change.
#include "helpers/xgui.h"
#include <cstdio>
#include "map/map.h"

void RvipSendSide(int which, const std::string& s); // be_web.cpp

namespace {
std::string Css(unsigned rgb)
{
    char b[8];
    snprintf(b, sizeof b, "#%06x", rgb & 0xFFFFFF);
    return b;
}
std::string Plain(const std::string& s) // markup and escapes out
{
    std::string out;
    const std::string e = ExpandMarkup(s);
    for (size_t i = 0; i < e.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(e[i]);
        if (c == 31) {
            i++;
        } else if (c == static_cast<unsigned char>(RGB_ESCAPE)) {
            i += 6;
        } else if (c >= ' ' && c != '\t' && c != '\n') {
            out += static_cast<char>(c);
        }
    }
    return out;
}
}

extern ItemKind output_items_mask[];
extern const char* output_items_name[];

void RvipSidePanes(XCreature* cr, const XMap* map)
{
    auto* hero = dynamic_cast<XHero*>(cr);
    if (!hero || !map) {
        return;
    }

    // Inventory: the pack in the game's own order (as `i` lists it), a
    // header per kind. Line: "<letter>\t<glyph>\t<name>\t<css>\t<tile>",
    // "=<header>" for a section.
    std::string inv;
    int n = 0;
    ItemKind last = ItemKind::UNKNOWN;
    for (const auto& it : hero->contain) {
        if (it->kind != last) {
            last = it->kind;
            for (int oi = 0; oi < 19; oi++) {
                if (output_items_mask[oi] & last) {
                    inv += std::string("=") + output_items_name[oi] + "\n";
                    break;
                }
            }
        }
        std::string name = Plain(it->toString());
        if (hero->IsWorn(it.get())) {
            name += " (worn)";
        }
        const int k = n++;
        inv += std::string(1, static_cast<char>('A' + k >= 'Z' ? 'A' + k + 1 : 'A' + k)) + "\t" + std::string(1, it->view) + "\t" + name + "\t"
             + Css(it->color) + "\t" + std::to_string(RvipItemTile(it.get())) + "\n";
    }
    if (inv.empty()) {
        inv = "=You carry nothing.\n";
    }
    RvipSendSide(4, inv);

    // Visible: monsters and items in the hero's sight (RvipWM.visible lines).
    std::string mon, itm;
    for (int y = 0; y < map->hgt; y++) {
        for (int x = 0; x < map->len; x++) {
            if (!map->GetVisible(x, y)) {
                continue;
            }
            XCreature* m = map->GetMonster(x, y);
            if (m && m != hero && hero->isCreatureVisible(m)) {
                mon += "M" + std::string(1, m->view) + Plain(m->GetNameEx(CRN_T1)) + "\t" + Css(m->color) + "\t"
                     + std::to_string(RvipCreatureTile(m)) + "\n";
            }
            if (XItemList* l = map->GetItemList(x, y)) {
                for (const auto& it : *l) {
                    itm += "I" + std::string(1, it->view) + Plain(it->toString()) + "\t" + Css(it->color) + "\t"
                         + std::to_string(RvipItemTile(it.get())) + "\n";
                }
            }
        }
    }
    RvipSendSide(5, mon + itm);
}
