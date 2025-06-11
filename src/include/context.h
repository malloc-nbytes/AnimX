#ifndef CONTEXT_H
#define CONTEXT_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <X11/extensions/Xrandr.h>

typedef struct {
        Display *display;
        int screen;
        Window root;
        Visual *visual;
        int depth;
        XRRScreenResources *screen_res;
        XRROutputInfo **output_infos; // Array for multiple monitors
        XRRCrtcInfo **crtc_infos; // Array for multiple monitors
        int num_monitors; // Number of monitors when index is -1
        long monitor_x, monitor_y, monitor_width, monitor_height;
        Pixmap root_pixmap;
        GC root_gc;
        Atom xrootpmap_id, esetroot_pmap_id;
        AVFormatContext *fmt_ctx;
        int video_stream_idx;
        AVCodecContext *codec_ctx;
        AVCodecParameters *codec_par;
        struct SwsContext *sws_ctx;
        AVFrame *frame, *bgra_frame;
        AVPacket *packet;
        uint8_t *bgra_buffer;
        int bgra_size;
        double frame_interval;
        double video_time_base;
        int64_t frame_duration;
} Context;

void cleanup_context(Context *ctx);
int init_context(Context *ctx, int monitor_index, const char *video_mp4);

#endif // CONTEXT_H
