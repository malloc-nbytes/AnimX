#ifndef GL_H
#define GL_H

typedef enum Mode_Type {
        MODE_LOAD = 0,
        MODE_STREAM,
} Mode_Type;

/* extern struct { */
/*         uint32_t flags; */
/*         char *wp; */
/*         int mon; */
/*         Mode_Type mode; */
/*         double maxmem; */
/* } g_config; */

#define FIFO_PATH "/tmp/AnimX.fifo"
#define LOG_PATH "/tmp/log/AnimX.log"
#define PID_PATH "/tmp/AnimX.pid"

#endif // GL_H
