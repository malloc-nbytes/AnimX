#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
        uint8_t *data; // BGRA data
        int width, height; // Frame dimensions
        int size; // Size of data (width * height * 4 for BGRA)
} Image;

AVFormatContext *create_avformat_ctx(const char *video_fp);
int get_video_stream_index(AVFormatContext *fmt_ctx);
AVCodec *find_codec_decoder(AVFormatContext *fmt_ctx, int video_stream_idx, AVCodecContext **codec_ctx, AVCodecParameters **codec_par);

typedef struct {
        Display *display;
        int screen;
        Window root;
        Visual *visual;
        int depth;
        XRRScreenResources *screen_res;
        XRROutputInfo *output_info;
        XRRCrtcInfo *crtc_info;
        int monitor_x, monitor_y, monitor_width, monitor_height;
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

// Initialize common resources
int init_context(Context *ctx, int monitor_index, const char *video_mp4) {
        avformat_network_init();
        ctx->fmt_ctx = create_avformat_ctx(video_mp4);
        ctx->video_stream_idx = get_video_stream_index(ctx->fmt_ctx);
        find_codec_decoder(ctx->fmt_ctx, ctx->video_stream_idx, &ctx->codec_ctx, &ctx->codec_par);

        ctx->display = XOpenDisplay(NULL);
        if (!ctx->display) {
                fprintf(stderr, "Cannot open X display\n");
                return -1;
        }

        ctx->screen = DefaultScreen(ctx->display);
        ctx->root = RootWindow(ctx->display, ctx->screen);
        ctx->visual = DefaultVisual(ctx->display, ctx->screen);
        ctx->depth = DefaultDepth(ctx->display, ctx->screen);

        ctx->screen_res = XRRGetScreenResources(ctx->display, ctx->root);
        if (!ctx->screen_res) {
                fprintf(stderr, "Failed to get screen resources\n");
                return -1;
        }

        int num_monitors = ctx->screen_res->noutput;
        if (monitor_index >= num_monitors) {
                fprintf(stderr, "Monitor index %d out of range (0-%d)\n", monitor_index, num_monitors - 1);
                return -1;
        }

        ctx->output_info = XRRGetOutputInfo(ctx->display, ctx->screen_res, ctx->screen_res->outputs[monitor_index]);
        if (!ctx->output_info || ctx->output_info->connection != RR_Connected) {
                fprintf(stderr, "Monitor %d is not connected\n", monitor_index);
                return -1;
        }

        ctx->crtc_info = XRRGetCrtcInfo(ctx->display, ctx->screen_res, ctx->output_info->crtc);
        if (!ctx->crtc_info) {
                fprintf(stderr, "Failed to get CRTC info for monitor %d\n", monitor_index);
                return -1;
        }

        ctx->monitor_x = ctx->crtc_info->x;
        ctx->monitor_y = ctx->crtc_info->y;
        ctx->monitor_width = ctx->crtc_info->width;
        ctx->monitor_height = ctx->crtc_info->height;
        printf("Monitor %d: %dx%d at (%d,%d)\n", monitor_index, ctx->monitor_width, ctx->monitor_height, ctx->monitor_x, ctx->monitor_y);

        ctx->sws_ctx = sws_getContext(ctx->codec_ctx->width, ctx->codec_ctx->height, ctx->codec_ctx->pix_fmt,
                                      ctx->monitor_width, ctx->monitor_height, AV_PIX_FMT_BGRA,
                                      SWS_BILINEAR, NULL, NULL, NULL);
        if (!ctx->sws_ctx) {
                fprintf(stderr, "Could not initialize swscale context\n");
                return -1;
        }

        ctx->frame = av_frame_alloc();
        ctx->bgra_frame = av_frame_alloc();
        ctx->packet = av_packet_alloc();
        if (!ctx->frame || !ctx->bgra_frame || !ctx->packet) {
                fprintf(stderr, "Memory allocation failed\n");
                return -1;
        }

        ctx->bgra_size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, ctx->monitor_width, ctx->monitor_height, 1);
        ctx->bgra_buffer = av_malloc(ctx->bgra_size * sizeof(uint8_t));
        if (!ctx->bgra_buffer) {
                fprintf(stderr, "Failed to allocate BGRA buffer\n");
                return -1;
        }
        av_image_fill_arrays(ctx->bgra_frame->data, ctx->bgra_frame->linesize, ctx->bgra_buffer, AV_PIX_FMT_BGRA, ctx->monitor_width, ctx->monitor_height, 1);

        ctx->root_pixmap = XCreatePixmap(ctx->display, ctx->root, DisplayWidth(ctx->display, ctx->screen), DisplayHeight(ctx->display, ctx->screen), ctx->depth);
        ctx->root_gc = XCreateGC(ctx->display, ctx->root_pixmap, 0, NULL);
        if (!ctx->root_gc) {
                fprintf(stderr, "Failed to create root GC\n");
                return -1;
        }
        XFillRectangle(ctx->display, ctx->root_pixmap, ctx->root_gc, 0, 0, DisplayWidth(ctx->display, ctx->screen), DisplayHeight(ctx->display, ctx->screen));

        ctx->xrootpmap_id = XInternAtom(ctx->display, "_XROOTPMAP_ID", False);
        ctx->esetroot_pmap_id = XInternAtom(ctx->display, "ESETROOT_PMAP_ID", False);

        ctx->frame_interval = 1.0 / 30.0; // 30 FPS
        ctx->video_time_base = av_q2d(ctx->fmt_ctx->streams[ctx->video_stream_idx]->time_base);
        ctx->frame_duration = ctx->frame_interval / ctx->video_time_base;

        return 0;
}

// Clean up common resources
void cleanup_context(Context *ctx) {
        XFreeGC(ctx->display, ctx->root_gc);
        XFreePixmap(ctx->display, ctx->root_pixmap);
        av_free(ctx->bgra_buffer);
        av_frame_free(&ctx->frame);
        av_frame_free(&ctx->bgra_frame);
        av_packet_free(&ctx->packet);
        sws_freeContext(ctx->sws_ctx);
        XRRFreeCrtcInfo(ctx->crtc_info);
        XRRFreeOutputInfo(ctx->output_info);
        XRRFreeScreenResources(ctx->screen_res);
        XCloseDisplay(ctx->display);
        avcodec_free_context(&ctx->codec_ctx);
        avformat_close_input(&ctx->fmt_ctx);
}

// Display a single frame
int display_frame(Context *ctx, uint8_t *data, int width, int height, int frame_count) {
        uint8_t *ximage_buffer = malloc(ctx->bgra_size);
        if (!ximage_buffer) {
                fprintf(stderr, "Failed to allocate XImage buffer for frame %d\n", frame_count);
                return -1;
        }
        memcpy(ximage_buffer, data, ctx->bgra_size);

        XImage *ximage = XCreateImage(ctx->display, ctx->visual, ctx->depth, ZPixmap, 0, (char *)ximage_buffer,
                                      width, height, 32, width * 4);
        if (!ximage) {
                fprintf(stderr, "Failed to create XImage for frame %d\n", frame_count);
                free(ximage_buffer);
                return -1;
        }
        ximage->byte_order = ImageByteOrder(ctx->display);

        Pixmap pixmap = XCreatePixmap(ctx->display, ctx->root, width, height, ctx->depth);
        if (!pixmap) {
                fprintf(stderr, "Failed to create pixmap for frame %d\n", frame_count);
                XDestroyImage(ximage);
                return -1;
        }

        GC gc = XCreateGC(ctx->display, pixmap, 0, NULL);
        if (!gc) {
                fprintf(stderr, "Failed to create GC for frame %d\n", frame_count);
                XFreePixmap(ctx->display, pixmap);
                XDestroyImage(ximage);
                return -1;
        }

        if (XPutImage(ctx->display, pixmap, gc, ximage, 0, 0, 0, 0, width, height) != Success) {
                fprintf(stderr, "XPutImage failed for frame %d\n", frame_count);
                XFreeGC(ctx->display, gc);
                XFreePixmap(ctx->display, pixmap);
                XDestroyImage(ximage);
                return -1;
        }

        XCopyArea(ctx->display, pixmap, ctx->root_pixmap, ctx->root_gc, 0, 0, width, height, ctx->monitor_x, ctx->monitor_y);
        XSetWindowBackgroundPixmap(ctx->display, ctx->root, ctx->root_pixmap);
        XChangeProperty(ctx->display, ctx->root, ctx->xrootpmap_id, XA_PIXMAP, 32, PropModeReplace,
                        (unsigned char *)&ctx->root_pixmap, 1);
        XChangeProperty(ctx->display, ctx->root, ctx->esetroot_pmap_id, XA_PIXMAP, 32, PropModeReplace,
                        (unsigned char *)&ctx->root_pixmap, 1);
        XClearWindow(ctx->display, ctx->root);
        XFlush(ctx->display);

        XFreeGC(ctx->display, gc);
        XFreePixmap(ctx->display, pixmap);
        XDestroyImage(ximage); // Frees ximage_buffer
        return 0;
}

int run_load_all(int monitor_index, const char *video_mp4) {
        Context ctx = {0};
        if (init_context(&ctx, monitor_index, video_mp4) < 0) {
                cleanup_context(&ctx);
                return -1;
        }

        // Allocate array for frames (assuming max 1000 frames)
        Image *images = malloc(1000 * sizeof(Image));
        if (!images) {
                fprintf(stderr, "Failed to allocate image array\n");
                cleanup_context(&ctx);
                return -1;
        }
        int image_count = 0;
        int64_t next_pts = 0;

        // Load all frames
        while (av_read_frame(ctx.fmt_ctx, ctx.packet) >= 0) {
                if (ctx.packet->stream_index == ctx.video_stream_idx) {
                        if (avcodec_send_packet(ctx.codec_ctx, ctx.packet) >= 0) {
                                while (avcodec_receive_frame(ctx.codec_ctx, ctx.frame) >= 0) {
                                        if (ctx.frame->pts >= next_pts) {
                                                sws_scale(ctx.sws_ctx, (const uint8_t * const *)ctx.frame->data, ctx.frame->linesize, 0, ctx.codec_ctx->height,
                                                          ctx.bgra_frame->data, ctx.bgra_frame->linesize);
                                                Image *img = &images[image_count];
                                                img->width = ctx.monitor_width;
                                                img->height = ctx.monitor_height;
                                                img->size = ctx.bgra_size;
                                                img->data = malloc(ctx.bgra_size);
                                                if (!img->data) {
                                                        fprintf(stderr, "Failed to allocate image data\n");
                                                        break;
                                                }
                                                memcpy(img->data, ctx.bgra_buffer, ctx.bgra_size);
                                                image_count++;
                                                next_pts += ctx.frame_duration;
                                                printf("Stored frame %d\n", image_count);
                                        }
                                }
                        }
                }
                av_packet_unref(ctx.packet);
        }

        printf("Loaded %d frames at %dx%d (BGRA)\n", image_count, ctx.monitor_width, ctx.monitor_height);

        // Display loop
        int i = 0;
        while (1) {
                Image *img = &images[i];
                if (!img->data) {
                        fprintf(stderr, "Null image data for frame %d\n", i);
                        i = (i + 1) % image_count;
                        continue;
                }

                if (display_frame(&ctx, img->data, img->width, img->height, i) < 0) {
                        i = (i + 1) % image_count;
                        continue;
                }

                printf("Displayed frame %d\n", i + 1);
                //usleep(33333); // 30 FPS
                i = (i + 1) % image_count;
        }

        // Cleanup
        for (int i = 0; i < image_count; i++) {
                if (images[i].data) free(images[i].data);
        }
        free(images);
        cleanup_context(&ctx);
        return 0;
}

int run_stream(int monitor_index, const char *video_mp4) {
        Context ctx = {0};
        if (init_context(&ctx, monitor_index, video_mp4) < 0) {
                cleanup_context(&ctx);
                return -1;
        }

        int64_t next_pts = 0;
        int frame_count = 0;

        while (1) {
                int ret = av_read_frame(ctx.fmt_ctx, ctx.packet);
                if (ret < 0) {
                        av_packet_unref(ctx.packet);
                        avcodec_flush_buffers(ctx.codec_ctx);
                        if (avformat_seek_file(ctx.fmt_ctx, ctx.video_stream_idx, INT64_MIN, 0, INT64_MAX, 0) < 0) {
                                fprintf(stderr, "Failed to seek to start of video\n");
                                break;
                        }
                        next_pts = 0;
                        frame_count = 0;
                        printf("Restarting video loop\n");
                        continue;
                }

                if (ctx.packet->stream_index == ctx.video_stream_idx) {
                        if (avcodec_send_packet(ctx.codec_ctx, ctx.packet) >= 0) {
                                while (avcodec_receive_frame(ctx.codec_ctx, ctx.frame) >= 0) {
                                        if (ctx.frame->pts >= next_pts) {
                                                sws_scale(ctx.sws_ctx, (const uint8_t * const *)ctx.frame->data, ctx.frame->linesize, 0, ctx.codec_ctx->height,
                                                          ctx.bgra_frame->data, ctx.bgra_frame->linesize);

                                                if (display_frame(&ctx, ctx.bgra_buffer, ctx.monitor_width, ctx.monitor_height, frame_count) < 0) {
                                                        continue;
                                                }

                                                frame_count++;
                                                next_pts += ctx.frame_duration;
                                                printf("Displayed frame %d\n", frame_count);
                                                usleep(33333);
                                        }
                                }
                        }
                }
                av_packet_unref(ctx.packet);
        }

        cleanup_context(&ctx);
        return 0;
}

int main(int argc, char *argv[]) {
        if (argc < 4) {
                fprintf(stderr, "Usage: %s <input.mp4> <monitor_index> --mode=<load|stream>\n", argv[0]);
                return -1;
        }

        int monitor_index = atoi(argv[2]);
        if (monitor_index < 0) {
                fprintf(stderr, "Invalid monitor index\n");
                return -1;
        }

        // Parse mode
        char *mode_str = strstr(argv[3], "--mode=");
        if (!mode_str || strlen(mode_str) <= 7) {
                fprintf(stderr, "Mode not specified, defaulting to stream\n");
                return run_stream(monitor_index, argv[1]);
        }

        mode_str += 7; // Skip "--mode="
        if (strcmp(mode_str, "load") == 0) {
                printf("Running in load-all mode\n");
                return run_load_all(monitor_index, argv[1]);
        } else if (strcmp(mode_str, "stream") == 0) {
                printf("Running in stream mode\n");
                return run_stream(monitor_index, argv[1]);
        } else {
                fprintf(stderr, "Invalid mode '%s', defaulting to stream\n", mode_str);
                return run_stream(monitor_index, argv[1]);
        }

        return 0;
}

AVFormatContext *create_avformat_ctx(const char *video_fp) {
        AVFormatContext *fmt_ctx = NULL;
        if (avformat_open_input(&fmt_ctx, video_fp, NULL, NULL) < 0) {
                fprintf(stderr, "Could not open video file\n");
                exit(1);
        }
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
                fprintf(stderr, "Could not find stream info\n");
                avformat_close_input(&fmt_ctx);
                exit(1);
        }
        return fmt_ctx;
}

int get_video_stream_index(AVFormatContext *fmt_ctx) {
        int video_stream_idx = -1;
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
                if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                        video_stream_idx = i;
                        break;
                }
        }
        if (video_stream_idx == -1) {
                fprintf(stderr, "No video stream found\n");
                avformat_close_input(&fmt_ctx);
                exit(1);
        }
        return video_stream_idx;
}

AVCodec *find_codec_decoder(
        AVFormatContext *fmt_ctx,
        int video_stream_idx,
        AVCodecContext **codec_ctx,
        AVCodecParameters **codec_par
) {
        *codec_par = fmt_ctx->streams[video_stream_idx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder((*codec_par)->codec_id);
        if (!codec) {
                fprintf(stderr, "Decoder not found\n");
                avformat_close_input(&fmt_ctx);
                exit(1);
        }
        *codec_ctx = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(*codec_ctx, *codec_par) < 0) {
                fprintf(stderr, "Failed to copy codec parameters\n");
                avformat_close_input(&fmt_ctx);
                exit(1);
        }
        if (avcodec_open2(*codec_ctx, codec, NULL) < 0) {
                fprintf(stderr, "Could not open codec\n");
                avcodec_free_context(codec_ctx);
                avformat_close_input(&fmt_ctx);
                exit(1);
        }
        return (AVCodec *)codec;
}
