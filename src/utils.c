#include "utils.h"

#include <ctype.h>
#include <stddef.h>

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
