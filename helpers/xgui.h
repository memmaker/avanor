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

#ifndef XGUI_H
#define XGUI_H

#include <functional>
#include <string>
#include <optional>
#include <vector>

#include "engine/global.h"

class XGuiItem
{
    protected:
        XGuiItem* next;
        XGuiItem* prev;

        friend class XGuiList;

    public:
        XGuiItem() : next(nullptr), prev(nullptr) {}

        virtual ~XGuiItem() = default;

        virtual int isSelectable() = 0;

        virtual bool SetWidth(std::string::size_type /*new_width*/)
        {
            return false;
        }

        virtual size_t GetHeight()
        {
            return 0;
        }

        virtual const char* operator[](std::size_t /*index*/)
        {
            return nullptr;
        }
};

class XGuiItem_SimpleSelect final : public XGuiItem
{
        std::string str;
    public:
        explicit XGuiItem_SimpleSelect(std::string_view text)
        {
            str = text;
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

        const char* operator[](std::size_t /*index*/) override
        {
            return str.c_str();
        }
};


class XGuiItem_Text final : public XGuiItem
{
        std::string text;
        std::vector<std::string> lines;
        int width{};
        int select_flag;

        static void WideBuffer(std::string& str, std::string::size_type new_width);

    public:
        XGuiItem_Text() : select_flag(0)
        {
            SetText("");
        }

        explicit XGuiItem_Text(const char* _text, const int sf = 0) :select_flag(sf)
        {
            if (*_text == 0) {
                SetText("\n");
            } else {
                SetText(_text);
            }
        }

        explicit XGuiItem_Text(const std::string& _text, const int sf = 0) :select_flag(sf)
        {
            SetText(_text);
        }

        void SetText(const char* _text)
        {
            SetText(std::string(_text ? _text : ""));
        }

        void SetText(const std::string& _text)
        {
            // Roles become escape bytes here, before anything measures
            // or wraps this text.
            text = ExpandMarkup(_text);
            SetWidth(text.length());
        }

        ~XGuiItem_Text() override
        {
            Clear();
        }

        int isSelectable() override
        {
            return select_flag;
        }

        size_t GetHeight() override
        {
            return lines.size();
        }

        bool SetWidth(std::string::size_type new_width) override;

        const char* operator[](const std::size_t index) override
        {
            return lines[index].c_str();
        }

    protected:
        void Clear() {
            for (auto line = lines.begin(); line != lines.end();)
                line = lines.erase(line);
        }
};

class XGuiList final
{
        XGuiItem* head;
        XGuiItem* tail;

        XGuiItem* top_item{};
        std::size_t top_item_first_line{};
        std::size_t top_item_lines_count{};
        std::size_t top_line{};
        int top_item_index{};
        // Which key labels which line, and back again - see the comment
        // on SelectorChar() for why Z is never one of them.
        static char SelectorChar(int index);
        static int SelectorIndex(int ch, int count);

        int top_selectable_index;
        int selectable_items_count;

        std::size_t items_count;
        std::size_t lines_count;
        int width{};

        std::size_t list_height;
        std::size_t list_width;

        std::string caption;
        std::string footer;

        int last_pressed_key{};
        // RVIP: cursor over the selectable lines of the current screenful.
        bool cursor_on = false;
        int cursor = 0;

        // Measures the list against however much screen there is now.
        // Called once as Run() starts and again whenever the terminal
        // changes shape under it.
        void Relayout();

    public:
        XGuiList() :
            head(nullptr), tail(nullptr), top_selectable_index(0), selectable_items_count(0), items_count(0), lines_count(0)
        {
            list_height = size_y - 5;
            list_width = size_x - 6;
        }

        ~XGuiList()
        {
            while (head != nullptr) {
                const XGuiItem* tmp = head;
                head = head->next;
                delete tmp;
            }
        }

        void AddItemHead(XGuiItem * item)
        {
            if (head != nullptr) {
                item->next = head;
                head->prev = item;
                head = item;
            } else {
                head = item;
                tail = item;
            }

            top_item = item;
            top_item_first_line = 0;
            top_item_lines_count = item->GetHeight();
            top_line = 0;
            top_item_index = 0;
        }

        void AddItemTail(XGuiItem * item)
        {
            if (tail != nullptr) {
                tail->next = item;
                item->prev = tail;
                tail = item;
            } else {
                head = item;
                tail = item;
                top_item = item;
                top_item_first_line = 0;
                top_item_lines_count = item->GetHeight();
                top_line = 0;
                top_item_index = 0;
            }
        }

        void AddItem(XGuiItem * item, const int is_first = 0)
        {
            if (is_first) {
                AddItemHead(item);
            } else {
                AddItemTail(item);
            }

            items_count++;
            lines_count += item->GetHeight();
        }

        void EnableCursor() { cursor_on = true; }
        void AddHtmlText(std::string_view text);

        void Put(std::optional<std::reference_wrapper<std::ofstream>> file = std::nullopt);
        int Run(int flag = 0, int flag2 = 0);

        void LineUp(size_t count = 1);
        void LineDown(size_t count = 1);
        void PageUp();
        void PageDown();
        void SetCaption(std::string_view _caption)
        {
            caption = _caption;
        }

        void SetFooter(std::string_view _footer)
        {
            footer = _footer;
        }

        [[nodiscard]] size_t GetHeight() const
        {
            return lines_count;
        }

        [[nodiscard]] int GetTopItemIndex() const
        {
            return top_item_index;
        }

        [[nodiscard]] int GetLastKey() const
        {
            return last_pressed_key;
        }
};

// RVIP: floating menu box over the screen, sized to its entries. An entry
// with key 0 is a group header. Returns the chosen entry's key, 0 on
// Escape/0/./4 (back).
struct XBoxEntry { int key; std::string text; };
int XBoxMenu(std::string_view title, const std::vector<XBoxEntry>& entries);

#endif
