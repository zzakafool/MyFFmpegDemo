#include "demuxing.h"

extern "C" {
#include <libavformat/avformat.h>
}

void demuxing(const std::string &url) {
    AVFormatContext *fmtCtx = nullptr;
    if(avformat_open_input(&fmtCtx, url.c_str(), NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Open source file failed.\n");
        return;
    }

    if(avformat_find_stream_info(fmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Find Stream info failed.\n");
        avformat_close_input(&fmtCtx);
        return;
    }

    AVPacket *packet = av_packet_alloc();
    if(packet == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate Packet failed.\n");
        avformat_close_input(&fmtCtx);
        return;
    }

    int packet_num = 0;
    while(av_read_frame(fmtCtx, packet) >= 0) {
        ++packet_num;
        // do something for compressed data.
        av_packet_unref(packet);
    }
    av_log(NULL, AV_LOG_INFO, "Total %d packets\n", packet_num);

    av_packet_free(&packet);
    avformat_close_input(&fmtCtx);
}