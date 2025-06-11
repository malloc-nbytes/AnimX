#include "utils.h"

#include <ctype.h>
#include <stddef.h>

int str_isdigit(const char *s) {
        if (*s == '-') ++s;
        for (size_t i = 0; s[i]; ++i) {
                if (!isdigit(s[i])) return 0;
        }
        return 1;
}
