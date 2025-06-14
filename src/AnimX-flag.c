#include <string.h>

#include "AnimX-flag.h"
#include "AnimX-utils.h"

static void fps_info(void) {
        printf("--help(%s):\n", FLAG_2HY_FPS);
        printf("    Set the FPS of the wallpaper. Setting it to a higher value will\n");
        printf("    drastically increase CPU usage. Meanwhile, setting it lower\n");
        printf("    can save a lot of resources. If this is unset, it will default to 30.\n\n");
        printf("    Example:\n");
        printf("        AnimX --fps=30\n");
        printf("        AnimX --fps=60\n");
        printf("        AnimX --fps=15\n");
}

static void help_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    Show the help menu or help on individual flags with `--help=<flag>|*`.\n\n");
        printf("    Example:\n");
        printf("        AnimX --help\n");
        printf("        AnimX -h\n");
        printf("        AnimX --help=version\n");
        printf("        AnimX -h=wp\n");
}

static void mon_info(void) {
        printf("--help(%s):\n", FLAG_2HY_MON);
        printf("    Set the monitor index. If left unset, it will mirror the wallpaper on all monitors.\n");
        printf("    You can also set this option to (-2) to do this.\n");
        printf("    If you want to stretch the wallpaper across all monitors, use the (-1) option.\n\n");
        printf("    Warning:\n");
        printf("        Using the (-1) options will significantly increase\n");
        printf("        memory usage. It is recommended to use --mode=stream\n");
        printf("        if you do not have a lot of RAM (or use --maxmem option).\n\n");
        printf("    Example:\n");
        printf("        AnimX --mon=1\n");
        printf("        AnimX --mon=2\n");
        printf("        AnimX --mon=-1 # combine all monitors into one monitor\n");
        printf("        AnimX --mon=-2 # mirror wallpaper\n");
}

static void mode_info(void) {
        printf("--help(%s):\n", FLAG_2HY_MODE);
        printf("    Set the mode of frame generation.\n");
        printf("    You can either set it to `stream` or `load`.\n");
        printf("    If this flag is not set, `stream` is used by default.\n\n");
        printf("    --mode=stream:\n");
        printf("        Generate frames on-the-fly and immediately display each one.\n");
        printf("        This allows for near-instant video loading, but it has a\n");
        printf("        significant impact on the CPU.\n\n");
        printf("    --mode=load:\n");
        printf("        Generate all frames up-front before displaying anything.\n");
        printf("        This significantly reduces the amount of work the CPU needs\n");
        printf("        to do, but with the tradeoff of big memory consumption.\n");
        printf("        If you have limited memory, it may be wise to use the\n");
        printf("        --maxmem option to ensure you do not run out.\n\n");
        printf("    Example:\n");
        printf("        AnimX --mode=stream\n");
        printf("        AnimX --mode=load\n");
}

static void maxmem_info(void) {
        printf("--help(%s):\n", FLAG_2HY_MAXMEM);
        printf("    Set the allowed maximum memory usage in GB as a float.\n");
        printf("    If the maximum memory usage has been hit, the program\n");
        printf("    will not exit, rather, it will stop frame generation\n");
        printf("    and just use those frames.\n\n");

        printf("Note:\n");
        printf("    This option does nothing when --mode=stream is used.\n\n");

        printf("    Example:\n");
        printf("        AnimX --maxmem=1.0\n");
        printf("        AnimX --maxmem=1\n");
        printf("        AnimX --maxmem=5.4\n");
        printf("        AnimX --maxmem=2.1234\n");
}

static void daemon_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_DAEMON, FLAG_2HY_DAEMON);
        printf("    Launch the deamon. If you do not provide any information\n");
        printf("    to AnimX when launching the daemon, it will wait until\n");
        printf("    you send a signal to it. Issue the `--stop` flag to stop it.\n\n");
        printf("    Note:\n");
        printf("        1. You can see logging information in `/var/log/syslog`.\n");
        printf("        2. A FIFO file and PID file are created in `/tmp/`.\n\n");
        printf("    Example:\n");
        printf("        AnimX -d                                     # starts the daemon, does nothing noticable\n");
        printf("        AnimX /home/user/vids/vid.mp4 --mon=1        # the daemon will use this information\n");
        printf("        AnimX --mode=load                            # the daemon will use this information\n");
        printf("        AnimX /home/user/vids/vid2.mp4               # the daemon will use this information\n");
        printf("        AnimX --stop                                 # kill daemon\n");
        printf("        AnimX -d /home/user/vids/vid.mp4 --mode=load # daemon will start with this information\n");
}

static void stop_info(void) {
        printf("--help(%s):\n", FLAG_2HY_STOP);
        printf("    Stop the daemon. If it is not running, this flag does nothing.\n");
        printf("    Example:\n");
        printf("        AnimX --deamon\n");
        printf("        AnimX --stop\n");
}

void dump_flag_info(const char *name) {
        if (*name == '-') {
                err_wargs("no known help infomation for `%s`, do not include hyphens `-`", name);
        }
        void (*infos[])(void) = {
                help_info,
                mon_info,
                mode_info,
                maxmem_info,
                daemon_info,
                stop_info,
                fps_info,
        };

#define OHYEQ(n, flag, actual) ((n) == 1 && (flag)[0] == (actual))
        size_t n = strlen(name);
        if (OHYEQ(n, name, FLAG_1HY_HELP) || !strcmp(name, FLAG_2HY_HELP)) {
                infos[0]();
        } else if (!strcmp(name, FLAG_2HY_MON)) {
                infos[1]();
        } else if (!strcmp(name, FLAG_2HY_MODE)) {
                infos[2]();
        } else if (!strcmp(name, FLAG_2HY_MAXMEM)) {
                infos[3]();
        } else if (OHYEQ(n, name, FLAG_1HY_DAEMON) || !strcmp(name, FLAG_2HY_DAEMON)) {
                infos[4]();
        } else if (!strcmp(name, FLAG_2HY_STOP)) {
                infos[5]();
        } else if (!strcmp(name, FLAG_2HY_FPS)) {
                infos[6]();
        } else if (OHYEQ(n, name, '*')) {
                for (size_t i = 0; i < sizeof(infos)/sizeof(*infos); ++i) {
                        if (i != 0) putchar('\n');
                        infos[i]();
                }
        }
        else {
                err_wargs("no known help infomation for `%s`", name);
        }
#undef OHYEQ
}
