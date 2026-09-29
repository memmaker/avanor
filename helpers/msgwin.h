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

#ifndef MSGWIN_H_
#define MSGWIN_H_

#include <string>
#include <string_view>

#define MSGWIN_X 0
#define MSGWIN_Y 0
#define MSGWIN_H 2
#define MSGWIN_L 80

class XGuiList;

class XMsgWin
{
    private:
        int index_x;
        int index_y;
        std::string sent_buf;
        XGuiList* history_list;

    public:
        int count = 0; // RVIP: messages added so far (explore's new-message stop)
        XMsgWin();
        ~XMsgWin();

        void Add(std::string_view str);

        void ClrMsg();
        void ShowHistory() const;
};

extern XMsgWin msgwin;

#endif
