#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h> // Added for av_image_get_buffer_size and av_image_fill_arrays
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
        uint8_t *data; // RGB data
        int width, height; // Frame dimensions
        int size; // Size of data (width * height * 3 for RGB)
} Image;

void dump_img(const Image *img) {
        printf("Image: %p\n", img);
        printf("  data=%p\n", img->data);
        printf("  width=%d\n", img->width);
        printf("  height=%d\n", img->height);
        printf("  size=%d\n", img->size);
}

int main(int argc, char *argv[]) {
        if (argc < 2) {
                fprintf(stderr, "Usage: %s <input.mp4>\n", argv[0]);
                return -1;
        }

        // Initialize FFmpeg
        avformat_network_init();

        // Open video file
        AVFormatContext *fmt_ctx = NULL;
        if (avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0) {
                fprintf(stderr, "Could not open video file\n");
                return -1;
        }

        // Find stream info
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
                fprintf(stderr, "Could not find stream info\n");
                avformat_close_input(&fmt_ctx);
                return -1;
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
                return -1;
        }

        // Get codec parameters
        AVCodecParameters *codec_par = fmt_ctx->streams[video_stream_idx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
        if (!codec) {
                fprintf(stderr, "Decoder not found\n");
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        // Initialize codec context
        AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(codec_ctx, codec_par) < 0) {
                fprintf(stderr, "Failed to copy codec parameters\n");
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
                fprintf(stderr, "Could not open codec\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        // Initialize scaling context for RGB conversion
        struct SwsContext *sws_ctx = sws_getContext(
                                                    codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                    codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
                                                    SWS_BILINEAR, NULL, NULL, NULL
                                                    );
        if (!sws_ctx) {
                fprintf(stderr, "Could not initialize swscale context\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        // Allocate frame and packet
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgb_frame = av_frame_alloc();
        AVPacket *packet = av_packet_alloc(); // Modern packet allocation
        if (!frame || !rgb_frame || !packet) {
                fprintf(stderr, "Memory allocation failed\n");
                av_frame_free(&frame);
                av_frame_free(&rgb_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }

        // Allocate RGB buffer
        int rgb_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
        uint8_t *rgb_buffer = av_malloc(rgb_size * sizeof(uint8_t));
        if (!rgb_buffer) {
                fprintf(stderr, "Failed to allocate RGB buffer\n");
                av_frame_free(&frame);
                av_frame_free(&rgb_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return -1;
        }
        av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer, AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);

        // Calculate frame interval for 30 FPS
        double frame_interval = 1.0 / 30.0; // Seconds per frame
        double video_time_base = av_q2d(fmt_ctx->streams[video_stream_idx]->time_base);
        int64_t frame_duration = frame_interval / video_time_base;
        int64_t next_pts = 0;

        // Store images in memory
        Image *images = malloc(1000 * sizeof(Image)); // Adjust size as needed
        int image_count = 0;

        while (av_read_frame(fmt_ctx, packet) >= 0) {
                if (packet->stream_index == video_stream_idx) {
                        // Decode packet
                        if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                                        // Check if this frame is at the desired time (30 FPS)
                                        if (frame->pts >= next_pts) {
                                                // Convert to RGB
                                                sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, codec_ctx->height,
                                                          rgb_frame->data, rgb_frame->linesize);

                                                // Store in memory
                                                Image *img = &images[image_count];
                                                img->width = codec_ctx->width;
                                                img->height = codec_ctx->height;
                                                img->size = rgb_size;
                                                img->data = malloc(rgb_size);
                                                if (!img->data) {
                                                        fprintf(stderr, "Failed to allocate image data\n");
                                                        break;
                                                }
                                                memcpy(img->data, rgb_buffer, rgb_size);
                                                image_count++;

                                                // Update next PTS for 30 FPS
                                                next_pts += frame_duration;
                                                dump_img(img);
                                        }
                                }
                        }
                }
                av_packet_unref(packet);
        }

        // Print summary
        printf("Extracted %d frames at %dx%d (RGB)\n", image_count, codec_ctx->width, codec_ctx->height);

        // Cleanup
        for (int i = 0; i < image_count; i++) {
                free(images[i].data);
        }
        free(images);
        av_free(rgb_buffer);
        av_frame_free(&frame);
        av_frame_free(&rgb_frame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);

        return 0;
}
