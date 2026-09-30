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

#ifndef XHERO_H
#define XHERO_H

#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <fmt/format.h>

#include <cereal/types/base_class.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>

#include "item/xpotion.h"
#include "creature/creature.h"
#include "engine/global.h"
#include "helpers/xgui.h"
#include "helpers/xstring.h"

extern int _exit_flag;

enum INVENTORY_FLAG {
    IF_NONE,
    IF_FIXED_MASK,
    IF_VIEW_ONLY,
    // Select and return an item without removing it from item_list - unlike
    // IF_VIEW_ONLY (which never returns a selection at all), this still
    // returns the chosen item, just leaves it where it lives. Needed
    // whenever the raw pointer handed back has to survive past this
    // function's own scope (e.g. through an XObject** out-param) without
    // item_list's shared_ptr ownership going away under it.
    IF_NO_ERASE = 4,

    // Leave out anything the hero is currently wearing. Since worn items
    // stay in contain, they turn up in every list drawn from it - which
    // is right when you are looking at what you own, and wrong the
    // moment the list is asking you to part with something. Offering a
    // worn item and refusing it afterwards is worse than not offering it.
    IF_HIDE_WORN = 8,

    // RVIP: return nullptr on any key that is no filter key (rvip_pick_key
    // holds the key an item was picked with, or the key that closed it).
    IF_CURSOR = 16,
};

enum SKILL_FLAG {
    SKF_IMPROVE_SKILL,
    SKF_LIST_SKILL,
    SKF_USE_SKILL,
};

// RVIP: item preselect for the next Inventory() prompt (xhero_menu.cpp).
// state 1: take rvip_pre if that list shows it, else return nothing;
// state 2: used, further prompts of a one-shot command return nothing.
class XItem;
extern XItem* rvip_pre;
extern int rvip_pre_state;
extern bool rvip_pre_oneshot;
extern int rvip_pick_key;

class XHero final : public XCreature
{
    protected:
        XHero()
        {
            last_char = '5';
            isDisturb = 0;
            run_way_count = 0;
            target.reset();
        }

        friend class cereal::access;

        int last_char;
        int run_way_count;
        std::weak_ptr<XCreature> target; // for convenient user interface
        int PossibleWayCount(int px, int py) const;
    public:
        DECLARE_CREATOR(XHero, XCreature);

        // Builds a brand-new character - rolls the dice, runs
        // PlayerSetup(), equips a body. It cannot be the default
        // constructor: that one is the quiet one above, which Cereal
        // uses to bring a saved hero back without creating a character
        // over the top of it. Hence a tag rather than the `int flag`
        // this used to take, which named nothing and was ignored.
        struct NewCharacter {};
        explicit XHero(NewCharacter);
        void PlayerSetup();
        void NewMove() override;
        // RVIP auto-explore and stair walks (player/xhero_explore.cpp).
        int ExploreStep();
        bool ExploreStart(int mode);
        bool HostileInView();
        // RVIP stage 3 (player/xhero_menu.cpp): Enter command menu and the
        // `i` list with a cursor and item menus. Both return a command key
        // for NewMove() to run (0 = none).
        int CommandMenu();
        int InventoryMenu();
        bool rvip_reopen = false; // reopen `i` after an item action
        void Move() override;

        // Says that something in plain view cannot be made out, so the hero
        // has the chance to reach for a potion before it reaches them.
        // What is said is world/creature_classes.lua's business.
        void SenseUnseen();

        // The shopkeeper of the shop the hero is standing in, or null
        // anywhere else. What the inventory needs to know whether it is
        // looking at somebody else's goods.
        [[nodiscard]] XCreature* ShopkeeperHere() const;

        std::shared_ptr<XItem> Inventory(XItemList* item_list, ItemKind mask = ItemKind::ALL, INVENTORY_FLAG flag = IF_NONE,
                         int ret_item_count = 0, XItemFilter* ifiltr = nullptr,
                         std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt) const;
        void Equipment(std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt);
        void PickItem();
        void DropItem();
        void LookAt();
        static void CreateScreenShot();
        static void DumpVBuffer(std::ofstream &file);
        void ReadAll();
        void ExpList() const;
        void InfoList();
        // The hero's eat command: choose something, then eat it
        // through XCreature::Eat(). Named apart from that one the way
        // ReadAll() is, so neither hides the other.
        void EatFood();
        int XShoot();
        int Targeting(int range, XPoint* pt);
        int GetTarget(TARGET_REASON tr, XPoint* pt = nullptr, int max_range = 0, XObject** back = nullptr) override; //Get target for a spell
        std::shared_ptr<XItem> SelectItem(XItemFilter* filter, bool isGetAll = false) override;

        int SelectPosition(XPoint * pt, int flag = 0);
        unsigned int turn_count{};

        // Whether something unseen was within sight last turn, so that the
        // warning is given when one arrives rather than once a turn for as
        // long as it lingers.
        bool sensed_unseen{};

        void OpenDoor();
        void OpenChest();
        void CloseDoor();
        void Die(XCreature* killer) override;
        int XCast(std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt);
        XSpell* last_cast;
        int RepeatCast();

        void MagicLevelList() const;
        XSkill* SkillsList(SKILL_FLAG skill_flag, int marks_left = 0,
            std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt) const;
        int UseSkill();
        void IncLevel() override;
        void WarSkillsList(std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt) const;

        void DrinkPotion();
        void SetTactics();
        void SaveGame();
        int UseTool();
        int UseOuterObject();
        void PayBill();
        void Pray();
        static int WhichDirection(XPoint* pt, int flag = 1); // flag == 1 - allow 0,0 coords (self)
        std::shared_ptr<XItem> onIdentifyItem() override;
        void ShowResistance(std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt);

        void GodJump();
        void ActivateTrap();
        void GiveItem();
        void ChatWithMonster();
        bool Chat(XCreature* chatter, const char* msg) override;

        void FirstStep(int _x, int _y, XLocation* _l) override;
        void LastStep() override;

        void stopAction() override;

        // last_char/run_way_count/target/last_cast are transient
        // per-turn UI state, and melee_attack is a non-owning pointer
        // into a static table (hero_melee, private to xhero.cpp) - none
        // of these were ever persisted even before Cereal (the existing
        // Restore() above resets every one of them to the same fresh
        // values the default constructor uses), so FixupHeroDefaults()
        // (defined in xhero.cpp, where hero_melee is visible) just
        // replicates that reset here rather than persisting them.
        void FixupHeroDefaults();

        template<class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::base_class<XCreature>(this));
            ar(race, race_name, profession, profession_name, turn_count, recipe_list);
            FixupHeroDefaults();
        }

        void doSacrifice();
        int OrderCompanion();

        // The keys world/hero.lua defined these under, and the names it
        // showed in the menus. Both are kept: the key is what content
        // matches on, and the name is what the character sheet and the
        // memorial show - which must still read correctly for a character
        // whose race the world has since renamed or dropped.
        std::string race;
        std::string race_name;
        std::string profession;
        std::string profession_name;

        const char* GetRaceStr() const;
        const char* GetProfessionStr() const;

        // RVIP: with report_ev ("death"/"win"/"quit") only compute the score,
        // send the run report (graveyard beacon) and return - called before
        // any key wait, so a tab closed on the end screens keeps the run.
        static void EndGame(const char* end_msg, const char* report_ev = nullptr, const char* killer = nullptr);

        // ALCHEMY
        int LearnRecipe(PotionName pn1, PotionName pn2, PotionName pn3);
        void ShowRecipes() const;
        void MixPotions();
        std::vector<std::unique_ptr<XAlchemyRecipe>> recipe_list;
};

class XGuiItem_Inventory final : public XGuiItem
{
        std::string str;
    public:
        // show_price swaps the trailing badge from the
        // item's weight to its total gp value.
        explicit XGuiItem_Inventory(XItem* item, bool worn = false, bool show_price = false,
                                    int unpaid = 0)
        {
            str = "<TEXT>" + item->toString();

            if (worn) {
                str += "<DECORATION> (worn)";
            }

            // A heap can be part bought and part not - two rations carried
            // in and one taken off the shelf - so say how many are owed
            // unless the whole heap is.
            if (unpaid >= item->quantity) {
                str += "<WARNING> (unpaid)";
            } else if (unpaid > 0) {
                str += fmt::format("<WARNING> ({} unpaid)", unpaid);
            }

            // Pad to size_x characters using spaces. str may contain ANSI
            // escape codes that do not count toward the visible width.
            // x_strlen returns the visible length. The padding is based on this.
            const size_t visible = static_cast<size_t>(x_strlen(str.c_str()));
            if (visible < static_cast<size_t>(size_x))
                str.append(static_cast<size_t>(size_x) - visible, ' ');

            // Align the weight/price badge to the right. x_strlen measures
            // the visible width of the badge (excluding ANSI characters).
            const std::string badge = show_price
                ? fmt::format("<DECORATION>[<TEXT>{}gp<DECORATION>]", item->GetValue() * item->quantity)
                : fmt::format("<DECORATION>[<TEXT>{}<DECORATION>]", item->weight * item->quantity);
            const size_t badge_visible = static_cast<size_t>(x_strlen(badge.c_str()));

            // Insertion position in str. Measured back from str's actual
            // byte length, not size_x: the padding appended above is
            // plain spaces (1 byte per visible column) all the way to the
            // end of str, so this naturally lands badge_visible + 5
            // visible columns before the end regardless of how many color
            // codes ("<TEXT>", plus "<DECORATION>" if worn) precede it.
            // Using size_x here instead used to get that wrong by however
            // many color-code bytes preceded the padding - correct for an
            // unworn item's one code, 2 bytes short for a worn item's two.
            const size_t insert_pos = str.size() - 5 - badge_visible;
            str.replace(insert_pos, badge.size(), badge);
        }

        int isSelectable() override
        {
            return 1;
        }

        bool SetWidth(std::string::size_type /*new_width*/) override
        {
            return true;
        }

        size_t GetHeight() override
        {
            return 1;
        }

        const char* operator[](const std::size_t /*index*/) override
        {
            return str.c_str();
        }
};

#endif
