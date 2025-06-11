#include <string.h>

#include "flag.h"
#include "utils.h"

static void help_info(void) {
        printf("--help(%c, %s):\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    Show the help menu or help on individual flags with `--help=<flag>|*`.\n\n");
        printf("    Example:\n");
        printf("        awx --help\n");
        printf("        awx -h\n");
        printf("        awx --help=version\n");
        printf("        awx -h=wp\n");
}

static void mon_info(void) {
        printf("--help(%s):\n", FLAG_2HY_MON);
        printf("    Set the monitor index. If left unset, it will combine\n");
        printf("    all monitors available into one.\n");
        printf("    You can also set this option to (-1) to do this.\n\n");
        printf("    Warning:\n");
        printf("        Using the (-1) option will significantly increase\n");
        printf("        memory usage. It is recommended to use --mode=stream\n");
        printf("        if you do not have a lot of RAM (or use --maxmem option).\n\n");
        printf("    Example:\n");
        printf("        awx --mon=1\n");
        printf("        awx --mon=2\n");
        printf("        awx --mon=-1 # combine all monitors into one monitor\n");
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
        printf("        awx --mode=stream\n");
        printf("        awx --mode=load\n");
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
        printf("        awx --maxmem=1.0\n");
        printf("        awx --maxmem=1\n");
        printf("        awx --maxmem=5.4\n");
        printf("        awx --maxmem=2.1234\n");
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
        };

#define OHYEQ(n, flag, actual) ((n) == 1 && (flag)[0] == (actual))
        size_t n = strlen(name);
        if (OHYEQ(n, name, FLAG_1HY_HELP) || !strcmp(name, FLAG_2HY_HELP)) {
                infos[0]();
        } else if (!strcmp(name, FLAG_2HY_MON)) {
                infos[2]();
        } else if (!strcmp(name, FLAG_2HY_MODE)) {
                infos[3]();
        } else if (!strcmp(name, FLAG_2HY_MAXMEM)) {
                infos[4]();
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
