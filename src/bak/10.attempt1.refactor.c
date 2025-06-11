#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h> // Added for atoms
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
        uint8_t *data; // BGRA data
        int width, height; // Frame dimensions
        int size; // Size of data (width * height * 4 for BGRA)
} Image;

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

Image get_next_img(AVPacket *packet, int video_stream_idx, AVCodecContext *codec_ctx, AVFrame *frame, int64_t *next_pts, struct SwsContext *sws_ctx, AVFrame *bgra_frame, int frame_duration, int monitor_width, int monitor_height, int bgra_size, uint8_t **bgra_buffer) {
        Image img = {0};
        if (packet->stream_index == video_stream_idx) {
                if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                                if (frame->pts >= *next_pts) {
                                        sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, codec_ctx->height,
                                                  bgra_frame->data, bgra_frame->linesize);
                                        img.width = monitor_width;
                                        img.height = monitor_height;
                                        img.size = bgra_size;
                                        img.data = malloc(bgra_size);
                                        if (!img.data) {
                                                fprintf(stderr, "Failed to allocate image data\n");
                                                break;
                                        }
                                        memcpy(img.data, *bgra_buffer, bgra_size);
                                        *next_pts += frame_duration;
                                        printf("Image from frame created: %p\n", &img);
                                }
                        }
                }
        }
        av_packet_unref(packet);
        return img;
}

int run(int monitor_index, const char *video_mp4) {
        avformat_network_init();
        AVFormatContext *fmt_ctx = create_avformat_ctx(video_mp4);
        int video_stream_idx = get_video_stream_index(fmt_ctx);
        AVCodecContext *codec_ctx = NULL;
        AVCodecParameters *codec_par = NULL;
        find_codec_decoder(fmt_ctx, video_stream_idx, &codec_ctx, &codec_par);

        Display *display = XOpenDisplay(NULL);
        if (!display) {
                fprintf(stderr, "Cannot open X display\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        Visual *visual = DefaultVisual(display, screen);
        int depth = DefaultDepth(display, screen);

        XRRScreenResources *screen_res = XRRGetScreenResources(display, root);
        if (!screen_res) {
                fprintf(stderr, "Failed to get screen resources\n");
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        int num_monitors = screen_res->noutput;
        if (monitor_index >= num_monitors) {
                fprintf(stderr, "Monitor index %d out of range (0-%d)\n", monitor_index, num_monitors - 1);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        XRROutputInfo *output_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[monitor_index]);
        if (!output_info || output_info->connection != RR_Connected) {
                fprintf(stderr, "Monitor %d is not connected\n", monitor_index);
                if (output_info) XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_res, output_info->crtc);
        if (!crtc_info) {
                fprintf(stderr, "Failed to get CRTC info for monitor %d\n", monitor_index);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        int monitor_x = crtc_info->x;
        int monitor_y = crtc_info->y;
        int monitor_width = crtc_info->width;
        int monitor_height = crtc_info->height;
        printf("Monitor %d: %dx%d at (%d,%d)\n", monitor_index, monitor_width, monitor_height, monitor_x, monitor_y);

        struct SwsContext *sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                    monitor_width, monitor_height, AV_PIX_FMT_BGRA,
                                                    SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) {
                fprintf(stderr, "Could not initialize swscale context\n");
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        AVFrame *frame = av_frame_alloc();
        AVFrame *bgra_frame = av_frame_alloc();
        AVPacket *packet = av_packet_alloc();
        if (!frame || !bgra_frame || !packet) {
                fprintf(stderr, "Memory allocation failed\n");
                av_frame_free(&frame);
                av_frame_free(&bgra_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        int bgra_size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, monitor_width, monitor_height, 1);
        uint8_t *bgra_buffer = av_malloc(bgra_size * sizeof(uint8_t));
        if (!bgra_buffer) {
                fprintf(stderr, "Failed to allocate BGRA buffer\n");
                av_frame_free(&frame);
                av_frame_free(&bgra_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }
        av_image_fill_arrays(bgra_frame->data, bgra_frame->linesize, bgra_buffer, AV_PIX_FMT_BGRA, monitor_width, monitor_height, 1);

        Image *images = malloc(1000 * sizeof(Image));
        int image_count = 0;

        double frame_interval = 1.0 / 30.0;
        double video_time_base = av_q2d(fmt_ctx->streams[video_stream_idx]->time_base);
        int64_t frame_duration = frame_interval / video_time_base;
        int64_t next_pts = 0;

        if (visual->class != TrueColor) {
                fprintf(stderr, "Unsupported visual: not TrueColor\n");
                for (int i = 0; i < image_count; i++) free(images[i].data);
                free(images);
                av_free(bgra_buffer);
                av_frame_free(&frame);
                av_frame_free(&bgra_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        Pixmap root_pixmap = XCreatePixmap(display, root, DisplayWidth(display, screen), DisplayHeight(display, screen), depth);
        GC root_gc = XCreateGC(display, root_pixmap, 0, NULL);
        XFillRectangle(display, root_pixmap, root_gc, 0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen));

        // Get atoms for root pixmap ID
        Atom xrootpmap_id = XInternAtom(display, "_XROOTPMAP_ID", False);
        Atom esetroot_pmap_id = XInternAtom(display, "ESETROOT_PMAP_ID", False);

        int i = 0;
        while (1) {
                Image img = {0};
                if (av_read_frame(fmt_ctx, packet) >= 0) {
                        img = get_next_img(packet, video_stream_idx,
                                           codec_ctx, frame,
                                           &next_pts, sws_ctx,
                                           bgra_frame, frame_duration,
                                           monitor_width, monitor_height,
                                           bgra_size, &bgra_buffer);
                        images[image_count++] = img;
                } else {
                        img = images[i];
                        printf("FALLBACK\n");
                }

                if (!img.data) {
                        fprintf(stderr, "Null image data for frame %d\n", i);
                        i = (i + 1) % image_count;
                        continue;
                }

                XImage *ximage = XCreateImage(display, visual, depth, ZPixmap, 0, (char *)img.data,
                                              img.width, img.height, 32, img.width * 4);
                if (!ximage) {
                        fprintf(stderr, "Failed to create XImage for frame %d (width=%d, height=%d, depth=%d)\n",
                                i, img.width, img.height, depth);
                        i = (i + 1) % image_count;
                        continue;
                }
                ximage->byte_order = ImageByteOrder(display);

                Pixmap pixmap = XCreatePixmap(display, root, img.width, img.height, depth);
                if (!pixmap) {
                        fprintf(stderr, "Failed to create pixmap for frame %d\n", i);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                GC gc = XCreateGC(display, pixmap, 0, NULL);
                if (!gc) {
                        fprintf(stderr, "Failed to create GC for frame %d\n", i);
                        XFreePixmap(display, pixmap);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                if (XPutImage(display, pixmap, gc, ximage, 0, 0, 0, 0, img.width, img.height) != Success) {
                        fprintf(stderr, "XPutImage failed for frame %d\n", i);
                        XFreeGC(display, gc);
                        XFreePixmap(display, pixmap);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                XCopyArea(display, pixmap, root_pixmap, root_gc, 0, 0, img.width, img.height, monitor_x, monitor_y);
                XSetWindowBackgroundPixmap(display, root, root_pixmap);

                // Set root pixmap atoms for compositor compatibility
                XChangeProperty(display, root, xrootpmap_id, XA_PIXMAP, 32, PropModeReplace,
                                (unsigned char *)&root_pixmap, 1);
                XChangeProperty(display, root, esetroot_pmap_id, XA_PIXMAP, 32, PropModeReplace,
                                (unsigned char *)&root_pixmap, 1);

                XClearWindow(display, root);
                XFlush(display);

                XFreeGC(display, gc);
                XFreePixmap(display, pixmap);
                //XDestroyImage(ximage);

                usleep(33333);
                i = (i + 1) % image_count;
        }

        XFreeGC(display, root_gc);
        XFreePixmap(display, root_pixmap);
        XCloseDisplay(display);
        for (int i = 0; i < image_count; i++) {
                if (images[i].data) free(images[i].data);
        }
        free(images);
        av_free(bgra_buffer);
        av_frame_free(&frame);
        av_frame_free(&bgra_frame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);
        XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
}

int main(int argc, char *argv[]) {
        if (argc < 3) {
                fprintf(stderr, "Usage: %s <input.mp4> <monitor_index>\n", argv[0]);
                return -1;
        }

        int monitor_index = atoi(argv[2]);

        if (monitor_index < 0) {
                fprintf(stderr, "Invalid monitor index\n");
                return -1;
        }

        if (!run(monitor_index, argv[1])) {
                fprintf(stderr, "Failed to execute\n");
                exit(1);
        }

        return 0;
}
