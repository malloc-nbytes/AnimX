#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
        uint8_t *data; // RGBA data
        int width, height; // Frame dimensions
        int size; // Size of data (width * height * 4 for RGBA)
} Image;

AVFormatContext *create_avformat_ctx(const char *video_fp) {
        AVFormatContext *fmt_ctx = NULL;
        if (avformat_open_input(&fmt_ctx, video_fp, NULL, NULL) < 0) {
                fprintf(stderr, "Could not open video file\n");
                exit(1);
        }
        // Find stream info
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

        // Initialize codec context
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

        // Initialize FFmpeg
        avformat_network_init();

        // Open video file
        AVFormatContext *fmt_ctx = create_avformat_ctx(argv[1]);

        // Find video stream
        int video_stream_idx = get_video_stream_index(fmt_ctx);

        // Get codec parameters
        AVCodecContext *codec_ctx = NULL;
        AVCodecParameters *codec_par = NULL;
        const AVCodec *codec = find_codec_decoder(fmt_ctx, video_stream_idx, &codec_ctx, &codec_par);

        // Initialize X11
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

        // Get monitor information using XRandR
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

        // Initialize scaling context for RGBA conversion, scaled to monitor size
        struct SwsContext *sws_ctx = sws_getContext(
                                                    codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                    monitor_width, monitor_height, AV_PIX_FMT_BGRA,
                                                    SWS_BILINEAR, NULL, NULL, NULL
                                                    );
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

        // Allocate frame and packet
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgba_frame = av_frame_alloc();
        AVPacket *packet = av_packet_alloc();
        if (!frame || !rgba_frame || !packet) {
                fprintf(stderr, "Memory allocation failed\n");
                av_frame_free(&frame);
                av_frame_free(&rgba_frame);
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

        // Allocate RGBA buffer for monitor size
        int rgba_size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, monitor_width, monitor_height, 1);
        uint8_t *rgba_buffer = av_malloc(rgba_size * sizeof(uint8_t));
        if (!rgba_buffer) {
                fprintf(stderr, "Failed to allocate RGBA buffer\n");
                av_frame_free(&frame);
                av_frame_free(&rgba_frame);
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
        av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer, AV_PIX_FMT_BGRA, monitor_width, monitor_height, 1);

        // Calculate frame interval for 30 FPS
        double frame_interval = 1.0 / 30.0;
        double video_time_base = av_q2d(fmt_ctx->streams[video_stream_idx]->time_base);
        int64_t frame_duration = frame_interval / video_time_base;
        int64_t next_pts = 0;

        // Store images in memory
        Image *images = malloc(1000 * sizeof(Image));
        int image_count = 0;

        while (av_read_frame(fmt_ctx, packet) >= 0) {
                if (packet->stream_index == video_stream_idx) {
                        if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                                        if (frame->pts >= next_pts) {
                                                sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, codec_ctx->height,
                                                          rgba_frame->data, rgba_frame->linesize);

                                                Image *img = &images[image_count];
                                                img->width = monitor_width;
                                                img->height = monitor_height;
                                                img->size = rgba_size;
                                                img->data = malloc(rgba_size);
                                                if (!img->data) {
                                                        fprintf(stderr, "Failed to allocate image data\n");
                                                        break;
                                                }
                                                memcpy(img->data, rgba_buffer, rgba_size);
                                                image_count++;

                                                next_pts += frame_duration;
                                                printf("Image from frame created: %p\n", img);
                                        }
                                }
                        }
                }
                av_packet_unref(packet);
        }

        printf("Extracted %d frames at %dx%d (RGBA)\n", image_count, monitor_width, monitor_height);

        // Debug visual information
        printf("Screen depth: %d, Visual class: %d (TrueColor=%d), Byte order: %s\n",
               depth, visual->class, TrueColor, ImageByteOrder(display) == LSBFirst ? "LSBFirst" : "MSBFirst");

        // Ensure visual is TrueColor
        if (visual->class != TrueColor) {
                fprintf(stderr, "Unsupported visual: not TrueColor\n");
                for (int i = 0; i < image_count; i++) free(images[i].data);
                free(images);
                av_free(rgba_buffer);
                av_frame_free(&frame);
                av_frame_free(&rgba_frame);
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

        // Create a pixmap for the entire root window to preserve other monitors
        Pixmap root_pixmap = XCreatePixmap(display, root, DisplayWidth(display, screen), DisplayHeight(display, screen), depth);
        GC root_gc = XCreateGC(display, root_pixmap, 0, NULL);
        XFillRectangle(display, root_pixmap, root_gc, 0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen)); // Clear

        int i = 0;
        while (1) {
                Image *img = &images[i];
                if (!img->data) {
                        fprintf(stderr, "Null image data for frame %d\n", i);
                        i = (i + 1) % image_count;
                        continue;
                }

                // Create XImage
                XImage *ximage = XCreateImage(display, visual, depth, ZPixmap, 0, (char *)img->data,
                                              img->width, img->height, 32, img->width * 4);
                if (!ximage) {
                        fprintf(stderr, "Failed to create XImage for frame %d (width=%d, height=%d, depth=%d)\n",
                                i, img->width, img->height, depth);
                        i = (i + 1) % image_count;
                        continue;
                }
                ximage->byte_order = ImageByteOrder(display);

                // Create pixmap for the monitor
                Pixmap pixmap = XCreatePixmap(display, root, img->width, img->height, depth);
                if (!pixmap) {
                        fprintf(stderr, "Failed to create pixmap for frame %d\n", i);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                // Create GC
                GC gc = XCreateGC(display, pixmap, 0, NULL);
                if (!gc) {
                        fprintf(stderr, "Failed to create GC for frame %d\n", i);
                        XFreePixmap(display, pixmap);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                // Copy image to pixmap
                if (XPutImage(display, pixmap, gc, ximage, 0, 0, 0, 0, img->width, img->height) != Success) {
                        fprintf(stderr, "XPutImage failed for frame %d\n", i);
                        XFreeGC(display, gc);
                        XFreePixmap(display, pixmap);
                        XDestroyImage(ximage);
                        i = (i + 1) % image_count;
                        continue;
                }

                // Copy monitor pixmap to root pixmap at monitor coordinates
                XCopyArea(display, pixmap, root_pixmap, root_gc, 0, 0, img->width, img->height, monitor_x, monitor_y);

                // Set root pixmap as background
                XSetWindowBackgroundPixmap(display, root, root_pixmap);
                XClearWindow(display, root);
                XFlush(display);

                // Free resources
                XFreeGC(display, gc);
                XFreePixmap(display, pixmap);
                //XDestroyImage(ximage);
                //img->data = NULL;

                usleep(33333);
                i = (i + 1) % image_count;
        }

        // Cleanup
        XFreeGC(display, root_gc);
        XFreePixmap(display, root_pixmap);
        XCloseDisplay(display);
        for (int i = 0; i < image_count; i++) {
                if (images[i].data) free(images[i].data);
        }
        free(images);
        av_free(rgba_buffer);
        av_frame_free(&frame);
        av_frame_free(&rgba_frame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);
        XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);

        return 0;
}
