#ifndef UTILS_H
#define UTILS_H

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

#endif // UTILS_H
