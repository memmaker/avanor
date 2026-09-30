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

// The game around the game: making a character, saving, gaining a level,
// and the end of it all.

#include "port/rvip_sound.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include <fmt/format.h>
#include <sol/sol.hpp>

#include "creature/skeep_ai.h"
#include "creature/xhero.h"
#include "engine/xarchive.h"
#include "engine/xlua.h"
#include "game/game.h"
#include "game/quest.h"
#include "helpers/hiscore.h"
#include "helpers/manual.h"
#include "helpers/msgwin.h"
#include "helpers/xstring.h"
#include "item/item_misc.h"
#include "magic/modifier.h"

namespace {

// Draws a lettered menu and waits for one of its letters to be pressed.
//
// The menu is drawn inside the loop rather than before it, so that a
// terminal which changes shape while somebody is choosing gets the menu
// laid out again for the size it now is. Everything else on screen is
// redrawn from scratch each time round, which costs nothing here and
// means the resize needs no special case beyond falling through to the
// next turn of the loop.
int ChooseFromMenu(const char* title, const std::vector<std::string>& names,
    const std::vector<std::string>& notes = {})
{
    while (true) {
        vClrScr();
        vGotoXY(7, 4);
        vPutS(title);

        // Laid out top to bottom rather than by index, because an entry
        // with something to say about itself is several lines tall and
        // one without is a single line.
        int row = 6;

        for (std::size_t i = 0; i < names.size(); i++) {
            vGotoXY(7, row++);
            vPutS(fmt::format("<TEXT>[<SELECTOR>{:c}<TEXT>] {} ",
                              static_cast<int>(i) + 'a', names[i]));

            if (i >= notes.size() || notes[i].empty()) {
                continue;
            }

            // Indented under the name it belongs to, and wrapped to
            // whatever the terminal is now - the loop above redraws on a
            // resize, so this has to measure rather than assume.
            for (const auto& line : WrapText(notes[i], size_x - 15)) {
                vGotoXY(11, row++);
                vPutS(fmt::format("<TEXT>{}", line));
            }

            row++;
        }

        vRefresh();

        if (const int ch = vGetch();
            ch >= 'a' && ch < 'a' + static_cast<int>(names.size())) {
            vClrScr();

            return ch;
        }
    }
}

} // namespace

void XHero::IncLevel()
{
    RVIP_SOUND("level");
    msgwin.Add("Congratulations! You have advanced to a new level. Press any key.");
    l->map->Put(this);
    vRefresh();
    vGetch();
    msgwin.ClrMsg();

    int counter = stats->Get(XStats::LEN) / 5 + 1;

    if (counter < 3) {
        counter = 3;
    }

    while (counter > 0) {
        int flag = 0;

        for (const auto& [skt, skill] : sk->skills) {
            if (skill->GetLevel() < skill->GetMaxLevel()) {
                flag = 1;
                break;
            }
        }

        if (flag) {
            XSkill* tskill = SkillsList(SKF_IMPROVE_SKILL, counter);

            if (tskill && tskill->GetLevel() < tskill->GetMaxLevel()) {
                tskill->IncLevel();
                counter--;

                if (counter == 0) {
                    SkillsList(SKF_LIST_SKILL);
                }
            }
        } else {
            SkillsList(SKF_LIST_SKILL);
            break;
        }
    }

    XCreature::IncLevel();
}

void XHero::SaveGame()
{
    V_BUFFER xyzbuf;
    vStore(&xyzbuf);
    vClrScr();
    vGotoXY(0, 0);
    vPutS("<TEXT>Storing the game:");
    vRefresh();
    XGame::hero_guid = guid();

    const bool saved = XArchive::StoreGame();
    vRestore(&xyzbuf);
    vRefresh();

    // Saving can fail - a full disk, a home directory that cannot be
    // written to - and until now it failed silently, leaving the player
    // to find out when there was nothing to restore.
    if (!saved) {
        msgwin.Add("The game could not be saved!");
    }
};

void XHero::PlayerSetup()
{
    sol::state_view lua(XLua::State());

    // The whole of the offer in one answer: every race, and for each of
    // them the professions it allows. The engine builds menus from this and
    // decides none of it - world/hero.lua does.
    sol::table races = lua["HeroRaces"]();
    std::vector<std::string> race_labels;

    for (auto& [_, row] : races) {
        race_labels.push_back(row.as<sol::table>()["name"]);
    }

    const int race_choice = ChooseFromMenu("<TEXT>Choose a race:", race_labels);
    sol::table chosen_race = races[race_choice - 'a' + 1];
    race = chosen_race["key"];
    race_name = chosen_race["name"];

    sol::table genders = lua["HeroGenders"]();
    std::vector<std::string> gender_labels;

    for (auto& [_, row] : genders) {
        gender_labels.push_back(row.as<sol::table>()["name"]);
    }

    const int gender_choice = ChooseFromMenu("<TEXT>Choose a gender:", gender_labels);
    sol::table chosen_gender = genders[gender_choice - 'a' + 1];
    const std::string gender_key = chosen_gender["key"];

    // The pronoun comes from the entry rather than from its place in the
    // menu, so a world may offer these in any order or offer more of them.
    // YOU is added here and not there: being addressed in the second person
    // is what makes this creature the player, which is the engine's business
    // and not the world's.
    const int pronoun = chosen_gender["pronoun"];
    creature_person_type = static_cast<XCreature::PersonType>(pronoun | XCreature::YOU);

    // Only what this race may take up.
    sol::table professions = chosen_race["professions"];
    std::vector<std::string> profession_labels;

    for (auto& [_, row] : professions) {
        profession_labels.push_back(row.as<sol::table>()["name"]);
    }

    {
        const int ch = ChooseFromMenu("<TEXT>Choose a profession:", profession_labels);
        sol::table chosen_profession = professions[ch - 'a' + 1];
        profession = chosen_profession["key"];
        profession_name = chosen_profession["name"];

        // Who this one worships, asked rather than worked out. A world
        // that declares no gods offers no choice and the hero simply has
        // no patron, which is what every creature in the world has.
        std::string deity_key;
        sol::table deities = lua["HeroDeities"]();
        std::vector<std::string> deity_labels;
        std::vector<std::string> deity_notes;

        for (auto& [_, row] : deities) {
            const sol::table god = row.as<sol::table>();
            deity_labels.push_back(god["name"]);
            deity_notes.push_back(god["description"].get_or<std::string>(""));
        }

        if (!deity_labels.empty()) {
            const int dch = ChooseFromMenu("<TEXT>Choose a patron deity:",
                deity_labels, deity_notes);
            sol::table chosen_deity = deities[dch - 'a' + 1];
            deity_key = chosen_deity["key"];

            // Where offerings go from here on, unless an altar says
            // otherwise. Set before InitHero so content may read it back.
            religion.SetPatron(deity_key);
        }

        // Everything those choices mean - the figures, the pace, the
        // stomach, what the race simply knows, what standing the patron
        // starts with - belongs to content.
        if (sol::protected_function init = lua["InitHero"]; init.valid()) {
            if (const auto result = init((void*)this, race, gender_key,
                    profession, deity_key);
                !result.valid()) {
                const sol::error err = result;
                std::cerr << "world: InitHero: " << err.what() << std::endl;
            }
        }

        vClrScr();
    }

    vClrScr();
    vGotoXY(0, 4);
    vPutS("<TEXT>Enter character name (15 letters max.): ");
    vRefresh();
    char char_name[20];
    vGetS(char_name, 15);
    name = char_name;
}

void XHero::DumpVBuffer(std::ofstream& file)
{
    for (int j = 0; j < size_y; j++) {
        for (int k = 0; k < size_x; k++) {
            file << vTestCh(k, j);
        }

        file << "\n";
    }
}

void XHero::CreateScreenShot()
{
    for (int i = 0; i < 1000; i++) {
        auto filename = fmt::format("shot{:03}.txt", i);

        if (!std::filesystem::exists(filename)) {
            std::ofstream file(filename);
            DumpVBuffer(file);

            msgwin.Add(fmt::format("Screenshot '{}' created successfully.",
                filename));

            return;
        }
    }
}

void XHero::EndGame(const char* end_msg)
{
    // Static, so there is no `this` to write the tombstone for - it goes
    // through main_creature, whoever is standing in for the hero. In
    // "-demo" mode that is an ordinary creature (see SetMainCreature()),
    // and then there is no hero's game to end. Asked once here, so that
    // everything below is a hero by type rather than by assumption.
    XHero* hero = dynamic_cast<XHero*>(main_creature);

    if (!hero) {
        return;
    }

    unsigned long score = hero->experience + hero->MoneyOp(0);

    XGuiList list;

    const std::string title = fmt::format("{}, {} {} {} (L{}).",
        hero->name,
        hero->GetGenderStr(),
        hero->GetRaceStr(),
        hero->GetProfessionStr(),
        hero->level);

    list.AddItem(new XGuiItem_Text(title));

    list.AddItem(new XGuiItem_Text(fmt::format("You survived {} turns.", hero->turn_count)));

    // How the story ended, in one line.
    list.AddItem(new XGuiItem_Text(end_msg));

    int place_count = 0;

    for (const auto& [key, location] : Game.locations) {
        if (location && location->visited_by_hero) {
            place_count++;
        }
    }

    score += place_count * 200;
    list.AddItem(new XGuiItem_Text(fmt::format("You visited {} places.", place_count)));

    // Standing with each god a world declares, counted only where it
    // amounts to something - which is to say where the god has become
    // willing to do anything for them.
    int flag = 1;

    for (const auto& row : deities_db) {
        const DeityRank* rank = hero->religion.GetRank(row.id);

        if (!rank || hero->religion.AvailableHelp(row.id).empty()) {
            continue;
        }

        list.AddItem(new XGuiItem_Text(fmt::format("You were a {} of {}",
            rank->name, row.name)));

        score += rank->score;
        flag = 0;
    }

    if (flag) {
        list.AddItem(new XGuiItem_Text("You were not very religious."));
    }

    // Quests are defined world/quests.lua, with all messages and the score.
    // COMPLETE means the deed was done, CLOSED that it was done and reported;
    // a quest that has nothing to say about one of those leaves that string
    // empty, and the score is paid either way so that never going back to be
    // thanked does not cost the credit.
    for (const auto& quest: XQuest::quest.quests) {
        if (quest->status != XQuest::COMPLETE && quest->status != XQuest::CLOSED) {
            continue;
        }

        const std::string& line = quest->status == XQuest::CLOSED
            ? quest->closed
            : quest->complete;

        if (!line.empty()) {
            list.AddItem(new XGuiItem_Text(line));
        }

        score += quest->score;
    }

    list.AddItem(new XGuiItem_Text(fmt::format("You scored {}.", score)));
    list.SetCaption("<DECORATION>###<TEXT> Achievements <DECORATION>###");
    list.Run();

    vGotoXY(0, 0);
    msgwin.ClrMsg();
    msgwin.Add("Create Memory File? (Y/<KEY>N<TEXT>)");

    vRefresh();

    if (const int tch = vGetch(); tch == 'y' || tch == 'Y') {
        msgwin.ClrMsg();
        msgwin.Add("### Screenshot ###");

        // Not name.append(): that would rename the hero as a side effect.
        const std::string filename = hero->name + ".mem";
        std::ofstream file(vMakePath(HOME_DIR, filename));
        DumpVBuffer(file);
        list.Put(file);
        file << "\n";
        hero->Equipment(file);
        file << "\n";
        hero->WarSkillsList(file);
        file << "\n";
        hero->SkillsList(SKF_LIST_SKILL, 0, file);
        file << "\n";
        hero->XCast(file);
        file << "\n";
        hero->ShowResistance(file);
        file << "\n";

        for (const auto& item : hero->contain) {
            item->Identify();
        }

        hero->Inventory(
            &hero->contain, ItemKind::ALL, IF_NONE, 0, nullptr, file);

        vClrScr();
    }

    const std::shared_ptr<XHiScoreItem> hii(new XHiScoreItem(0, score, title, end_msg, XQuest::quest.hero_win, 1));

    XHiScore hiscore;
    hiscore.AddRecord(hii);
    hiscore.Show();
}
