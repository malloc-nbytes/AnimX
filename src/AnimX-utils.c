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

#include "AnimX-utils.h"

#include <ctype.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

int str_isdigit(const char *s) {
        if (*s == '-') ++s;
        int period = 0;
        for (size_t i = 0; s[i]; ++i) {
                if (s[i] == '.') {
                        if (period) return 0;
                        period = 1;
                }
                else if (!isdigit(s[i])) return 0;
        }
        return 1;
}

char *resolve(const char *fp) {
        syslog(LOG_INFO, "resolve() got filepath: %s\n", fp);
        static char rp[PATH_MAX] = {0};
        memset(rp, 0, sizeof(rp)/sizeof(*rp));
        if (!realpath(fp, rp)) {
                syslog(LOG_ERR, "Failed to get realpath: %s\n", strerror(errno));
                perror("realpath");
        }
        return rp;
}
