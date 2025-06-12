// glibc
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <string.h>
#include <errno.h>

// FFmpeg
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

// X
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

// Local
#include "context.h"
#include "flag.h"
#include "utils.h"
#include "gl.h"
#include "dyn_array.h"
#define CLAP_IMPL
#include "clap.h"

static struct {
        uint32_t flags;
        char *wp;
        int mon;
        Mode_Type mode;
        double maxmem;
} g_config = {
        .flags = 0x00000000,
        .wp = NULL,
        .mon = -1,
        .mode = MODE_STREAM,
        .maxmem = 0.f,
};

typedef struct {
        uint8_t *data; // BGRA data
        int width, height; // Frame dimensions
        int size; // Size of data (width * height * 4 for BGRA)
} Image;

static int g_pid_fd;

// Thread-specific key for Worker_Data
static pthread_key_t worker_data_key;

// Threading data for streaming mode
typedef struct {
        Context *ctx;
        Image *buffer;   // Circular buffer for frames
        int buffer_size; // Number of frames in buffer
        int write_idx;   // Where producer writes
        int read_idx;    // Where consumer reads
        int count;       // Number of frames in buffer
        struct {
                pthread_mutex_t mutex;
                pthread_cond_t not_full;
                pthread_cond_t not_empty;
        } threading;
        int done;        // Flag to signal threads to exit
} Thread_Data;

typedef struct {
        pthread_t thread;        // Worker thread handle
        pthread_mutex_t mutex;   // Mutex for worker state
        pthread_cond_t cond;     // Condition variable for signaling worker start/stop
        int running;            // Flag indicating if worker is running
        int stop;               // Flag to signal worker to stop
        char *wp;               // Current wallpaper path
        int mon;                // Current monitor index
        Mode_Type mode;         // Current mode
        double maxmem;          // Current max memory (for MODE_LOAD)
        Thread_Data *td;        // Pointer to Thread_Data for run_stream
} Worker_Data;

int run_stream(int monitor_index, const char *video_mp4);
int run_load_all(int monitor_index, const char *video_mp4);
static void parse_daemon_sender_msg(const char *msg);

// Initialize thread-specific key
static void init_thread_specific(void) {
        pthread_key_create(&worker_data_key, NULL);
}

static int is_single_frame(Context *ctx) {
        // Check if the input is likely an image (single frame)
        // Images typically have a duration of 0 or a single frame
        if (ctx->fmt_ctx->duration != AV_NOPTS_VALUE && ctx->fmt_ctx->duration <= AV_TIME_BASE) {
                return 1; // Duration <= 1 second, likely an image
        }

        // Alternatively, try decoding to count frames
        AVPacket *packet = av_packet_alloc();
        if (!packet) {
                syslog(LOG_ERR, "Failed to allocate packet for frame count");
                return 0;
        }

        int frame_count = 0;
        int video_stream_idx = ctx->video_stream_idx;

        // Save format context position
        int64_t start_pos = avio_tell(ctx->fmt_ctx->pb);
        avformat_seek_file(ctx->fmt_ctx, video_stream_idx, INT64_MIN, 0, INT64_MAX, 0);

        while (av_read_frame(ctx->fmt_ctx, packet) >= 0) {
                if (packet->stream_index == video_stream_idx) {
                        if (avcodec_send_packet(ctx->codec_ctx, packet) >= 0) {
                                while (avcodec_receive_frame(ctx->codec_ctx, ctx->frame) >= 0) {
                                        frame_count++;
                                        if (frame_count > 1) {
                                                av_packet_unref(packet);
                                                // Restore format context position
                                                avformat_seek_file(ctx->fmt_ctx, video_stream_idx, INT64_MIN, start_pos, INT64_MAX, 0);
                                                av_packet_free(&packet);
                                                return 0; // More than one frame, not an image
                                        }
                                }
                        }
                }
                av_packet_unref(packet);
        }

        // Restore format context position
        avformat_seek_file(ctx->fmt_ctx, video_stream_idx, INT64_MIN, start_pos, INT64_MAX, 0);
        av_packet_free(&packet);
        return frame_count == 1;
}

static void init_worker_data(Worker_Data *wd) {
        wd->thread = 0;
        pthread_mutex_init(&wd->mutex, NULL);
        pthread_cond_init(&wd->cond, NULL);
        wd->running = 0;
        wd->stop = 0;
        wd->wp = NULL;
        wd->mon = -1;
        wd->mode = MODE_STREAM;
        wd->maxmem = 0.f;
        wd->td = NULL;
}

static void cleanup_worker_data(Worker_Data *wd) {
        if (wd->wp) free(wd->wp);
        pthread_mutex_destroy(&wd->mutex);
        pthread_cond_destroy(&wd->cond);
        wd->td = NULL; // Thread_Data is cleaned up in run_stream
}

void *worker_thread(void *arg) {
        Worker_Data *wd = (Worker_Data *)arg;
        pthread_setspecific(worker_data_key, wd); // Set thread-specific data
        while (1) {
                pthread_mutex_lock(&wd->mutex);
                while (!wd->running && !wd->stop) {
                        pthread_cond_wait(&wd->cond, &wd->mutex);
                }
                if (wd->stop) {
                        wd->running = 0;
                        pthread_mutex_unlock(&wd->mutex);
                        break;
                }
                pthread_mutex_unlock(&wd->mutex);

                char *wp = NULL;
                int mon;
                Mode_Type mode;
                double maxmem;
                pthread_mutex_lock(&wd->mutex);
                if (wd->wp) wp = strdup(wd->wp);
                mon = wd->mon;
                mode = wd->mode;
                maxmem = wd->maxmem;
                pthread_mutex_unlock(&wd->mutex);

                if (mode == MODE_STREAM) {
                        syslog(LOG_INFO, "Worker: Starting run_stream with wp=%s, mon=%d", wp ? wp : "(null)", mon);
                        run_stream(mon, wp);
                } else if (mode == MODE_LOAD) {
                        syslog(LOG_INFO, "Worker: Starting run_load_all with wp=%s, mon=%d, maxmem=%f", wp ? wp : "(null)", mon, maxmem);
                        run_load_all(mon, wp);
                }

                free(wp);

                pthread_mutex_lock(&wd->mutex);
                wd->running = 0;
                pthread_cond_signal(&wd->cond);
                pthread_mutex_unlock(&wd->mutex);
        }
        return NULL;
}

// Current time in microseconds
long get_time_us(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

// Display a single frame
int display_frame(Context *ctx, uint8_t *data, int width, int height, int frame_count) {
        uint8_t *ximage_buffer = (uint8_t *)malloc(ctx->bgra_size);
        if (!ximage_buffer) {
                syslog(LOG_ERR, "Failed to allocate XImage buffer for frame %d\n", frame_count);
                fprintf(stderr, "Failed to allocate XImage buffer for frame %d\n", frame_count);
                return -1;
        }
        memcpy(ximage_buffer, data, ctx->bgra_size);

        XImage *ximage = XCreateImage(ctx->display, ctx->visual, ctx->depth, ZPixmap, 0, (char *)ximage_buffer,
                                      width, height, 32, width * 4);
        if (!ximage) {
                syslog(LOG_ERR, "Failed to create XImage for frame %d\n", frame_count);
                fprintf(stderr, "Failed to create XImage for frame %d\n", frame_count);
                free(ximage_buffer);
                return -1;
        }
        ximage->byte_order = ImageByteOrder(ctx->display);

        Pixmap pixmap = XCreatePixmap(ctx->display, ctx->root, width, height, ctx->depth);
        if (!pixmap) {
                syslog(LOG_ERR, "Failed to create pixmap for frame %d\n", frame_count);
                fprintf(stderr, "Failed to create pixmap for frame %d\n", frame_count);
                XDestroyImage(ximage);
                return -1;
        }

        GC gc = XCreateGC(ctx->display, pixmap, 0, NULL);
        if (!gc) {
                syslog(LOG_ERR, "Failed to create GC for frame %d\n", frame_count);
                fprintf(stderr, "Failed to create GC for frame %d\n", frame_count);
                XFreePixmap(ctx->display, pixmap);
                XDestroyImage(ximage);
                return -1;
        }

        if (XPutImage(ctx->display, pixmap, gc, ximage, 0, 0, 0, 0, width, height) != Success) {
                syslog(LOG_ERR, "XPutImage failed for frame %d\n", frame_count);
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
        Worker_Data *wd = pthread_getspecific(worker_data_key);
        Context ctx = {0};
        if (init_context(&ctx, monitor_index, video_mp4) < 0) {
                cleanup_context(&ctx);
                return -1;
        }

        // Check if input is a single frame
        int single_frame = is_single_frame(&ctx);
        if (single_frame) {
                // Read and display single frame
                int frame_count = 0;
                while (av_read_frame(ctx.fmt_ctx, ctx.packet) >= 0) {
                        if (ctx.packet->stream_index == ctx.video_stream_idx) {
                                if (avcodec_send_packet(ctx.codec_ctx, ctx.packet) >= 0) {
                                        while (avcodec_receive_frame(ctx.codec_ctx, ctx.frame) >= 0) {
                                                sws_scale(ctx.sws_ctx, (const uint8_t * const *)ctx.frame->data, ctx.frame->linesize, 0, ctx.codec_ctx->height,
                                                          ctx.bgra_frame->data, ctx.bgra_frame->linesize);
                                                if (display_frame(&ctx, ctx.bgra_buffer, ctx.monitor_width, ctx.monitor_height, frame_count) < 0) {
                                                        syslog(LOG_ERR, "Failed to display single frame");
                                                        fprintf(stderr, "Failed to display single frame\n");
                                                } else {
                                                        printf("Displayed single frame\n");
                                                }
                                                frame_count++;
                                                break; // Only process one frame
                                        }
                                }
                        }
                        av_packet_unref(ctx.packet);
                }
                cleanup_context(&ctx);
                return single_frame; // Return 1 to indicate single frame
        }

        // Existing multi-frame logic
        dyn_array(Image, images);
        if (!images.data) {
                syslog(LOG_ERR, "Failed to allocate image array\n");
                fprintf(stderr, "Failed to allocate image array\n");
                cleanup_context(&ctx);
                return -1;
        }
        int image_count = 0;
        int64_t next_pts = 0;
        const char loading[] = {'|', '/', '-', '\\', '|'};
        size_t loading_len = sizeof(loading)/sizeof(*loading);
        size_t loading_i = 0;
        double mem_usage = 0;

        while (av_read_frame(ctx.fmt_ctx, ctx.packet) >= 0) {
                pthread_mutex_lock(&wd->mutex);
                if (wd->stop) {
                        pthread_mutex_unlock(&wd->mutex);
                        av_packet_unref(ctx.packet);
                        break;
                }
                pthread_mutex_unlock(&wd->mutex);

                if (ctx.packet->stream_index == ctx.video_stream_idx) {
                        if (avcodec_send_packet(ctx.codec_ctx, ctx.packet) >= 0) {
                                while (avcodec_receive_frame(ctx.codec_ctx, ctx.frame) >= 0) {
                                        if (ctx.frame->pts >= next_pts) {
                                                double GBs = mem_usage / (1024.0 * 1024.0 * 1024.0);
                                                if ((g_config.flags & FT_MAXMEM) && GBs >= g_config.maxmem) {
                                                        fflush(stdout);
                                                        printf("maximum memory allowed (%f) has been exceeded, stopping image generation...\n", g_config.maxmem);
                                                        goto done;
                                                }
                                                sws_scale(ctx.sws_ctx, (const uint8_t * const *)ctx.frame->data, ctx.frame->linesize, 0, ctx.codec_ctx->height,
                                                          ctx.bgra_frame->data, ctx.bgra_frame->linesize);
                                                Image img = (Image){
                                                        .data = (uint8_t *)malloc(ctx.bgra_size),
                                                        .width = (int)ctx.monitor_width,
                                                        .height = (int)ctx.monitor_height,
                                                };
                                                mem_usage += (double)ctx.bgra_size;
                                                if (!img.data) {
                                                        syslog(LOG_ERR, "Failed to allocate image data\n");
                                                        fprintf(stderr, "Failed to allocate image data\n");
                                                        break;
                                                }
                                                memcpy(img.data, ctx.bgra_buffer, ctx.bgra_size);
                                                image_count++;
                                                next_pts += ctx.frame_duration;
                                                printf("Loading Frames... [%d], mem=%fGB %c\n", image_count, GBs, loading[loading_i]);
                                                fflush(stdout);
                                                printf("\033[A");
                                                printf("\033[2K");
                                                if (image_count%5 == 0) {
                                                        loading_i = (loading_i+1)%loading_len;
                                                }
                                                dyn_array_append(images, img);
                                        }
                                }
                        }
                }
                av_packet_unref(ctx.packet);
        }

 done:
        printf("Loaded %d frames at %ldx%ld (BGRA)\n", image_count, ctx.monitor_width, ctx.monitor_height);
        sleep(1);

        int i = 0;
        while (1) {
                pthread_mutex_lock(&wd->mutex);
                if (wd->stop) {
                        pthread_mutex_unlock(&wd->mutex);
                        break;
                }
                pthread_mutex_unlock(&wd->mutex);

                Image *img = &images.data[i];
                if (!img->data) {
                        syslog(LOG_ERR, "Null image data for frame %d\n", i);
                        fprintf(stderr, "Null image data for frame %d\n", i);
                        i = (i + 1) % image_count;
                        continue;
                }

                long start_time = get_time_us();
                if (display_frame(&ctx, img->data, img->width, img->height, i) < 0) {
                        i = (i + 1) % image_count;
                        continue;
                }

                long end_time = get_time_us();
                long processing_time = end_time - start_time;
                long target_time = 33333; // 33.33ms for 30 FPS
                long sleep_time = target_time - processing_time;

                if (sleep_time > 0) {
                        usleep(sleep_time);
                }

                printf("Displayed frame %d (processing: %ld us, sleep: %ld us)\n", i + 1, processing_time, sleep_time > 0 ? sleep_time : 0);
                fflush(stdout);
                printf("\033[A");
                printf("\033[2K");
                i = (i + 1) % image_count;
        }

        for (int i = 0; i < image_count; i++) {
                if (images.data[i].data) free(images.data[i].data);
        }
        dyn_array_free(images);
        cleanup_context(&ctx);
        return 0; // Multi-frame case
}

// Producer thread: decodes and scales frames
void *producer_thread(void *arg) {
        Thread_Data *td = (Thread_Data *)arg;
        Context *ctx = td->ctx;
        int64_t next_pts = 0;
        int frame_count = 0;

        while (!td->done) {
                pthread_mutex_lock(&td->threading.mutex);
                // Wait if buffer is full
                while (td->count == td->buffer_size && !td->done) {
                        pthread_cond_wait(&td->threading.not_full, &td->threading.mutex);
                }
                if (td->done) {
                        pthread_mutex_unlock(&td->threading.mutex);
                        break;
                }
                pthread_mutex_unlock(&td->threading.mutex);

                int ret = av_read_frame(ctx->fmt_ctx, ctx->packet);
                if (ret < 0 || td->done) {
                        pthread_mutex_lock(&td->threading.mutex);
                        av_packet_unref(ctx->packet);
                        if (!td->done) {
                                avcodec_flush_buffers(ctx->codec_ctx);
                                if (avformat_seek_file(ctx->fmt_ctx, ctx->video_stream_idx, INT64_MIN, 0, INT64_MAX, 0) < 0) {
                                        syslog(LOG_ERR, "Failed to seek to start of video\n");
                                        fprintf(stderr, "Failed to seek to start of video\n");
                                        td->done = 1;
                                } else {
                                        next_pts = 0;
                                        frame_count = 0;
                                }
                        }
                        pthread_cond_broadcast(&td->threading.not_empty);
                        pthread_mutex_unlock(&td->threading.mutex);
                        if (td->done) break;
                        continue;
                }
                if (ctx->packet->stream_index == ctx->video_stream_idx) {
                        if (avcodec_send_packet(ctx->codec_ctx, ctx->packet) >= 0) {
                                while (avcodec_receive_frame(ctx->codec_ctx, ctx->frame) >= 0) {
                                        if (ctx->frame->pts >= next_pts) {
                                                sws_scale(ctx->sws_ctx, (const uint8_t * const *)ctx->frame->data, ctx->frame->linesize, 0,
                                                          ctx->codec_ctx->height, ctx->bgra_frame->data, ctx->bgra_frame->linesize);

                                                pthread_mutex_lock(&td->threading.mutex);
                                                Image *img = &td->buffer[td->write_idx];
                                                img->width = ctx->monitor_width;
                                                img->height = ctx->monitor_height;
                                                img->size = ctx->bgra_size;
                                                if (!img->data) {
                                                        img->data = (uint8_t *)malloc(ctx->bgra_size);
                                                        if (!img->data) {
                                                                syslog(LOG_ERR, "Failed to allocate buffer frame %d\n", frame_count);
                                                                fprintf(stderr, "Failed to allocate buffer frame %d\n", frame_count);
                                                                td->done = 1;
                                                                pthread_cond_broadcast(&td->threading.not_empty);
                                                                pthread_mutex_unlock(&td->threading.mutex);
                                                                break;
                                                        }
                                                }
                                                memcpy(img->data, ctx->bgra_buffer, ctx->bgra_size);
                                                td->write_idx = (td->write_idx + 1) % td->buffer_size;
                                                td->count++;
                                                frame_count++;
                                                next_pts += ctx->frame_duration;
                                                pthread_cond_signal(&td->threading.not_empty);
                                                pthread_mutex_unlock(&td->threading.mutex);
                                        }
                                }
                        }
                }
                av_packet_unref(ctx->packet);
        }
        return NULL;
}

// Consumer thread: displays frames
void *consumer_thread(void *arg) {
        Thread_Data *td = (Thread_Data *)arg;
        Context *ctx = td->ctx;
        int frame_count = 0;

        while (!td->done) {
                long start_time = get_time_us();

                pthread_mutex_lock(&td->threading.mutex);
                while (td->count == 0 && !td->done) {
                        pthread_cond_wait(&td->threading.not_empty, &td->threading.mutex);
                }
                if (td->count == 0 && td->done) {
                        pthread_mutex_unlock(&td->threading.mutex);
                        break;
                }
                Image *img = &td->buffer[td->read_idx];
                td->read_idx = (td->read_idx + 1) % td->buffer_size;
                td->count--;
                pthread_cond_signal(&td->threading.not_full);
                pthread_mutex_unlock(&td->threading.mutex);

                if (display_frame(ctx, img->data, img->width, img->height, frame_count) < 0) {
                        continue;
                }

                frame_count++;
                long end_time = get_time_us();
                long processing_time = end_time - start_time;
                long target_time = 33333; // 33.33ms for 30 FPS
                long sleep_time = target_time - processing_time;

                if (sleep_time > 0) {
                        usleep(sleep_time);
                }

                printf("Displayed frame %d (processing: %ld us, sleep: %ld us)\n",
                       frame_count, processing_time, sleep_time > 0 ? sleep_time : 0);
                fflush(stdout);
                printf("\033[A");
                printf("\033[2K");

        }
        return NULL;
}

void *fifo_reader_thread(void *arg) {
        Worker_Data *wd = (Worker_Data *)arg;
        FILE *fifo = fopen(FIFO_PATH, "r");
        if (!fifo) {
                syslog(LOG_ERR, "Failed to open FIFO %s for reading: %s", FIFO_PATH, strerror(errno));
                exit(EXIT_FAILURE);
        }

        char buf[256];
        while (1) {
                if (fgets(buf, sizeof(buf), fifo)) {
                        syslog(LOG_INFO, "FIFO reader: Received message: %s", buf);
                        parse_daemon_sender_msg(buf);

                        pthread_mutex_lock(&wd->mutex);
                        if (g_config.wp && (!wd->wp || strcmp(g_config.wp, wd->wp) != 0 || g_config.mon != wd->mon || g_config.mode != wd->mode || g_config.maxmem != wd->maxmem)) {
                                if (wd->running) {
                                        syslog(LOG_INFO, "FIFO reader: Stopping existing worker");
                                        wd->stop = 1;
                                        if (wd->mode == MODE_STREAM && wd->td) {
                                                pthread_mutex_lock(&wd->td->threading.mutex);
                                                wd->td->done = 1;
                                                pthread_cond_broadcast(&wd->td->threading.not_empty);
                                                pthread_cond_broadcast(&wd->td->threading.not_full);
                                                pthread_mutex_unlock(&wd->td->threading.mutex);
                                        }
                                        while (wd->running) {
                                                pthread_cond_wait(&wd->cond, &wd->mutex);
                                        }
                                        if (wd->thread) {
                                                pthread_join(wd->thread, NULL);
                                                wd->thread = 0;
                                        }
                                }

                                // Update worker configuration
                                if (wd->wp) free(wd->wp);
                                wd->wp = g_config.wp ? strdup(g_config.wp) : NULL;
                                wd->mon = g_config.mon;
                                wd->mode = g_config.mode;
                                wd->maxmem = g_config.maxmem;
                                wd->stop = 0;

                                if (wd->wp) {
                                        wd->running = 1;
                                        if (pthread_create(&wd->thread, NULL, worker_thread, wd) != 0) {
                                                syslog(LOG_ERR, "Failed to create worker thread");
                                                wd->running = 0;
                                        } else {
                                                syslog(LOG_INFO, "FIFO reader: Started new worker with wp=%s, mon=%d, mode=%d", wd->wp, wd->mon, (int)wd->mode);
                                        }
                                }
                        }
                        pthread_mutex_unlock(&wd->mutex);
                } else {
                        fclose(fifo);
                        fifo = fopen(FIFO_PATH, "r");
                        if (!fifo) {
                                syslog(LOG_ERR, "Failed to reopen FIFO %s: %s", FIFO_PATH, strerror(errno));
                                exit(EXIT_FAILURE);
                        }
                }
        }

        fclose(fifo);
        return NULL;
}

static void cleanup_thread_data(Thread_Data *td) {
        for (int i = 0; i < td->buffer_size; i++) {
                if (td->buffer[i].data) free(td->buffer[i].data);
        }
        free(td->buffer);
        pthread_mutex_destroy(&td->threading.mutex);
        pthread_cond_destroy(&td->threading.not_full);
        pthread_cond_destroy(&td->threading.not_empty);
}

int run_stream(int monitor_index, const char *video_mp4) {
        Worker_Data *wd = pthread_getspecific(worker_data_key);
        syslog(LOG_INFO, "run_stream()");
        Context ctx = {0};
        if (init_context(&ctx, monitor_index, video_mp4) < 0) {
                syslog(LOG_ERR, "init context failed");
                cleanup_context(&ctx);
                return -1;
        }

        // Check if input is a single frame
        int single_frame = is_single_frame(&ctx);
        if (single_frame) {
                // Read and display single frame
                int frame_count = 0;
                while (av_read_frame(ctx.fmt_ctx, ctx.packet) >= 0) {
                        if (ctx.packet->stream_index == ctx.video_stream_idx) {
                                if (avcodec_send_packet(ctx.codec_ctx, ctx.packet) >= 0) {
                                        while (avcodec_receive_frame(ctx.codec_ctx, ctx.frame) >= 0) {
                                                sws_scale(ctx.sws_ctx, (const uint8_t * const *)ctx.frame->data, ctx.frame->linesize, 0, ctx.codec_ctx->height,
                                                          ctx.bgra_frame->data, ctx.bgra_frame->linesize);
                                                if (display_frame(&ctx, ctx.bgra_buffer, ctx.monitor_width, ctx.monitor_height, frame_count) < 0) {
                                                        syslog(LOG_ERR, "Failed to display single frame");
                                                        fprintf(stderr, "Failed to display single frame\n");
                                                } else {
                                                        printf("Displayed single frame\n");
                                                }
                                                frame_count++;
                                                break; // Only process one frame
                                        }
                                }
                        }
                        av_packet_unref(ctx.packet);
                }
                cleanup_context(&ctx);
                return single_frame; // Return 1 to indicate single frame
        }

        // Existing multi-frame logic
        Thread_Data td = {0};
        td.ctx = &ctx;
        td.buffer_size = 2;
        td.buffer = (Image *)malloc(td.buffer_size * sizeof(Image));
        if (!td.buffer) {
                syslog(LOG_ERR, "Failed to allocate thread buffer\n");
                fprintf(stderr, "Failed to allocate thread buffer\n");
                cleanup_context(&ctx);
                return -1;
        }
        for (int i = 0; i < td.buffer_size; i++) {
                td.buffer[i].data = NULL;
                td.buffer[i].size = ctx.bgra_size;
        }
        pthread_mutex_init(&td.threading.mutex, NULL);
        pthread_cond_init(&td.threading.not_full, NULL);
        pthread_cond_init(&td.threading.not_empty, NULL);
        td.done = 0;

        pthread_mutex_lock(&wd->mutex);
        wd->td = &td;
        pthread_mutex_unlock(&wd->mutex);

        pthread_t producer, consumer;
        if (pthread_create(&producer, NULL, producer_thread, &td) != 0) {
                syslog(LOG_ERR, "Failed to create producer thread\n");
                fprintf(stderr, "Failed to create producer thread\n");
                pthread_mutex_lock(&wd->mutex);
                wd->td = NULL;
                pthread_mutex_unlock(&wd->mutex);
                cleanup_thread_data(&td);
                cleanup_context(&ctx);
                return -1;
        }
        if (pthread_create(&consumer, NULL, consumer_thread, &td) != 0) {
                syslog(LOG_ERR, "Failed to create consumer thread\n");
                fprintf(stderr, "Failed to create consumer thread\n");
                td.done = 1;
                pthread_cond_broadcast(&td.threading.not_empty);
                pthread_join(producer, NULL);
                pthread_mutex_lock(&wd->mutex);
                wd->td = NULL;
                pthread_mutex_unlock(&wd->mutex);
                cleanup_thread_data(&td);
                cleanup_context(&ctx);
                return -1;
        }

        pthread_join(producer, NULL);
        pthread_join(consumer, NULL);

        pthread_mutex_lock(&wd->mutex);
        wd->td = NULL;
        pthread_mutex_unlock(&wd->mutex);

        cleanup_thread_data(&td);
        cleanup_context(&ctx);
        return 0; // Multi-frame case
}

static void daemonize(void) {
        pid_t pid, sid;

        // First fork
        if ((pid = fork()) < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
        }
        if (pid > 0) {
                // Parent exits
                exit(EXIT_SUCCESS);
        }

        // Become session leader
        if ((sid = setsid()) < 0) {
                perror("setsid");
                exit(EXIT_FAILURE);
        }

        // Second fork (prevent reacquiring terminal)
        if ((pid = fork()) < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
        }
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }

        // Set file permissions and working dir
        umask(0);
        if (chdir("/") < 0) {
                perror("chdir");
                exit(EXIT_FAILURE);
        }

        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Redirect std streams to /dev/null
        open("/dev/null", O_RDWR); // stdin
        int _ =dup(0);                    // stdout
        _ = dup(0);                    // stderr


        // Open PID file
        g_pid_fd = open(PID_PATH, O_RDWR | O_CREAT, 0640);
        if (g_pid_fd < 0) {
                // Cannot open
                exit(EXIT_FAILURE);
        }

        // Try to lock the file
        if (flock(g_pid_fd, LOCK_EX | LOCK_NB) < 0) {
                // Already locked: another instance running
                perror("flock");
                close(g_pid_fd);
                exit(EXIT_FAILURE);
        }

        // Write PID
        if (ftruncate(g_pid_fd, 0) != 0) {
                perror("ftruncate");
                close(g_pid_fd);
                exit(EXIT_FAILURE);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%d\n", getpid());
        if (write(g_pid_fd, buf, strlen(buf)) < 0) {
                perror("write");
                close(g_pid_fd);
                exit(EXIT_FAILURE);
        }

        // Note: don't close pid_fd — keep it open to hold the lock
}

static void signal_handler(int sig) {
        if (sig == SIGTERM) {
                syslog(LOG_INFO, "Received SIGTERM, shutting down.");
                closelog();

                unlink(PID_PATH);
                unlink(FIFO_PATH);
                if (g_pid_fd >= 0) {
                        close(g_pid_fd);
                }

                exit(0);
        }
}

static void stop_daemon(void) {
        FILE *f = fopen(PID_PATH, "r");
        if (!f) {
                fprintf(stderr, "No daemon running (PID file %s not found)\n", PID_PATH);
                exit(EXIT_FAILURE);
        }

        pid_t pid;
        if (fscanf(f, "%d", &pid) != 1) {
                fclose(f);
                fprintf(stderr, "Failed to read PID from %s\n", PID_PATH);
                exit(EXIT_FAILURE);
        }
        fclose(f);

        if (kill(pid, SIGTERM) < 0) {
                if (errno == ESRCH) {
                        fprintf(stderr, "No daemon running with PID %d\n", pid);
                } else {
                        perror("kill");
                }
                exit(EXIT_FAILURE);
        }

        printf("Sent SIGTERM to daemon with PID %d\n", pid);
        exit(EXIT_SUCCESS);
}

static void usage(void) {
        printf("awx <walpaper_filepath> [options...]\n");
        printf("Options:\n");
        printf("    -%c, --%s[=<flag>|*]      display this message or get help on individual flags or all (*)\n", FLAG_1HY_HELP, FLAG_2HY_HELP);
        printf("    -%c, --%s               start the daemon\n", FLAG_1HY_DAEMON, FLAG_2HY_DAEMON);
        printf("        --%s=<int>            set the display monitor or (-1) to combine all monitors into one single monitor\n", FLAG_2HY_MON);
        printf("        --%s=<stream|load>   set the frame generation mode\n", FLAG_2HY_MODE);
        printf("        --%s=<float>       set a maximum memory limit for --mode=load\n", FLAG_2HY_MAXMEM);
        printf("        --%s                 stop the running the daemon\n", FLAG_2HY_STOP);
}

static void parse_daemon_sender_msg(const char *msg) {
        while (*msg) {
                if (*msg == ' ' || *msg == '\n') {
                        ++msg;
                        continue;
                }
                if (*msg == '-' && msg[1] == '-') {
                        msg += 2;
                        char buf[256] = {0};
                        size_t blen = 0;
                        while (*msg && *msg != ' ' && *msg != '\n' && blen < sizeof(buf) - 1) {
                                buf[blen++] = *msg++;
                        }
                        buf[blen] = '\0';
                        char cmd[256] = {0};
                        size_t cmd_end = 0;
                        int iseq = 0;
                        for (cmd_end = 0; cmd_end < blen; ++cmd_end) {
                                if (buf[cmd_end] == '=' || buf[cmd_end] == ' ') {
                                        iseq = buf[cmd_end] == '=';
                                        break;
                                }
                        }
                        memcpy(cmd, buf, cmd_end);
                        cmd[cmd_end] = '\0';
                        char *rest = &buf[cmd_end + (iseq ? 1 : 0)];
                        // Trim trailing spaces from rest
                        size_t rest_len = strlen(rest);
                        while (rest_len > 0 && rest[rest_len - 1] == ' ') {
                                rest[--rest_len] = '\0';
                        }
                        syslog(LOG_INFO, "cmd: %s", cmd);
                        syslog(LOG_INFO, "rest: %s", rest);
                        if (!strcmp(cmd, "mode")) {
                                if (!iseq) {
                                        syslog(LOG_ERR, "option `%s` requires equals (=)", cmd);
                                        err_wargs("option `%s` requires equals (=)", cmd);
                                }
                                if (!strcmp(rest, "stream")) {
                                        g_config.mode = MODE_STREAM;
                                        syslog(LOG_INFO, "set mode to STREAM %s", cmd);
                                } else if (!strcmp(rest, "load")) {
                                        g_config.mode = MODE_LOAD;
                                        syslog(LOG_INFO, "set mode to LOAD %s", cmd);
                                } else {
                                        syslog(LOG_ERR, "unknown mode `%s`", rest);
                                        err_wargs("unknown mode `%s`", rest);
                                }
                        } else if (!strcmp(cmd, "mon")) {
                                if (!iseq) {
                                        syslog(LOG_ERR, "option `%s` requires equals (=)", cmd);
                                        err_wargs("option `%s` requires equals (=)", cmd);
                                }
                                if (!str_isdigit(rest)) {
                                        syslog(LOG_ERR, "option `%s` expects a number, got `%s`", cmd, rest);
                                        err_wargs("option `%s` expects a number, got `%s`", cmd, rest);
                                }
                                g_config.mon = atoi(rest);
                                syslog(LOG_INFO, "set monitor to %d", g_config.mon);
                        } else {
                                syslog(LOG_ERR, "Unknown option: %s", cmd);
                                err_wargs("Unknown  option: %s", cmd);
                        }
                } else {
                        char buf[256] = {0};
                        size_t blen = 0;
                        while (*msg && *msg != ' ' && *msg != '\n' && blen < sizeof(buf) - 1) {
                                buf[blen++] = *msg++;
                        }
                        buf[blen] = '\0';
                        // Trim trailing spaces from buf
                        while (blen > 0 && buf[blen - 1] == ' ') {
                                buf[--blen] = '\0';
                        }
                        if (g_config.wp) {
                                free(g_config.wp);
                        }
                        g_config.wp = strdup(buf);
                        syslog(LOG_INFO, "fp: %s", g_config.wp ? g_config.wp : "(null)");
                }
        }
}

static void daemon_loop(void) {
        printf("starting daemon, do `tail -f /var/log/syslog` to see logging\n");

        daemonize();
        openlog("awx", LOG_PID | LOG_CONS, LOG_DAEMON);
        signal(SIGTERM, signal_handler);

        unlink(FIFO_PATH);
        if (mkfifo(FIFO_PATH, 0666) < 0) {
                syslog(LOG_ERR, "Failed to create FIFO %s: %s", FIFO_PATH, strerror(errno));
                /* exit(EXIT_FAILURE); */
        }

        init_thread_specific();
        Worker_Data wd;
        init_worker_data(&wd);

        // Apply initial configuration if available
        pthread_mutex_lock(&wd.mutex);
        if (g_config.wp) {
                wd.wp = strdup(g_config.wp);
                wd.mon = g_config.mon;
                wd.mode = g_config.mode;
                wd.maxmem = g_config.maxmem;
                wd.running = 1;
                if (pthread_create(&wd.thread, NULL, worker_thread, &wd) != 0) {
                        syslog(LOG_ERR, "Failed to create initial worker thread");
                        wd.running = 0;
                } else {
                        syslog(LOG_INFO, "Started initial worker with wp=%s, mon=%d, mode=%d", wd.wp, wd.mon, (int)wd.mode);
                }
        }
        pthread_mutex_unlock(&wd.mutex);

        pthread_t fifo_reader;
        if (pthread_create(&fifo_reader, NULL, fifo_reader_thread, &wd) != 0) {
                syslog(LOG_ERR, "Failed to create FIFO reader thread");
                cleanup_worker_data(&wd);
                unlink(FIFO_PATH);
                closelog();
                exit(EXIT_FAILURE);
        }

        pthread_join(fifo_reader, NULL);

        // Cleanup
        pthread_mutex_lock(&wd.mutex);
        if (wd.running) {
                wd.stop = 1;
                if (wd.mode == MODE_STREAM && wd.td) {
                        pthread_mutex_lock(&wd.td->threading.mutex);
                        wd.td->done = 1;
                        pthread_cond_broadcast(&wd.td->threading.not_empty);
                        pthread_cond_broadcast(&wd.td->threading.not_full);
                        pthread_mutex_unlock(&wd.td->threading.mutex);
                }
                while (wd.running) {
                        pthread_cond_wait(&wd.cond, &wd.mutex);
                }
                if (wd.thread) {
                        pthread_join(wd.thread, NULL);
                        wd.thread = 0;
                }
        }
        pthread_mutex_unlock(&wd.mutex);

        cleanup_worker_data(&wd);
        free(g_config.wp);
        unlink(FIFO_PATH);
        closelog();
}

static int daemon_running(void) {
        FILE *f = fopen(PID_PATH, "r");

        if (!f) return 0;

        pid_t pid;

        if (fscanf(f, "%d", &pid) != 1) {
                fclose(f);
                return 0;
        }

        fclose(f);

        return kill(pid, 0) == 0;
}

static int determine_argc(const char *s) {
        int argc = 0;
        for (size_t i = 0; s[i]; ++i) {
                char c = s[i];
                if (c == ' ') ++argc;
        }
        return argc;
}

void send_msg(char **msg, size_t len) {
        dyn_array(char, buf);
        for (size_t i = 0; i < len; ++i) {
                for (size_t j = 0; msg[i][j]; ++j) {
                        dyn_array_append(buf, msg[i][j]);
                }
                if (i != len - 1) {
                        dyn_array_append(buf, ' ');
                }
        }

        // Open FIFO in non-blocking mode
        int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
                if (errno == ENXIO) {
                        syslog(LOG_ERR, "No process is reading from FIFO %s\n", FIFO_PATH);
                        fprintf(stderr, "No process is reading from FIFO %s\n", FIFO_PATH);
                } else {
                        perror("open");
                }
                dyn_array_free(buf);
                return;
        }

        // Convert file descriptor to FILE stream
        FILE *f = fdopen(fd, "w");
        if (!f) {
                perror("fdopen");
                close(fd);
                dyn_array_free(buf);
                return;
        }

        // Write message
        fprintf(f, "%s\n", buf.data);
        fclose(f); // Closes fd as well
        dyn_array_free(buf);
}

int main(int argc, char *argv[]) {
        --argc, ++argv;

        char **orig_argv = argv;
        int orig_argc = argc;

        clap_init(argc, argv);

        Clap_Arg arg = {0};
        while (clap_next(&arg)) {
                if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_HELP) {
                        if (arg.eq) {
                                dump_flag_info(arg.eq);
                        } else {
                                usage();
                        }
                        exit(0);
                } else if (arg.hyphc == 1 && arg.start[0] == FLAG_1HY_DAEMON) {
                        g_config.flags |= FT_DAEMON;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_HELP)) {
                        if (arg.eq) {
                                dump_flag_info(arg.eq);
                        } else {
                                usage();
                        }
                        exit(0);
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_MON)) {
                        if (!arg.eq) {
                                err("--mon expects a value after equals (=)\n");
                        }
                        if (!str_isdigit(arg.eq)) {
                                err_wargs("--mon expects a number, not `%s`\n", arg.eq);
                        }
                        g_config.mon = atoi(arg.eq);
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_MODE)) {
                        if (!arg.eq) {
                                err("--mode expects a value after equals (=)\n");
                        }
                        if (!strcmp(arg.eq, "load")) {
                                g_config.mode = MODE_LOAD;
                        } else if (!strcmp(arg.eq, "stream")) {
                                g_config.mode = MODE_STREAM;
                        } else {
                                err_wargs("--mode expects either `stream` or `load`, not `%s`", arg.eq);
                        }
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_STOP)) {
                        stop_daemon();
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_MAXMEM)) {
                        if (!arg.eq) {
                                err("--maxmem expects a value after equals (=)\n");
                        }
                        if (!str_isdigit(arg.eq)) {
                                err_wargs("--maxmem expects a float, not `%s`\n", arg.eq);
                        }
                        g_config.maxmem = strtod(arg.eq, NULL);
                        g_config.flags |= FT_MAXMEM;
                } else if (arg.hyphc == 2 && !strcmp(arg.start, FLAG_2HY_DAEMON)) {
                        g_config.flags |= FT_DAEMON;
                } else if (arg.hyphc == 0) {
                        if (g_config.wp) {
                                err_wargs("only one wallpaper is allowed, already have: %s", g_config.wp);
                        }
                        g_config.wp = strdup(arg.start);
                } else {
                        err_wargs("unknown option `%s`", arg.start);
                }
        }
        clap_destroy();

        if (g_config.flags & FT_DAEMON) {
                if (daemon_running()) {
                        err("awx daemon is already running");
                }

                printf("Wallpaper filepath: %s\n", g_config.wp);
                printf("Monitor: %d %s\n", g_config.mon, g_config.mon == -1 ? "[Stretch]" : "");
                printf("Mode: %s\n", g_config.mode == MODE_LOAD ? "load" : "stream");
                if (g_config.flags & FT_MAXMEM) {
                        if (g_config.maxmem < 0) {
                                err_wargs("The maximum memory you entered (%f) must be > 0.0", g_config.maxmem);
                        }
                        printf("Maximum Memory Allowed: %fGB\n", g_config.maxmem);
                }

                daemon_loop();
        } else {
                // Sending message
                if (!daemon_running()) {
                        if (!g_config.wp) {
                                err("Wallpaper filepath is not set");
                        }

                        int result = 0;
                        if (g_config.mode == MODE_STREAM) {
                                result = run_stream(g_config.mon, g_config.wp);
                        } else if (g_config.mode == MODE_LOAD) {
                                result = run_load_all(g_config.mon, g_config.wp);
                        }

                        // If single frame and not in daemon mode, exit
                        if (result == 1) {
                                printf("Applied single-frame image, exiting\n");
                                free(g_config.wp);
                                exit(EXIT_SUCCESS);
                        }
                }
                send_msg(orig_argv, orig_argc);
                printf("sent configuration to daemon, applying changes...\n");
        }

        return 0;
}
