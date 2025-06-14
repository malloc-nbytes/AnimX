#ifndef ANIMX_GL_H
#define ANIMX_GL_H

#include <stdint.h>

extern struct {
        uint32_t flags;
        char *wp;
        int mon;
        int mode;
        double maxmem;
        int fps;
} g_config;

#endif // ANIMX_GL_H
