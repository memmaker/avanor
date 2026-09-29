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

#include "creature/xhero.h"
#include "helpers/xgui.h"
#include "helpers/xstring.h"

#include <algorithm>
#include <string>

#include <fmt/format.h>

// The XGuiItem_Text::WideBuffer() function tries to widen the input string
// str to the given width. If it is impossible (size already is equal or
// greater than requested), the function simply exits and does nothing.
void XGuiItem_Text::WideBuffer(std::string& str, const std::string::size_type new_width) {
    // Determine the length of the string, excluding color escape sequences
    std::string::difference_type num_escapes = std::count(str.begin(), str.end(), 0x1F);
    std::string::size_type len = str.size() - (num_escapes * 2);

    // Find spaces (these may consist of one or more ' ' characters)
    std::vector<std::string::size_type> spaces;

    for(std::string::size_type i = 0; i < str.size(); ++i) {
        if (str[i] == ' ') {
            spaces.push_back(i);

            while (str[i]== ' ') {
                ++i;
            }

            if (i == str.size()) {
               break;
            }
        }
    }

    if (len >= new_width || spaces.empty()) {
        return;
    }

    std::string::size_type missing_spaces = new_width - len;
    std::string::size_type spaces_per_space = std::max(static_cast<std::string::size_type>(1), missing_spaces / spaces.size());

    while (!spaces.empty() && missing_spaces > 0) {
        const auto position = spaces.back();
        str.insert(position, spaces_per_space, ' ');

        spaces.pop_back();
        missing_spaces -= spaces_per_space;
    }
}

bool XGuiItem_Text::SetWidth(const std::string::size_type new_width) {
    // The escape in force, re-emitted at the head of every wrapped line
    // so a colour survives the break: two bytes for a role or a palette
    // slot, seven for a colour given outright.
    std::string colour = {0x1F, static_cast<char>(ROLE_TEXT)};
    std::size_t start = 0;

    Clear();

    while (true) {
        bool is_last_line = false, is_line_break = false;

        if (start == text.size()) {
            break;
        }

        std::size_t last_space = 0;
        std::size_t size = 0; // count of characters, including color codes
        std::size_t len = 0;  // count of visible characters

        while (len <= new_width) {
            if (text[size + start] == 0x1F) {
                size += 2;
                continue;
            }

            if ((size + start) == text.size()) {
                is_last_line = true;
                last_space = size + start;
                break;
            }

            if (text[size + start] == '\n') {
                is_line_break = true;
                last_space = size + start;
                break;
            }

            if (text[size + start] == ' ') {
                last_space = size + start;
            }

            size++;
            len++;
        }

        if (last_space == 0) {
            return false;
        }

        // handle trailing spaces
        while (last_space >= 2 && text[last_space - 1] == ' ') {
            last_space--;
        }

        std::string tmp;
        tmp.reserve(new_width + 2);

        if (text[start] != 0x1F && text[start] != RGB_ESCAPE) {
            tmp = colour;
        }

        tmp += text.substr(start, last_space - start);

        // memorize active color
        for(std::string::size_type i = 0; i < tmp.size(); ++i) {
            if (tmp[i] == 0x1F && i + 1 < tmp.size()) {
                colour = tmp.substr(i, 2);
            } else if (tmp[i] == RGB_ESCAPE && i + RGB_ESCAPE_LENGTH <= tmp.size()) {
                colour = tmp.substr(i, RGB_ESCAPE_LENGTH);
            }
        }

        // justify line
        if (!is_last_line && !is_line_break) {
            WideBuffer(tmp, new_width);
        }

        lines.emplace_back(tmp);

        // move forward string reference
        start = last_space;

        // step over whitespace
        if (is_line_break && text[start] == '\n') {
            start++;
        } else while (std::isspace(text[start])) {
            start++;
        }
    }

    this->width = static_cast<int>(new_width);

    return true;
}

void XGuiList::Put(const std::optional<std::reference_wrapper<std::ofstream>> file)
{
    vClrScr();

    if (!caption.empty()) {
        const int dx = size_x / 2 - x_strlen(caption.c_str()) / 2;

        if (!file) {
            vGotoXY(dx, 0);
            vPutS(caption);
        } else {
            auto line = fmt::format("{}{}\n\n", std::string(dx, ' '), caption);
            vFPutS(file.value(), line);
        }
    }

    XGuiItem* item = top_item;
    std::size_t item_first_line = top_item_first_line;
    std::size_t item_lines_count = top_item_lines_count;
    std::size_t count = list_height;

    if (file) {
        count = 100000;
    }

    int y_pos = 2;

    if (top_line + count >= lines_count) {
        count = lines_count - top_line;
    }

    std::size_t cur_line = top_line;
    int i = 0;
    selectable_items_count = 0;

    while (count > 0) {
        if (cur_line < item_first_line + item_lines_count) {

            if (cur_line == item_first_line) {
                vSetAttr(ResolveColour(ROLE_TEXT));
            }

            if (cur_line == item_first_line && item->isSelectable()) {
                if (file) {
                    vFPutS(file.value(), " ");
                } else {
                    // The letter that picks this line: [A], [B], [C]...
                    // Counted per screenful, not per list. SelectorChar()
                    // decides it and SelectorIndex() reads it back, so the
                    // two stay in step wherever ASCII takes them.
                    const std::string selector =
                        fmt::format("<DECORATION>[<SELECTOR>{}<DECORATION>]",
                                    SelectorChar(i++));

                    vGotoXY(0, y_pos);
                    vPutS(selector);
                    if (cursor_on && i - 1 == cursor) {
                        vPutCh(3, y_pos, '>', ResolveColour(ROLE_KEY));
                    }
                    selectable_items_count++;
                }
            }

            if (file) {
                vFPutS(file.value(), (*item)[cur_line++ - item_first_line]);
                vFPutS(file.value(), "\n");
            } else {
                vGotoXY(4, y_pos);
                vPutS((*item)[cur_line++ - item_first_line]);
            }

            y_pos++;
            count--;
        } else {
            item_first_line += item_lines_count;
            item = item->next;
            item_lines_count = item->GetHeight();
        }
    }

    if (!file) {
        const char* tprompt = "<TEXT>Use <DECORATION>[<KEY>/*-+<DECORATION>]<TEXT>to scroll up/down, <DECORATION>[<KEY>ESC<TEXT>,<KEY>Z<DECORATION>]<TEXT> to exit.";
        vGotoXY(size_x / 2 - x_strlen(tprompt) / 2, size_y - 1);
        vPutS(tprompt);

        if (top_line > 0) {
            vGotoXY(size_x - 6, 1);
            vPutS("<TEXT>(<KEY>more<TEXT>)");
        }

        if (top_line + list_height < lines_count) {
            vGotoXY(size_x - 6, size_y - 3);
            vPutS("<TEXT>(<KEY>more<TEXT>)");
        }
    }

    if (!footer.empty() && !file) {
        int dx = size_x / 2 - x_strlen(footer.c_str()) / 2;
        vGotoXY(dx, size_y - 2);
        vPutS(footer);
    }

    vRefresh();
}

void XGuiList::LineUp(size_t count)
{
    while (count > 0) {
        if (top_line <= 0) {
            return;
        }

        if (top_item_first_line < top_line) {
            top_line--;
            count--;
            continue;
        }

        top_item = top_item->prev;

        if (top_item->isSelectable()) {
            top_selectable_index--;
        }

        top_item_lines_count = top_item->GetHeight();
        top_item_first_line -= top_item_lines_count;
        top_item_index--;
    }
}

void XGuiList::LineDown(size_t count)
{
    while (count > 0) {
        if (top_line + list_height >= lines_count) {
            return;
        }

        if (top_line + 1 < top_item_first_line + top_item_lines_count) {
            top_line++;
            count--;
            continue;
        }

        if (top_item->isSelectable()) {
            top_selectable_index++;
        }

        top_item = top_item->next;

        top_item_first_line += top_item_lines_count;

        top_item_lines_count = top_item->GetHeight();
        top_item_index++;
    }
}

void XGuiList::PageUp()
{
    LineUp(list_height);
}

void XGuiList::PageDown()
{
    LineDown(list_height);
}

void XGuiList::Relayout()
{
    list_height = size_y - 5;
    list_width = size_x - 6;

    XGuiItem* item = head;
    lines_count = 0;

    while (item != nullptr) {
        item->SetWidth(list_width);
        lines_count += item->GetHeight();
        item = item->next;
    }

    // Rewrapping moves every line, so the view goes back to the top
    // rather than to a line that may no longer be where it was.
    top_item = head;
    top_item_first_line = 0;
    top_item_lines_count = top_item ? top_item->GetHeight() : 0;
    top_line = 0;
    top_item_index = 0;
    top_selectable_index = 0;
}

// Z is how a list is closed, so no line is ever labelled with it: the
// letters run A, B, ... Y and then straight past Z into the punctuation
// that follows it in ASCII, which is where a screenful of more than
// twenty-five selectable lines ended up anyway. Labelling one [Z] made it
// unselectable - the key closed the list instead, the caller was told
// nothing had been picked, and in the give command that looked like the
// item doing nothing at all.
char XGuiList::SelectorChar(const int index)
{
    const char c = static_cast<char>('A' + index);

    return c >= 'Z' ? static_cast<char>(c + 1) : c;
}

// The inverse: which line that key picks, or -1 for a key that picks none.
// Upper and lower case both choose, as they always have.
int XGuiList::SelectorIndex(const int ch, const int count)
{
    // Neither case of the key that closes the list is ever a selector.
    if (ch == 'Z' || ch == 'z') {
        return -1;
    }

    int index;

    if (ch >= 'A' && ch < 'a') {
        index = ch - 'A' - (ch > 'Z' ? 1 : 0);
    } else if (ch >= 'a') {
        index = ch - 'a' - (ch > 'z' ? 1 : 0);
    } else {
        return -1;
    }

    return (index >= 0 && index < count) ? index : -1;
}

int XGuiList::Run(int flag, int flag2)
{
    V_BUFFER xyzbuf;
    vStore(&xyzbuf);
    vClrScr();
    vHideCursor();

    Relayout();

    if (flag2) {
        LineDown(flag2);
    }

    while (true) {
        Put();
        if (cursor_on && cursor >= selectable_items_count && selectable_items_count > 0) {
            cursor = selectable_items_count - 1;
            Put();
        }
        int ch = vGetch();
        last_pressed_key = ch;

        // RVIP: cursor keys (arrows, numpad 8/2), 5/Enter/+/-/* pick the
        // cursor line (the caller reads the key), Ctrl+letter picks that
        // line, 0/. close, 4/6 return to the caller.
        if (cursor_on) {
            int pick = -1;

            if (ch == KEY_UP || ch == '8') {
                if (cursor > 0) cursor--; else LineUp();
                continue;
            }
            if (ch == KEY_DOWN || ch == '2') {
                if (cursor + 1 < selectable_items_count) cursor++; else LineDown();
                continue;
            }
            if (ch == KEY_ENTER || ch == '\n' || ch == '5' || ch == '+' || ch == '-' || ch == '*') {
                pick = cursor;
            } else if (ch >= 1 && ch <= 26 && ch != 9) {
                pick = SelectorIndex('a' + ch - 1, selectable_items_count);
            } else if (ch == '0' || ch == '.') {
                last_pressed_key = KEY_ESC;
                vRestore(&xyzbuf);
                vRefresh();
                return -1;
            } else if (ch == '4' || ch == '6') {
                vRestore(&xyzbuf);
                return -1;
            }

            if (pick >= 0 && pick < selectable_items_count) {
                vRestore(&xyzbuf);
                return top_selectable_index + pick;
            }
        }

        if (const int picked = SelectorIndex(ch, selectable_items_count); picked >= 0) {
            vRestore(&xyzbuf);

            if (!flag) {
                vRefresh();
            }

            return top_selectable_index + picked;
        }

        switch (ch) {
            case KEY_RESIZE:
                // The list was measured for the old screen: how many
                // lines fit, and how wide an item may be before it wraps.
                // Both have to be worked out again, and the items rewrapped
                // to match, before Put() draws at the top of the next turn
                // round this loop.
                Relayout();
                break;

            case KEY_ESC :
            case 'Z' :
            case 'z' :
            case ' ' :
                vRestore(&xyzbuf);
                vRefresh();
                return -1;

            case '*' :
            case '+' :
            case KEY_DOWN :
                LineDown();
                continue;

            case '/' :
            case '-' :
            case KEY_UP :
                LineUp();
                continue;

            case KEY_PGUP :
                PageUp();
                continue;

            case KEY_PGDOWN :
                PageDown();
                continue;

            default:
                break;
        }

        // if flag then we need to return
        if (flag) {
            vRestore(&xyzbuf);
            return -1;
        }

    }
}

// RVIP: floating menu box, sized to its content (longest line x lines +
// border, one space padding), scrolled when taller than the screen.
int XBoxMenu(const std::string_view title, const std::vector<XBoxEntry>& entries)
{
    auto keyname = [](int k) {
        if (k >= 1 && k <= 26) return fmt::format("^{}", static_cast<char>('A' + k - 1));
        return std::string(1, static_cast<char>(k));
    };
    std::vector<std::string> lines;
    int w = static_cast<int>(title.size());
    for (const auto& e : entries) {
        std::string l = e.key ? fmt::format("{:>2} {}", keyname(e.key), e.text) : e.text;
        w = std::max(w, static_cast<int>(l.size()));
        lines.push_back(std::move(l));
    }
    int cur = 0;
    while (cur < static_cast<int>(entries.size()) && !entries[cur].key) cur++;
    if (cur == static_cast<int>(entries.size())) return 0;

    const int n = static_cast<int>(lines.size());
    const int bw = std::min(w + 4, size_x);
    const int rows = std::min(n, size_y - 2);
    const int x0 = (size_x - bw) / 2;
    const int y0 = (size_y - rows - 2) / 2;
    int top = 0;

    V_BUFFER buf;
    vStore(&buf);
    vHideCursor();
    auto step = [&](int d) {
        int c = cur;
        do { c += d; } while (c >= 0 && c < n && !entries[c].key);
        if (c >= 0 && c < n) cur = c;
    };

    int result = 0;
    while (true) {
        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;
        if (top > 0 && !entries[top - 1].key && cur == top) top--; // show its header
        const unsigned deco = ResolveColour(ROLE_DECORATION);
        const std::string edge = "+" + std::string(bw - 2, '-') + "+";
        for (int i = 0; i < bw; i++) {
            vPutCh(x0 + i, y0, edge[i], deco);
            vPutCh(x0 + i, y0 + rows + 1, edge[i], deco);
        }
        for (int i = 0; i < static_cast<int>(title.size()) && i < bw - 4; i++) {
            vPutCh(x0 + 2 + i, y0, title[i], ResolveColour(ROLE_VALUE));
        }
        for (int r = 0; r < rows; r++) {
            const int k = top + r;
            vPutCh(x0, y0 + 1 + r, '|', deco);
            vPutCh(x0 + bw - 1, y0 + 1 + r, '|', deco);
            const bool sel = k == cur;
            const unsigned col = !entries[k].key ? ResolveColour(ROLE_VALUE)
                : sel ? ResolveColour(ROLE_KEY) : ResolveColour(ROLE_TEXT);
            vPutCh(x0 + 1, y0 + 1 + r, sel ? '>' : ' ', col);
            for (int i = 0; i < bw - 3; i++) {
                const char c = i < static_cast<int>(lines[k].size()) ? lines[k][i] : ' ';
                vPutCh(x0 + 2 + i, y0 + 1 + r, c, col);
            }
        }
        if (top > 0) vPutCh(x0 + bw - 1, y0 + 1, '^', deco);
        if (top + rows < n) vPutCh(x0 + bw - 1, y0 + rows, 'v', deco);
        vRefresh();

        const int ch = vGetch();
        if (ch == KEY_UP || ch == '8') { step(-1); continue; }
        if (ch == KEY_DOWN || ch == '2') { step(1); continue; }
        if (ch == KEY_PGUP) { for (int i = 0; i < rows; i++) step(-1); continue; }
        if (ch == KEY_PGDOWN) { for (int i = 0; i < rows; i++) step(1); continue; }
        if (ch == KEY_ENTER || ch == '\n' || ch == '5' || ch == '6' || ch == ' ') { result = entries[cur].key; break; }
        if (ch == KEY_ESC || ch == '0' || ch == '.' || ch == '4') break;
        bool hit = false;
        for (const auto& e : entries) {
            if (e.key && e.key == ch) { result = ch; hit = true; break; }
        }
        if (hit) break;
    }
    vRestore(&buf);
    vRefresh();
    return result;
}
