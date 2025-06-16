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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "AnimX-io.h"
#include "AnimX-gl.h"
#include "AnimX-utils.h"
#include "AnimX-flag.h"
#include "dyn_array.h"
#define CIO_IMPL
#include "cio.h"

static const char *get_config_fullpath(void) {
        const char *home = getenv("HOME");
        static char path[256] = {0};
        (void)memset(path, 0, sizeof(path)/sizeof(*path));
        (void)stpcpy(path, home);
        (void)strcat(path, "/");
        (void)strcat(path, ANIMX_CONFIG_NAME);
        return path;
}

static void parse_config_file(const char *path) {
        size_t ret_len = 0;
        char **lines = cio_file_to_lines(path, &ret_len);

        for (size_t i = 0; i < ret_len; ++i) {
                dyn_array(char, cmd);
                dyn_array(char, value);
                int eq = 0;

                char *s = lines[i];
                while (*s) {
                        if (*s == '/' && *(s+1) == '/') {
                                break;
                        }
                        if (*s == '\n') continue;
                        if (*s == '=') {
                                if (!eq) {
                                        // allows for '=' in value
                                        eq = 1;
                                        ++s;
                                        continue;
                                }
                        }

                        if (!eq) {
                                dyn_array_append(cmd, *s);
                        } else {
                                dyn_array_append(value, *s);
                        }
                        ++s;
                }

                dyn_array_append(cmd, 0);
                dyn_array_append(value, 0);

                if (!strcmp(cmd.data, "wp")) {
                        g_config.wp = strdup(value.data);
                } else if (!strcmp(cmd.data, "mon")) {
                        if (!str_isdigit(value.data)) {
                                err_wargs("parse_config_file(): --mon expects a number, not `%s`\n", value.data);
                        }
                        g_config.mon = atoi(value.data);
                } else if (!strcmp(cmd.data, "mode")) {
                        if (!strcmp(value.data, "load")) {
                                g_config.mode = 0;
                        } else if (!strcmp(value.data, "stream")) {
                                g_config.mode = 1;
                        } else {
                                fprintf(stderr, "parse_config_file(): --mode expects either `stream` or `load`, not `%s`\n", value.data);
                        }
                } else if (!strcmp(cmd.data, "maxmem")) {
                        if (!str_isdigit(value.data)) {
                                err_wargs("parse_config_file(): --maxmem expects a float, not `%s`\n", value.data);
                        }
                        g_config.maxmem = strtod(value.data, NULL);
                } else if (!strcmp(cmd.data, "fps")) {
                        if (!str_isdigit(value.data)) {
                                err_wargs("parse_config_file(): --fps expects a number, not `%s`\n", value.data);
                        }
                        g_config.fps = atoi(value.data);
                } else if (!strcmp(cmd.data, "daemon")) {
                        if (!strcmp(value.data, "true")) {
                                g_config.flags |= FT_DAEMON;
                        } else if (strcmp(value.data, "false") != 0) {
                                fprintf(stderr, "parse_config_file(): daemon requires true|false, not: %s\n", cmd.data);
                        }
                }
                if (cmd.len > 0) { dyn_array_free(cmd); }
                if (value.len > 0) { dyn_array_free(value); }
        }
}

void read_config_file(void) {
        const char *path = get_config_fullpath();
        if (!cio_file_exists(path)) {
                cio_create_file(path, 1);
                return;
        }
        parse_config_file(path);
}

void write_config_file(void) {
        const char *path = get_config_fullpath();
        char buf[256] = {0};
        dyn_array(char, content);

        // comment
        {
                char comment[] = "// This is a generated file, changes here will not be saved!";
                for (size_t i = 0; comment[i]; ++i) dyn_array_append(content, comment[i]);
                dyn_array_append(content, '\n');
        }

        // wp
        {
                char cmd[] = "wp";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');

                for (size_t i = 0; g_config.wp[i]; ++i) {
                        dyn_array_append(content, g_config.wp[i]);
                } dyn_array_append(content, '\n');
        }

        // mon
        {
                char cmd[] = "mon";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');

                sprintf(buf, "%d", g_config.mon);
                for (size_t i = 0; buf[i]; ++i) {
                        dyn_array_append(content, buf[i]);
                } dyn_array_append(content, '\n');
                memset(buf, 0, sizeof(buf)/sizeof(*buf));
        }

        // mode
        {
                char cmd[256] = "mode";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');

                char mode[32] = {0};
                if (g_config.mode == 0) {
                        strcpy(mode, "load");
                } else if (g_config.mode == 1) {
                        strcpy(mode, "stream");
                }
                for (size_t i = 0; mode[i]; ++i) {
                        dyn_array_append(content, mode[i]);
                } dyn_array_append(content, '\n');
        }

        // maxmem
        {
                char cmd[256] = "maxmem";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');

                sprintf(buf, "%f", g_config.maxmem);
                for (size_t i = 0; buf[i]; ++i) {
                        dyn_array_append(content, buf[i]);
                } dyn_array_append(content, '\n');
                memset(buf, 0, sizeof(buf)/sizeof(*buf));
        }

        // fps
        {
                char cmd[256] = "fps";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');

                sprintf(buf, "%d", g_config.fps);
                for (size_t i = 0; buf[i]; ++i) {
                        dyn_array_append(content, buf[i]);
                } dyn_array_append(content, '\n');
                memset(buf, 0, sizeof(buf)/sizeof(*buf));
        }

        // daemon
        {
                char cmd[256] = "daemon";
                char true_[] = "true";
                char false_[] = "false";
                for (size_t i = 0; cmd[i]; ++i) dyn_array_append(content, cmd[i]);
                dyn_array_append(content, '=');
                int is_daemon = g_config.flags & FT_DAEMON;

                for (size_t i = 0; is_daemon ? true_[i] : false_[i]; ++i) {
                        dyn_array_append(content, is_daemon ? true_[i] : false_[i]);
                } // no more newlines
        }

        dyn_array_append(content, 0);

        if (!cio_write_file(path, content.data)) {
                fprintf(stderr, "failed to write to config: %s\n", strerror(errno));
        }
        dyn_array_free(content);
}
