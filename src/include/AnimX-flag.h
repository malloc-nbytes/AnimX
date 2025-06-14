/*
 * AnimX: Animated Wallpapers for X
 * Copyright (C) 2025  malloc-nbytes
 * Contact: zdhdev@yahoo.com

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#ifndef ANIMX_FLAG_H
#define ANIMX_FLAG_H

#define FLAG_1HY_HELP 'h'
#define FLAG_1HY_DAEMON 'd'

#define FLAG_2HY_HELP "help"
#define FLAG_2HY_MON "mon"
#define FLAG_2HY_MODE "mode"
#define FLAG_2HY_MAXMEM "maxmem"
#define FLAG_2HY_DAEMON "daemon"
#define FLAG_2HY_STOP "stop"
#define FLAG_2HY_FPS "fps"
#define FLAG_2HY_RESTORE "restore"

typedef enum {
        FT_MAXMEM = 1 << 0,
        FT_DAEMON = 1 << 1,
} Flag_Type;

void dump_flag_info(const char *name);

#endif // ANIMX_FLAG_H
