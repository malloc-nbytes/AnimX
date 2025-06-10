#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XShm.h>
#include <Imlib2.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h> // Added for av_image_get_buffer_size and av_image_fill_arrays

#define ConnectionDisconnected 1
#define FRAME_INTERVAL_NS (1000000000 / 30) // 33.33 ms for 30 FPS in nanoseconds

// Function to set wallpaper from raw RGB data
void set_wallpaper(Display *display, Window root, GC gc, int screen, int mon_x, int mon_y, int mon_width, int mon_height,
                   uint8_t *rgb_data, int width, int height) {
        // Create Imlib2 image from raw RGB data
        Imlib_Image image = imlib_create_image_using_data(width, height, (DATA32 *)rgb_data);
        if (!image) {
                fprintf(stderr, "Failed to create Imlib2 image\n");
                return;
        }

        imlib_context_set_image(image);
        imlib_context_set_anti_alias(1);

        // Scale image to monitor size
        Imlib_Image scaled_image = imlib_create_cropped_scaled_image(0, 0, width, height, mon_width, mon_height);
        if (!scaled_image) {
                fprintf(stderr, "Failed to scale image\n");
                imlib_context_set_image(image);
                imlib_free_image();
                return;
        }

        imlib_context_set_image(scaled_image);

        // Create pixmap for the root window
        int screen_width = DisplayWidth(display, screen);
        int screen_height = DisplayHeight(display, screen);
        Pixmap pixmap = XCreatePixmap(display, root, screen_width, screen_height, DefaultDepth(display, screen));
        if (!pixmap) {
                fprintf(stderr, "Failed to create pixmap\n");
                imlib_context_set_image(scaled_image);
                imlib_free_image();
                imlib_context_set_image(image);
                imlib_free_image();
                return;
        }

        imlib_context_set_drawable(pixmap);

        // Clear pixmap
        XFillRectangle(display, pixmap, gc, 0, 0, screen_width, screen_height);

        // Render image to the monitor's region
        imlib_render_image_on_drawable(mon_x, mon_y);

        // Set pixmap as root window background
        XSetWindowBackgroundPixmap(display, root, pixmap);
        XClearWindow(display, root);

        // Update root window properties
        Atom prop_root = XInternAtom(display, "_XROOTPMAP_ID", False);
        Atom prop_esetroot = XInternAtom(display, "ESETROOT_PMAP_ID", False);
        XChangeProperty(display, root, prop_root, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pixmap, 1);
        XChangeProperty(display, root, prop_esetroot, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pixmap, 1);

        // Flush and synchronize
        XFlush(display);
        XSync(display, False);

        // Clean up
        XFreePixmap(display, pixmap);
        imlib_context_set_image(scaled_image);
        imlib_free_image();
        imlib_context_set_image(image);
        imlib_free_image();
}

int main(int argc, char **argv) {
        if (argc != 2) {
                fprintf(stderr, "Usage: %s <video_path>\n", argv[0]);
                exit(1);
        }

        // Initialize X11 display
        Display *display = XOpenDisplay(NULL);
        if (!display) {
                fprintf(stderr, "Could not open X display\n");
                exit(1);
        }

        int screen = DefaultScreen(display);
        Window root = DefaultRootWindow(display);

        // Initialize Xrandr
        int xrandr_event_base, xrandr_error_base;
        if (!XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
                fprintf(stderr, "Xrandr extension not available\n");
                XCloseDisplay(display);
                exit(1);
        }

        // Initialize Imlib2
        imlib_set_cache_size(2048 * 1024);
        imlib_context_set_display(display);
        imlib_context_set_visual(DefaultVisual(display, screen));
        imlib_context_set_colormap(DefaultColormap(display, screen));

        // Check for MIT-SHM extension
        int shm_event_base, shm_error_base;
        if (!XShmQueryExtension(display)) {
                fprintf(stderr, "MIT-SHM extension not available; Imlib2 will use fallback rendering\n");
        }

        // Get screen resources
        XRRScreenResources *screen_res = XRRGetScreenResources(display, root);
        if (!screen_res) {
                fprintf(stderr, "Failed to get screen resources\n");
                XCloseDisplay(display);
                exit(1);
        }

        int monitor_index = 1; // Hardcoded; can be made configurable
        if (monitor_index < 0 || monitor_index >= screen_res->noutput) {
                fprintf(stderr, "Invalid monitor index: %d (available: 0 to %d)\n",
                        monitor_index, screen_res->noutput - 1);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Get monitor info
        XRROutputInfo *output_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[monitor_index]);
        if (!output_info || output_info->connection == ConnectionDisconnected) {
                fprintf(stderr, "Monitor %d is not connected\n", monitor_index);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, screen_res, output_info->crtc);
        if (!crtc_info) {
                fprintf(stderr, "Failed to get CRTC info for monitor %d\n", monitor_index);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        int mon_x = crtc_info->x;
        int mon_y = crtc_info->y;
        int mon_width = crtc_info->width;
        int mon_height = crtc_info->height;

        // Create a custom GC
        XGCValues gc_values;
        gc_values.foreground = 0; // Black for clearing
        GC gc = XCreateGC(display, root, GCForeground, &gc_values);
        if (!gc) {
                fprintf(stderr, "Failed to create GC\n");
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Initialize FFmpeg
        avformat_network_init();
        AVFormatContext *fmt_ctx = NULL;
        if (avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0) {
                fprintf(stderr, "Could not open video file: %s\n", argv[1]);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
                fprintf(stderr, "Could not find stream info\n");
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Find video stream
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
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        AVCodecParameters *codec_par = fmt_ctx->streams[video_stream_idx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
        if (!codec) {
                fprintf(stderr, "Decoder not found\n");
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) {
                fprintf(stderr, "Could not allocate codec context\n");
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
                fprintf(stderr, "Could not copy codec parameters\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
                fprintf(stderr, "Could not open codec\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Initialize scaling context
        struct SwsContext *sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                    codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
                                                    SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) {
                fprintf(stderr, "Could not initialize scaling context\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Allocate frame
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgb_frame = av_frame_alloc();
        if (!frame || !rgb_frame) {
                fprintf(stderr, "Could not allocate frames\n");
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Allocate buffer for RGB frame
        int rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
        uint8_t *rgb_buffer = av_malloc(rgb_buffer_size);
        if (!rgb_buffer) {
                fprintf(stderr, "Could not allocate RGB buffer\n");
                av_frame_free(&frame);
                av_frame_free(&rgb_frame);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer, AV_PIX_FMT_RGB24,
                             codec_ctx->width, codec_ctx->height, 1);

        AVPacket *packet = av_packet_alloc();
        if (!packet) {
                fprintf(stderr, "Could not allocate packet\n");
                av_free(rgb_buffer);
                av_frame_free(&frame);
                av_frame_free(&rgb_frame);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                XFreeGC(display, gc);
                XRRFreeCrtcInfo(crtc_info);
                XRRFreeOutputInfo(output_info);
                XRRFreeScreenResources(screen_res);
                XCloseDisplay(display);
                exit(1);
        }

        // Calculate frame interval for 30 FPS
        double fps = av_q2d(fmt_ctx->streams[video_stream_idx]->r_frame_rate);
        int64_t frame_interval = (int64_t)(AV_TIME_BASE / 30.0); // Target 30 FPS
        int64_t next_pts = 0;

        // Main loop to decode and display frames
        while (1) { // Infinite loop; use Ctrl+C to stop
                avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, INT64_MAX, 0); // Seek to start for looping
                while (av_read_frame(fmt_ctx, packet) >= 0) {
                        if (packet->stream_index == video_stream_idx) {
                                if (packet->pts >= next_pts) {
                                        // Decode frame
                                        if (avcodec_send_packet(codec_ctx, packet) < 0) {
                                                fprintf(stderr, "Error sending packet to decoder\n");
                                                av_packet_unref(packet);
                                                continue;
                                        }

                                        if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                                                // Convert to RGB
                                                sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height,
                                                          rgb_frame->data, rgb_frame->linesize);

                                                // Set wallpaper
                                                set_wallpaper(display, root, gc, screen, mon_x, mon_y, mon_width, mon_height,
                                                              rgb_frame->data[0], codec_ctx->width, codec_ctx->height);

                                                // Calculate next presentation time
                                                next_pts = packet->pts + frame_interval;

                                                // Sleep for 1/30th second (33.33 ms)
                                                struct timespec ts = {0, FRAME_INTERVAL_NS};
                                                nanosleep(&ts, NULL);
                                        }
                                }
                        }
                        av_packet_unref(packet);
                }
        }

        // Cleanup
        av_packet_free(&packet);
        av_free(rgb_buffer);
        av_frame_free(&frame);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        XFreeGC(display, gc);
        XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        XRRFreeScreenResources(screen_res);
        XCloseDisplay(display);

        return 0;
}
