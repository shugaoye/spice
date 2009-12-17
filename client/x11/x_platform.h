/*
   Copyright (C) 2009 Red Hat, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _H_XPLATFORM
#define _H_XPLATFORM

class XPlatform {
public:
    static Display* get_display();
    static XVisualInfo** get_vinfo();
    static GLXFBConfig** get_fbconfig();

    typedef void (*win_proc_t)(XEvent& event);
    static void set_win_proc(Window win, win_proc_t proc);
    static void cleare_win_proc(Window win);

    static void on_focus_in();
    static void on_focus_out();

    static bool is_x_shm_avail();
};

#endif

