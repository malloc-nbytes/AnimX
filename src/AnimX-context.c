#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "AnimX-context.h"

static AVCodec *find_codec_decoder(
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
                return NULL;
        }
        *codec_ctx = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(*codec_ctx, *codec_par) < 0) {
                fprintf(stderr, "Failed to copy codec parameters\n");
                avformat_close_input(&fmt_ctx);
                return NULL;
        }
        if (avcodec_open2(*codec_ctx, codec, NULL) < 0) {
                fprintf(stderr, "Could not open codec\n");
                avcodec_free_context(codec_ctx);
                avformat_close_input(&fmt_ctx);
                return NULL;
        }
        return (AVCodec *)codec;
}


static int get_video_stream_index(AVFormatContext *fmt_ctx) {
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
                return -1;
        }
        return video_stream_idx;
}

static AVFormatContext *create_avformat_ctx(const char *video_fp) {
        AVFormatContext *fmt_ctx = NULL;
        if (avformat_open_input(&fmt_ctx, video_fp, NULL, NULL) < 0) {
                syslog(LOG_ERR, "Could not open video file: %s, %s\n", video_fp, strerror(errno));
                fprintf(stderr, "Could not open video file: %s, %s\n", video_fp, strerror(errno));
                return NULL;
        }
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
                syslog(LOG_ERR, "Could not find stream info\n");
                fprintf(stderr, "Could not find stream info\n");
                avformat_close_input(&fmt_ctx);
                return NULL;
        }
        return fmt_ctx;
}

int init_context(Context *ctx, int monitor_index, const char *video_mp4) {
        avformat_network_init();
        ctx->fmt_ctx = create_avformat_ctx(video_mp4);
        if (!ctx->fmt_ctx) {
                syslog(LOG_ERR, "ctx->fmt_ctx");
                return -1;
        }
        ctx->video_stream_idx = get_video_stream_index(ctx->fmt_ctx);
        if (ctx->video_stream_idx == -1) {
                syslog(LOG_ERR, "ctx->video_stream_idx");
                return -1;
        }
        if (!find_codec_decoder(ctx->fmt_ctx, ctx->video_stream_idx, &ctx->codec_ctx, &ctx->codec_par)) {
                syslog(LOG_ERR, "find_codec_decoder()");
                return -1;
        }

        ctx->display = XOpenDisplay(NULL);
        if (!ctx->display) {
                syslog(LOG_ERR, "Cannot open X display\n");
                fprintf(stderr, "Cannot open X display\n");
                return -1;
        }

        ctx->screen = DefaultScreen(ctx->display);
        ctx->root = RootWindow(ctx->display, ctx->screen);
        ctx->visual = DefaultVisual(ctx->display, ctx->screen);
        ctx->depth = DefaultDepth(ctx->display, ctx->screen);

        ctx->screen_res = XRRGetScreenResources(ctx->display, ctx->root);
        if (!ctx->screen_res) {
                syslog(LOG_ERR, "Failed to get screen resources\n");
                fprintf(stderr, "Failed to get screen resources\n");
                return -1;
        }

        // Initialize monitor configurations
        int num_outputs = ctx->screen_res->noutput;
        int connected_count = 0;

        // First pass: count connected monitors with valid CRTCs
        for (int i = 0; i < num_outputs; i++) {
                XRROutputInfo *output_info = XRRGetOutputInfo(ctx->display, ctx->screen_res, ctx->screen_res->outputs[i]);
                if (output_info && output_info->connection == RR_Connected && output_info->crtc) {
                        XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(ctx->display, ctx->screen_res, output_info->crtc);
                        if (crtc_info && crtc_info->width > 0 && crtc_info->height > 0) {
                                connected_count++;
                        }
                        if (crtc_info) XRRFreeCrtcInfo(crtc_info);
                }
                if (output_info) XRRFreeOutputInfo(output_info);
        }

        if (connected_count == 0) {
                syslog(LOG_ERR, "No connected monitors with valid CRTCs found\n");
                fprintf(stderr, "No connected monitors with valid CRTCs found\n");
                return -1;
        }

        // Allocate arrays for all connected monitors
        ctx->output_infos = (XRROutputInfo **)malloc(connected_count * sizeof(XRROutputInfo *));
        ctx->crtc_infos = (XRRCrtcInfo **)malloc(connected_count * sizeof(XRRCrtcInfo *));
        if (monitor_index == -2) {
                ctx->monitor_pixmaps = (Pixmap *)malloc(connected_count * sizeof(Pixmap));
                ctx->monitor_gcs = (GC *)malloc(connected_count * sizeof(GC));
        }
        if (!ctx->output_infos || !ctx->crtc_infos || (monitor_index == -2 && (!ctx->monitor_pixmaps || !ctx->monitor_gcs))) {
                syslog(LOG_ERR, "Failed to allocate monitor info arrays\n");
                fprintf(stderr, "Failed to allocate monitor info arrays\n");
                if (ctx->output_infos) free(ctx->output_infos);
                if (ctx->crtc_infos) free(ctx->crtc_infos);
                if (ctx->monitor_pixmaps) free(ctx->monitor_pixmaps);
                if (ctx->monitor_gcs) free(ctx->monitor_gcs);
                return -1;
        }
        ctx->num_monitors = connected_count;
        ctx->mirror_mode = (monitor_index == -2);

        // Second pass: collect connected monitor info
        int idx = 0;
        long min_x = LONG_MAX, min_y = LONG_MAX;
        long max_x = LONG_MIN, max_y = LONG_MIN;

        for (int i = 0; i < num_outputs && idx < connected_count; i++) {
                XRROutputInfo *output_info = XRRGetOutputInfo(ctx->display, ctx->screen_res, ctx->screen_res->outputs[i]);
                if (output_info && output_info->connection == RR_Connected && output_info->crtc) {
                        XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(ctx->display, ctx->screen_res, output_info->crtc);
                        if (crtc_info && crtc_info->width > 0 && crtc_info->height > 0) {
                                ctx->output_infos[idx] = output_info;
                                ctx->crtc_infos[idx] = crtc_info;
                                printf("Monitor %d: %dx%d at (%ld,%ld)\n", idx, crtc_info->width, crtc_info->height, (long)crtc_info->x, (long)crtc_info->y);
                                // Update bounding rectangle for combined mode
                                if (monitor_index == -1) {
                                        if (crtc_info->x < min_x) min_x = crtc_info->x;
                                        if (crtc_info->y < min_y) min_y = crtc_info->y;
                                        if ((long)crtc_info->x + crtc_info->width > max_x) max_x = (long)crtc_info->x + crtc_info->width;
                                        if ((long)crtc_info->y + crtc_info->height > max_y) max_y = (long)crtc_info->y + crtc_info->height;
                                }
                                idx++;
                        } else {
                                if (crtc_info) XRRFreeCrtcInfo(crtc_info);
                                if (output_info) XRRFreeOutputInfo(output_info);
                        }
                } else if (output_info) {
                        XRRFreeOutputInfo(output_info);
                }
        }

        if (idx == 0) {
                fprintf(stderr, "No valid CRTCs found for connected monitors\n");
                free(ctx->output_infos);
                free(ctx->crtc_infos);
                if (ctx->monitor_pixmaps) free(ctx->monitor_pixmaps);
                if (ctx->monitor_gcs) free(ctx->monitor_gcs);
                return -1;
        }

        if (monitor_index == -2) {
                // Mirror mode: use first monitor's dimensions
                int ref_idx = 0;
                ctx->monitor_x = ctx->crtc_infos[ref_idx]->x;
                ctx->monitor_y = ctx->crtc_infos[ref_idx]->y;
                ctx->monitor_width = ctx->crtc_infos[ref_idx]->width;
                ctx->monitor_height = ctx->crtc_infos[ref_idx]->height;
                printf("Mirroring on all %d monitors using reference monitor %d: %ldx%ld at (%ld,%ld)\n",
                       connected_count, ref_idx, ctx->monitor_width, ctx->monitor_height, ctx->monitor_x, ctx->monitor_y);

                // Create pixmap and GC for each monitor
                for (int i = 0; i < ctx->num_monitors; i++) {
                        ctx->monitor_pixmaps[i] = XCreatePixmap(ctx->display, ctx->root, ctx->monitor_width, ctx->monitor_height, ctx->depth);
                        ctx->monitor_gcs[i] = XCreateGC(ctx->display, ctx->monitor_pixmaps[i], 0, NULL);
                        if (!ctx->monitor_pixmaps[i] || !ctx->monitor_gcs[i]) {
                                fprintf(stderr, "Failed to create pixmap or GC for monitor %d\n", i);
                                for (int j = 0; j < i; j++) {
                                        if (ctx->monitor_gcs[j]) XFreeGC(ctx->display, ctx->monitor_gcs[j]);
                                        if (ctx->monitor_pixmaps[j]) XFreePixmap(ctx->display, ctx->monitor_pixmaps[j]);
                                }
                                free(ctx->monitor_pixmaps);
                                free(ctx->monitor_gcs);
                                ctx->monitor_pixmaps = NULL;
                                ctx->monitor_gcs = NULL;
                                return -1;
                        }
                        XFillRectangle(ctx->display, ctx->monitor_pixmaps[i], ctx->monitor_gcs[i], 0, 0, ctx->monitor_width, ctx->monitor_height);
                }
        } else if (monitor_index == -1) {
                // Combine all monitors into a single virtual monitor
                long width = max_x - min_x;
                long height = max_y - min_y;
                if (width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX) {
                        fprintf(stderr, "Invalid combined monitor dimensions: %ldx%ld\n", width, height);
                        for (int i = 0; i < idx; i++) {
                                if (ctx->crtc_infos[i]) XRRFreeCrtcInfo(ctx->crtc_infos[i]);
                                if (ctx->output_infos[i]) XRRFreeOutputInfo(ctx->output_infos[i]);
                        }
                        free(ctx->output_infos);
                        free(ctx->crtc_infos);
                        return -1;
                }
                ctx->monitor_x = min_x;
                ctx->monitor_y = min_y;
                ctx->monitor_width = width;
                ctx->monitor_height = height;
                printf("Combined monitors: %ldx%ld at (%ld,%ld)\n", ctx->monitor_width, ctx->monitor_height, ctx->monitor_x, ctx->monitor_y);
        } else {
                // Single monitor
                if (monitor_index >= num_outputs) {
                        fprintf(stderr, "Monitor index %d out of range (0-%d)\n", monitor_index, num_outputs - 1);
                        free(ctx->output_infos);
                        free(ctx->crtc_infos);
                        return -1;
                }
                ctx->output_infos[0] = XRRGetOutputInfo(ctx->display, ctx->screen_res, ctx->screen_res->outputs[monitor_index]);
                if (!ctx->output_infos[0] || ctx->output_infos[0]->connection != RR_Connected) {
                        fprintf(stderr, "Monitor %d is not connected\n", monitor_index);
                        free(ctx->output_infos);
                        free(ctx->crtc_infos);
                        return -1;
                }
                ctx->crtc_infos[0] = XRRGetCrtcInfo(ctx->display, ctx->screen_res, ctx->output_infos[0]->crtc);
                if (!ctx->crtc_infos[0] || ctx->crtc_infos[0]->width <= 0 || ctx->crtc_infos[0]->height <= 0) {
                        fprintf(stderr, "Failed to get valid CRTC info for monitor %d\n", monitor_index);
                        if (ctx->crtc_infos[0]) XRRFreeCrtcInfo(ctx->crtc_infos[0]);
                        XRRFreeOutputInfo(ctx->output_infos[0]);
                        free(ctx->output_infos);
                        free(ctx->crtc_infos);
                        return -1;
                }
                ctx->num_monitors = 1;
                ctx->monitor_x = ctx->crtc_infos[0]->x;
                ctx->monitor_y = ctx->crtc_infos[0]->y;
                ctx->monitor_width = ctx->crtc_infos[0]->width;
                ctx->monitor_height = ctx->crtc_infos[0]->height;
                printf("Monitor %d: %ldx%ld at (%ld,%ld)\n", monitor_index, ctx->monitor_width, ctx->monitor_height, ctx->monitor_x, ctx->monitor_y);
        }

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
        if (ctx->bgra_size <= 0) {
                fprintf(stderr, "Invalid BGRA buffer size for %ldx%ld\n", ctx->monitor_width, ctx->monitor_height);
                return -1;
        }
        ctx->bgra_buffer = (uint8_t *)av_malloc(ctx->bgra_size * sizeof(uint8_t));
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

void cleanup_context(Context *ctx) {
        if (ctx->root_gc) XFreeGC(ctx->display, ctx->root_gc);
        if (ctx->root_pixmap) XFreePixmap(ctx->display, ctx->root_pixmap);
        if (ctx->mirror_mode) {
                for (int i = 0; i < ctx->num_monitors; i++) {
                        if (ctx->monitor_gcs && ctx->monitor_gcs[i]) XFreeGC(ctx->display, ctx->monitor_gcs[i]);
                        if (ctx->monitor_pixmaps && ctx->monitor_pixmaps[i]) XFreePixmap(ctx->display, ctx->monitor_pixmaps[i]);
                }
                if (ctx->monitor_gcs) free(ctx->monitor_gcs);
                if (ctx->monitor_pixmaps) free(ctx->monitor_pixmaps);
        }
        if (ctx->bgra_buffer) av_free(ctx->bgra_buffer);
        if (ctx->frame) av_frame_free(&ctx->frame);
        if (ctx->bgra_frame) av_frame_free(&ctx->bgra_frame);
        if (ctx->packet) av_packet_free(&ctx->packet);
        if (ctx->sws_ctx) sws_freeContext(ctx->sws_ctx);
        for (int i = 0; i < ctx->num_monitors; i++) {
                if (ctx->crtc_infos && ctx->crtc_infos[i]) XRRFreeCrtcInfo(ctx->crtc_infos[i]);
                if (ctx->output_infos && ctx->output_infos[i]) XRRFreeOutputInfo(ctx->output_infos[i]);
        }
        if (ctx->crtc_infos) free(ctx->crtc_infos);
        if (ctx->output_infos) free(ctx->output_infos);
        if (ctx->screen_res) XRRFreeScreenResources(ctx->screen_res);
        if (ctx->display) XCloseDisplay(ctx->display);
        if (ctx->codec_ctx) avcodec_free_context(&ctx->codec_ctx);
        if (ctx->fmt_ctx) avformat_close_input(&ctx->fmt_ctx);
}

