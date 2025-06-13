#ifndef UTILS_H
#define UTILS_H

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

#endif // UTILS_H
