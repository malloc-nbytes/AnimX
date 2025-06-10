#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <Imlib2.h>
#include <stdio.h>
#include <stdlib.h>

// Error handling macro
#define CHECK_ERR(x, msg) do { if ((x) < 0) { fprintf(stderr, msg "\n"); exit(1); } } while (0)

int main(int argc, char *argv[]) {
        if (argc < 2) {
                fprintf(stderr, "Usage: %s <input_mp4>\n", argv[0]);
                return 1;
        }

        // Open input file
        AVFormatContext *fmt_ctx = NULL;
        CHECK_ERR(avformat_open_input(&fmt_ctx, argv[1], NULL, NULL), "Failed to open input file");

        // Find stream info
        CHECK_ERR(avformat_find_stream_info(fmt_ctx, NULL), "Failed to find stream info");

        // Find video stream
        int video_stream = -1;
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
                if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                        video_stream = i;
                        break;
                }
        }
        if (video_stream == -1) {
                fprintf(stderr, "No video stream found\n");
                avformat_close_input(&fmt_ctx);
                return 1;
        }

        // Get codec parameters
        AVCodecParameters *codec_par = fmt_ctx->streams[video_stream]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codec_par->codec_id);
        if (!codec) {
                fprintf(stderr, "Decoder not found\n");
                avformat_close_input(&fmt_ctx);
                return 1;
        }

        // Initialize codec context
        AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
        CHECK_ERR(avcodec_parameters_to_context(codec_ctx, codec_par), "Failed to copy codec parameters");
        CHECK_ERR(avcodec_open2(codec_ctx, codec, NULL), "Failed to open codec");

        // Initialize Swscale for RGBA conversion (changed from RGB24 to RGBA)
        struct SwsContext *sws_ctx = sws_getContext(
                                                    codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                    codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGBA,
                                                    SWS_BILINEAR, NULL, NULL, NULL
                                                    );
        if (!sws_ctx) {
                fprintf(stderr, "Failed to initialize swscale context\n");
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return 1;
        }

        // Allocate frame and packet
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgba_frame = av_frame_alloc();
        AVPacket *packet = av_packet_alloc();
        if (!frame || !rgba_frame || !packet) {
                fprintf(stderr, "Failed to allocate frame or packet\n");
                av_frame_free(&frame);
                av_frame_free(&rgba_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return 1;
        }

        // Allocate RGBA frame buffer
        int rgba_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height, 1);
        uint8_t *rgba_buffer = av_malloc(rgba_buffer_size);
        if (!rgba_buffer) {
                fprintf(stderr, "Failed to allocate RGBA buffer\n");
                av_frame_free(&frame);
                av_frame_free(&rgba_frame);
                av_packet_free(&packet);
                sws_freeContext(sws_ctx);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                return 1;
        }
        CHECK_ERR(av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer, AV_PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height, 1),
                  "Failed to fill RGBA frame arrays");

        // Initialize Imlib2
        imlib_set_cache_size(0); // Disable disk cache
        Imlib_Image imlib_image;

        // Read and process frames
        while (av_read_frame(fmt_ctx, packet) >= 0) {
                if (packet->stream_index == video_stream) {
                        // Decode frame
                        CHECK_ERR(avcodec_send_packet(codec_ctx, packet), "Error sending packet");
                        int ret = avcodec_receive_frame(codec_ctx, frame);
                        if (ret == 0) {
                                // Convert to RGBA
                                sws_scale(sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height,
                                          rgba_frame->data, rgba_frame->linesize);

                                // Create Imlib2 image from RGBA data
                                imlib_image = imlib_create_image_using_data(codec_ctx->width, codec_ctx->height, (DATA32 *)rgba_frame->data[0]);
                                if (!imlib_image) {
                                        fprintf(stderr, "Failed to create Imlib2 image for frame %ld\n", codec_ctx->frame_num);
                                        av_packet_unref(packet);
                                        continue;
                                }

                                // Example: Print frame info
                                printf("Processed frame %ld\n", codec_ctx->frame_num);

                                // Free Imlib2 image
                                imlib_free_image();
                        }
                }
                av_packet_unref(packet);
        }

        // Clean up
        av_frame_free(&frame);
        av_frame_free(&rgba_frame);
        av_packet_free(&packet);
        av_free(rgba_buffer);
        sws_freeContext(sws_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);

        return 0;
}
