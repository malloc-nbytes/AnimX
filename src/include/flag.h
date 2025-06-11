#ifndef FLAG_H
#define FLAG_H

#define FLAG_1HY_HELP 'h'

#define FLAG_2HY_HELP "help"
#define FLAG_2HY_WP "wp"
#define FLAG_2HY_MON "mon"
#define FLAG_2HY_MODE "mode"
#define FLAG_2HY_MAXMEM "maxmem"

typedef enum {
        FT_MAXMEM = 1 << 0,
} Flag_Type;

#endif // FLAG_H
