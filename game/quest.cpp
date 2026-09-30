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

#include <sol/sol.hpp>

#include "creature/xhero.h"
#include "game/quest.h"
#include "helpers/xgui.h"

void XQuest::RegisterLua(sol::state_view& lua)
{
    lua.new_enum("XQuest",
        "UNKNOWN", XQuest::UNKNOWN,
        "KNOWN", XQuest::KNOWN,
        "COMPLETE", XQuest::COMPLETE,
        "CLOSED", XQuest::CLOSED,
        "FAIL", XQuest::FAIL
    );

    lua.new_usertype<XQuest>("XQuestState",
        "GetFlag", &XQuest::GetFlag,
        "SetFlag", &XQuest::SetFlag,
        "WinGame", &XQuest::WinGame,
        "GetCreatureRef", &XQuest::GetCreatureRef,
        "SetCreatureRef", &XQuest::SetCreatureRef
    );

    lua["QuestState"] = &XQuest::quest;
}

XQuest XQuest::quest;

int XQuest::GetFlag(const std::string& name) const
{
    auto it = flags.find(name);
    return it != flags.end() ? it->second : 0;
}

void XQuest::SetFlag(const std::string& name, int value)
{
    flags[name] = value;
}

void XQuest::WinGame(const std::string& msg)
{
    hero_win = 1;
    XHero::EndGame(msg.c_str(), "win");
    XHero::EndGame(msg.c_str());
}

XCreature* XQuest::GetCreatureRef(const std::string& name) const
{
    auto it = creature_refs.find(name);
    return it != creature_refs.end() ? it->second.lock().get() : nullptr;
}

void XQuest::SetCreatureRef(const std::string& name, XCreature* cr)
{
    creature_refs[name] = XCreature::ToWeakPtr(cr);
}

// The log used to show none but the quests still being worked on, which
// left the ones already done but not yet reported nowhere at all: kill
// Ahk-Ulan and Gefeon's errand vanished from the list, though the reward
// was still sitting with Gefeon and nothing anywhere said so. A quest is
// in one of three states worth reading about, so the log says which.
//
// What each entry shows is the line the quest was given with, in every
// state: it names who asked and for what, which is exactly what someone
// wondering "where do I take this?" needs. The achievements screen is
// where a quest's own past tense belongs (XQuestRec::complete/closed).
//
// The list scrolls, so there is no need to be sparing with it: XGuiList
// draws "(more)" at the foot whenever it holds more lines than the window
// has room for, and the scroll keys work here as anywhere else.
void XQuest::ShowQuests()
{
    XGuiList list;

    list.SetCaption("<DECORATION>### <VALUE>Quests<DECORATION> ###");

    struct Section {
        XQuest::Id status;
        const char* heading;
    };

    // In this order on purpose: what is still to do first, what is owed to
    // you next, then what went wrong, and last of all the tally of what is
    // behind you - which is the part worth reading and the part that can
    // be left off the bottom of the screen without costing anything.
    static constexpr Section sections[] = {
        { XQuest::KNOWN,    "<VALUE>Open" },
        { XQuest::COMPLETE, "<QUALITY_GOOD>Finished - not yet reported" },
        { XQuest::FAIL,     "<QUALITY_POOR>Failed" },
        { XQuest::CLOSED,   "<QUALITY_GOOD>Completed" },
    };

    bool anything = false;

    for (const auto& section : sections) {
        bool headed = false;

        for (const auto& quest : quests) {
            if (quest->status != section.status) {
                continue;
            }

            if (!headed) {
                // A blank line between sections, but not above the first.
                if (anything) {
                    list.AddItem(new XGuiItem_Text(""));
                }

                list.AddItem(new XGuiItem_Text(section.heading));
                headed = true;
                anything = true;
            }

            // A quest still in play is named by what was asked of you,
            // which says who to go back to. One already behind you is
            // named by what you did, which is the line written for the
            // achievements screen and reads as a record rather than an
            // errand - "You killed the demon that preyed on the village"
            // rather than "the Elder asked you to kill the demon". Not
            // every quest has one, and those fall back to the asking.
            const std::string& said =
                (section.status == XQuest::CLOSED && !quest->closed.empty())
                    ? quest->closed
                    : quest->know;

            list.AddItem(new XGuiItem_Text("<TEXT>" + said));
        }
    }

    if (!anything) {
        list.AddItem(new XGuiItem_Text("You have no quests."));
    }

    list.Run();
}

XQuestRec* XQuest::Find(const std::string& id)
{
    for (const auto& it: XQuest::quest.quests) {
        if (it->quest_id == id) {
            return it.get();
        }
    }

    return nullptr;
}
