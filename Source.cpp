/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


 /**
  * @file
  * API example for decoding and filtering
  * @example filtering_video.c
  */

#define _XOPEN_SOURCE 600 /* for usleep */
//#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>


extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/pixfmt.h>
    #include <libavutil/timecode.h>
    #include <libavutil/bprint.h>
    #include <libavutil/time.h>
    #include <libswscale/swscale.h>
}

const char* filter_descr = "transpose=cclock";
/* other way:
   scale=78:24 [scl]; [scl] transpose=cclock // assumes "[in]" and "[out]" to be input output pads respectively
 */

static AVFormatContext* fmt_ctx;
AVFormatContext* pFormatCtx = NULL;
static AVCodecContext* dec_ctx;
const AVCodec* codec;
AVCodecContext* c = NULL;
AVCodecContext* pCodecCtx = NULL;
AVPacket* pkt;
uint8_t endcode[] = { 0, 0, 1, 0xb7 };
char* outputFile;
FILE* fout;
AVFilterContext* buffersink_ctx;
AVFilterContext* buffersrc_ctx;
AVFilterContext* buffersrc1_ctx;
AVFilterContext* overlay_ctx;
AVFilterGraph* filter_graph;
static int video_stream_index = -1;
static int64_t last_pts = AV_NOPTS_VALUE;

double PCFreq = 0.0;
__int64 CounterStart = 0;

static void StartCounter()
{
    LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li))
        printf( "QueryPerformanceFrequency failed!\n");

    PCFreq = double(li.QuadPart) / 1000000.0;

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}
static double GetCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart - CounterStart) / PCFreq;
}

static int open_input_file(const char* filename)
{
    const AVCodec* dec;
    int ret;

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index = ret;

    /* create decoding context */
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
        return ret;
    }

    return 0;
}

AVFrame* OpenImage(const char* imageFileName)
{
    
    if (avformat_open_input(&(pFormatCtx), imageFileName, NULL, NULL) != 0)
    {
        printf("Can't open image file '%s'\n", imageFileName);
        return NULL;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Can't find stream\n");
        return NULL;
    }

    av_dump_format(pFormatCtx, 0, imageFileName, false);
    
    int index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    const AVCodec* dec = avcodec_find_decoder(pFormatCtx->streams[index]->codecpar->codec_id);
    pCodecCtx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[index]->codecpar);

    // Find the decoder for the video stream
    const AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (!pCodec)
    {
        printf("Codec not found\n");
        return NULL;
    }

    // Open codec
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("Could not open codec\n");
        return NULL;
    }

    // 
    AVFrame* pFrame;

    pFrame = av_frame_alloc();

    if (!pFrame)
    {
        printf("Can't allocate memory for AVFrame\n");
        return NULL;
    }

    int frameFinished;

    AVPacket packet;
    packet.data = NULL;
    packet.size = 0;

    int framesNumber = 0;



    while (av_read_frame(pFormatCtx, &packet) >= 0)
    {

        if (packet.stream_index == video_stream_index) {
            int ret = avcodec_send_packet(pCodecCtx, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    return NULL;
                }

                pFrame->pts = pFrame->best_effort_timestamp;
                return pFrame;
            }

        }

    }

    return pFrame;
}


static int init_filters(const char* filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* ovrFilter = avfilter_get_by_name("overlay");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        time_base.num, time_base.den,
        dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
        args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    time_base = pFormatCtx->streams[video_stream_index]->time_base;
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        time_base.num, time_base.den,
        pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc1_ctx, buffersrc, "in1",
        args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    snprintf(args, sizeof(args), "x=%d:y=%d",
        120, 120);

    ret = avfilter_graph_create_filter(&overlay_ctx, ovrFilter, "overlay",
        args, NULL, filter_graph);

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
        NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
        AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

   

    avfilter_link(buffersrc_ctx, 0, overlay_ctx, 0);
    avfilter_link(buffersrc1_ctx, 0, overlay_ctx, 1);
    avfilter_link(overlay_ctx, 0, buffersink_ctx, 0);

  

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

static void display_frame(const AVFrame* frame, AVRational time_base)
{
    int x, y;
    uint8_t* p0, * p;
    int64_t delay;

    if (frame->pts != AV_NOPTS_VALUE) {
        if (last_pts != AV_NOPTS_VALUE) {
            /* sleep roughly the right amount of time;
             * usleep is in microseconds, just like AV_TIME_BASE. */
            AVRational time_base_q;
            time_base_q.num = 1;
            time_base_q.den = AV_TIME_BASE;
            delay = av_rescale_q(frame->pts - last_pts,
                time_base, time_base_q);
            if (delay > 0 && delay < 1000000)
            {
                StartCounter();
                while (GetCounter() < delay);
            }
                // Sleep(1000);//std::this_thread::sleep_for(std::chrono::microseconds(x));//usleep(delay);
        }
        last_pts = frame->pts;
    }

    /* Trivial ASCII grayscale display. */
    p0 = frame->data[0];
    puts("\033c");
    for (y = 0; y < frame->height; y++) {
        p = p0;
        for (x = 0; x < frame->width; x++)
            putchar(" .-+#"[*(p++) / 52]);
        putchar('\n');
        p0 += frame->linesize[0];
    }
    fflush(stdout);
}

static int initEncoder()
{


    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name("mpeg1video");
    if (!codec) {
        fprintf(stderr, "Codec MPEG-4 not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    printf("%d %d %d %d %d %d %d %d %d\n", dec_ctx->bit_rate, dec_ctx->width, dec_ctx->height, dec_ctx->framerate.num,
        dec_ctx->framerate.den, dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->gop_size, dec_ctx->max_b_frames);

    /* put sample parameters */
    c->bit_rate = dec_ctx->bit_rate;
    /* resolution must be a multiple of two */
    c->width = dec_ctx->width;
    c->height = dec_ctx->height;
    /* frames per second */
    AVRational fr;
    fr.num = 25;
    fr.den = 1;
    c->time_base = fmt_ctx->streams[video_stream_index]->time_base;
    c->framerate = fmt_ctx->streams[video_stream_index]->avg_frame_rate;

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = dec_ctx->gop_size;
    c->max_b_frames = dec_ctx->max_b_frames;
    c->pix_fmt =  dec_ctx->pix_fmt;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
   int ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: \n");
        exit(1);
    }

    return ret;

}

static void encode(AVCodecContext* enc_ctx, AVFrame* frame, AVPacket* pkt,
    FILE* outfile)
{
    int ret;
    printf("\nhello encoder");
    /* send the frame to the encoder */
    //if (frame)
        //printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
       // exit(1);
    }

    printf("\nhello encoder1");

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            fprintf(stderr, "Error encoding a frame for encoding\n");
          //  return;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
           // exit(1);
        }

        printf("Write packet  (size=%5d)\n", pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv)
{
   
    int ret;
    AVPacket* packet;
    AVFrame* frame;
    AVFrame* ImgFrame;
    AVFrame* filt_frame;
   
    if (argc < 4) {
        fprintf(stderr, "Usage: file input_video input_image output_video\n");
        exit(1);
    }

    char* inputVideo = argv[1];
    char* inputImage = argv[2];
    char* outputVideo = argv[3];

    

    frame = av_frame_alloc();
    filt_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !filt_frame || !packet) {
        fprintf(stderr, "Could not allocate frame or packet\n");
        exit(1);
    }


    if ((ret = open_input_file(inputVideo)) < 0)
        goto end;

    ImgFrame = OpenImage(inputImage);

    if ((ret = init_filters(filter_descr)) < 0)
        goto end;
    if ((ret = initEncoder()) < 0)
        goto end;

    outputFile = outputVideo;

    fout = fopen(outputFile, "wb");
    if (!fout) {
        fprintf(stderr, "Could not open %s\n", outputFile);
        exit(1);
    }


    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(fmt_ctx, packet)) < 0)
            break;

        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(dec_ctx, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                frame->pts = frame->best_effort_timestamp;
                ImgFrame->pts = frame->best_effort_timestamp;

                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc1_ctx, ImgFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;

                    filt_frame->pts = frame->pts;

                    printf(" %d %d %d %d", frame->width, frame->height, filt_frame->width, filt_frame->height);
                   // display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
                    ret = av_frame_make_writable(filt_frame);
                    if (ret < 0)
                        exit(1);

                    /* encode the image */
                    encode(c, filt_frame, pkt, fout);

                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }

    /* flush the encoder */
    encode(c, NULL, pkt, fout);

    /* Add sequence end code to have a real MPEG file.
       It makes only sense because this tiny examples writes packets
       directly. This is called "elementary stream" and only works for some
       codecs. To create a valid file, you usually need to write packets
       into a proper file format or protocol; see muxing.c.
     */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), fout);
    fclose(fout);

end:
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&c);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    av_packet_free(&packet);
    av_packet_free(&pkt);

    if (ret < 0 && ret != AVERROR_EOF) {
       // fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }

    exit(0);
}
