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

#ifndef ANIMX_UTILS_H
#define ANIMX_UTILS_H

#include <stdio.h>
#include <stdlib.h>

#define err_wargs(msg, ...)                                             \
        do {                                                            \
                fprintf(stderr, "error: " msg "\n", __VA_ARGS__);       \
                exit(1);                                                \
        } while (0)

#define err(msg)                                \
        do {                                    \
                fprintf(stderr, msg "\n");      \
                exit(1);                        \
        } while (0)

int str_isdigit(const char *s);
char *resolve(const char *fp);

#endif // ANIMX_UTILS_H
