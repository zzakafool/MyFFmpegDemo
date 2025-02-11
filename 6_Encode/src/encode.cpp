#include "encode.h"
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
}

const int FRAME_WIDTH = 640;
const int FRAME_HEIGHT = 320;

const int TEST_FRAME_SIZE = 30 * 10;

void setDefaultColor(AVFrame *frame) {
    // Y
    for(int y = 0; y < frame->height; ++y) {
        for(int x = 0; x < frame->width; ++x) {
            frame->data[0][y * frame->linesize[0] + x] = 0;
        }
    }

    // U & V
    for(int y = 0; y < frame->height/2; ++y) {
        for(int x = 0; x < frame->width/2; ++x) {
            frame->data[1][y * frame->linesize[1] + x] = 0;
            frame->data[2][y * frame->linesize[2] + x] = 0;
        }
    }
}

void encode(std::string dst) {
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_alloc_output_context2(&fmtCtx, NULL, NULL, dst.c_str());
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate output format context failed\n");
        return;
    }

    ret = avio_open(&fmtCtx->pb, dst.c_str(), AVIO_FLAG_WRITE);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avio open failed\n");
        return;
    }

    AVStream* strm = avformat_new_stream(fmtCtx, nullptr);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "add stream failed\n");
        return;
    }
    strm->time_base = AVRational{1, 30};
    strm->avg_frame_rate = AVRational{30, 0};

    auto codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(codec == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find codec h264\n");
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if(codecCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate context\n");
        return;
    }

    codecCtx->bit_rate = 100000;
    codecCtx->width = FRAME_WIDTH;
    codecCtx->height = FRAME_HEIGHT;
    codecCtx->time_base = AVRational{1, 30};
    codecCtx->framerate = AVRational{30, 0};
    codecCtx->gop_size = 12;
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    av_opt_set(codecCtx->priv_data, "preset", "slow", 0);
    av_opt_set(codecCtx->priv_data, "profile", "high", 0);
    av_opt_set(codecCtx->priv_data, "level", "5.0", 0);

    ret = avcodec_open2(codecCtx, codec, NULL);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "codec open failed\n");
        return;
    }

    ret = avcodec_parameters_from_context(strm->codecpar, codecCtx);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Copy parameters from context failed\n");
        return;
    }

    // write header
    ret = avformat_write_header(fmtCtx, NULL);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "write header failed\n");
        return;
    }

    AVFrame *frame = av_frame_alloc();
    frame->width = FRAME_WIDTH;
    frame->height= FRAME_HEIGHT;
    frame->format= AV_PIX_FMT_YUV420P;

    AVPacket *packet = av_packet_alloc();

    ret = av_frame_get_buffer(frame, 0);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate frame buffer failed\n");
        return;
    }

    ret = av_frame_make_writable(frame);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "av frame make writable failed\n");
        return;
    }

    // 绿色
    setDefaultColor(frame);

    for(int i = 0;i < TEST_FRAME_SIZE; ++i) {
        frame->pts = i;
        int ret = avcodec_send_frame(codecCtx, frame);
        if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "send frame failed\n");
            return;
        }

        while(ret >= 0) {
            ret = avcodec_receive_packet(codecCtx, packet);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if(ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "receive packet failed\n");
                return;
            }

            packet->stream_index = strm->index;
            packet->dts = av_rescale_q(packet->dts, AVRational{1, 30}, strm->time_base);
            packet->pts = av_rescale_q(packet->pts, AVRational{1, 30}, strm->time_base);
            
            ret = av_interleaved_write_frame(fmtCtx, packet);
            if(ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "write frame failed\n");
                return;
            }

            av_packet_unref(packet);
        }
    }

    ret = avcodec_send_frame(codecCtx, NULL);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "send NULL failed\n");
        return;
    }

    while(ret >= 0) {
        ret = avcodec_receive_packet(codecCtx, packet);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "receive packet failed\n");
            return;
        }

        packet->stream_index = strm->index;
        packet->dts = av_rescale_q(packet->dts, AVRational{1, 30}, strm->time_base);
        packet->pts = av_rescale_q(packet->pts, AVRational{1, 30}, strm->time_base);

        ret = av_interleaved_write_frame(fmtCtx, packet);
        if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "write frame failed\n");
            return;
        }

        av_packet_unref(packet);
    }

    ret = av_write_trailer(fmtCtx);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "write trailer failed\n");
        return;
    }

    avcodec_close(codecCtx);
    avio_closep(&fmtCtx->pb);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_free_context(fmtCtx);
}