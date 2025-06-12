#ifndef FLAG_H
#define FLAG_H

#define FLAG_1HY_HELP 'h'
#define FLAG_1HY_DAEMON 'd'

#define FLAG_2HY_HELP "help"
#define FLAG_2HY_MON "mon"
#define FLAG_2HY_MODE "mode"
#define FLAG_2HY_MAXMEM "maxmem"
#define FLAG_2HY_DAEMON "damon"
#define FLAG_2HY_STOP "stop"

typedef enum {
        FT_MAXMEM = 1 << 0,
        FT_DAEMON = 1 << 1,
} Flag_Type;

void dump_flag_info(const char *name);

#endif // FLAG_H
