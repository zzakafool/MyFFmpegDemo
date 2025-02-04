#include "RemuxingTrim.h"
#include <map>

extern "C" {
#include <libavformat/avformat.h>
}

// 主要用于记录每一路流的首帧时间
struct PacketTime {
    int64_t dts = 0;
    int64_t pts = 0;
};

// 用于记录转封装时路的映射信息
struct StreamMapItem {
    int srcStreamId = -1;
    int dstStreamId = -1;
    PacketTime firstPacketTime;
    bool isFirstPkt = true;
};

// src : 输入的url
// dst : 输出的url
// startTimeMs : 裁剪的起始时间，单位(ms), 如果<=0则从视频第一帧开始
// endTimeMs : 裁剪的结束时间，单位(ms), 如果<=0则直到视频最后一帧结束
void remuxingTrim(std::string src, std::string dst, int64_t startTimeMs, int64_t endTimeMs) {
    // 初始化解封装相关的组件
    //   创建AVFormatContext和AVInputFormat
    AVFormatContext *inFmtCtx = nullptr;
    if(avformat_open_input(&inFmtCtx, src.c_str(), NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Open src file failed.");
        return;
    }

    // 读取输入多媒体文件的相关信息
    if(avformat_find_stream_info(inFmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Find stream info failed");
        avformat_close_input(&inFmtCtx);
        return;
    }

    // 有输入裁剪起始时间，需要进行seek
    if(startTimeMs > 0) {
        if(av_seek_frame(inFmtCtx, -1, av_rescale_q(startTimeMs, {1, 1000}, AV_TIME_BASE_Q), AVSEEK_FLAG_BACKWARD) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Seek to startTime : %lld failed", startTimeMs);
        }
    }

    // 初始化一个AVPacket，用于逐帧读入
    AVPacket *inPacket = av_packet_alloc();
    if(inPacket == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate Packet failed.\n");
        avformat_close_input(&inFmtCtx);
        return;
    }
    
    // 初始化封装相关的组件
    //   初始化AVFormatContext，同步文件名确定AVOutputFormat
    AVFormatContext *outFmtCtx = nullptr;
    if(avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, dst.c_str()) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Allocate output context failed.\n");
        av_packet_free(&inPacket);
        avformat_close_input(&inFmtCtx);
        return;
    }

    // 打开输出的文件
    if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        auto ret = avio_open(&outFmtCtx->pb, dst.c_str(), AVIO_FLAG_WRITE);
        if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "IO Open failed.\n");
        }
    }

    // 从原文件的流信息，创建新的输出流
    // 这里用一个map来记录 输入流id 和 输出流id 的对应关系
    std::map<int, StreamMapItem> streamIdxMap;
    for(int i = 0; i < inFmtCtx->nb_streams; ++i) {
        if(inFmtCtx->streams[i]->codecpar->codec_id == AV_CODEC_ID_NONE) {
            continue;
        }
        AVStream * strm = avformat_new_stream(outFmtCtx, NULL);
        strm->id = outFmtCtx->nb_streams - 1;
        strm->index = outFmtCtx->nb_streams - 1;

        StreamMapItem mapItem;
        mapItem.srcStreamId = i;
        mapItem.dstStreamId = strm->index;
        streamIdxMap[i] = mapItem;
        avcodec_parameters_copy(strm->codecpar, inFmtCtx->streams[i]->codecpar);
        strm->time_base = inFmtCtx->streams[i]->time_base;
        av_log(NULL, AV_LOG_INFO, "src timebase : %d / %d\n", strm->time_base.num, strm->time_base.den);
    }

    // 写入头部信息
    if(avformat_write_header(outFmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Write header failed.\n");
        av_packet_free(&inPacket);
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        return;
    }

    // 注意，写完header之后，stream的timebase可能会发生改变。
    for(int i = 0; i < outFmtCtx->nb_streams; ++i) {
        auto &strm = outFmtCtx->streams[i];
        av_log(NULL, AV_LOG_INFO, "new timebase : %d / %d\n", strm->time_base.num, strm->time_base.den);
    }

    // 逐帧写入
    while(av_read_frame(inFmtCtx, inPacket) >= 0) {
        if(streamIdxMap.find(inPacket->stream_index) == streamIdxMap.end()) {
            av_packet_unref(inPacket);
            continue;
        }

        // 记录每一路流的第一帧时间，用于裁剪
        if(streamIdxMap[inPacket->stream_index].isFirstPkt) {
            streamIdxMap[inPacket->stream_index].firstPacketTime.dts = inPacket->dts;
            streamIdxMap[inPacket->stream_index].firstPacketTime.pts = inPacket->pts;
            streamIdxMap[inPacket->stream_index].isFirstPkt = false;
        }

        // av_log(NULL, AV_LOG_INFO, "read a packet\n");
        auto &oldStream = inFmtCtx->streams[inPacket->stream_index];
        auto &newStream = outFmtCtx->streams[streamIdxMap[inPacket->stream_index].dstStreamId];

        // 裁剪逻辑 -----------------------------------------------------
        //   裁剪结束判断，兼容B帧的处理，直接用dts判断
        if(endTimeMs > 0 && inPacket->dts > av_rescale_q(endTimeMs, {1, 1000}, oldStream->time_base)) {
            break;
        }
        //   兼容B帧视频，pts大于结束时间的视频，写入文件，但是pts置为AV_NOPTS_VALUE，不播放
        //   一般只有最后一个gop有一两帧，不这么处理，某些视频可能会花屏
        if(endTimeMs > 0 && inPacket->pts > av_rescale_q(endTimeMs, {1, 1000}, oldStream->time_base)) {
            inPacket->pts = AV_NOPTS_VALUE;
        }

        // 重新修改时间戳，由于去掉了前面一段，所以需要根据裁剪的第一帧重新计算dts和pts
        //   --- 注意这里，由于dts < pts，dts 要保留首帧的dts和首帧的pts的偏移量
        inPacket->dts = inPacket->pts - streamIdxMap[inPacket->stream_index].firstPacketTime.pts 
                        + streamIdxMap[inPacket->stream_index].firstPacketTime.dts - streamIdxMap[inPacket->stream_index].firstPacketTime.pts; 
        inPacket->pts = inPacket->pts - streamIdxMap[inPacket->stream_index].firstPacketTime.pts;
        // ------------------------------------------------------------

        // 注意，这里上面提到了，在avformat_write_header()之后，流所使用的
        // time_base可能会发生改变，所以，对于帧要写入的dts，pts，都需要进行一个转换
        inPacket->dts = av_rescale_q(inPacket->dts, oldStream->time_base, newStream->time_base);
        inPacket->pts = av_rescale_q(inPacket->pts, oldStream->time_base, newStream->time_base);
        inPacket->stream_index = streamIdxMap[inPacket->stream_index].dstStreamId;
        inPacket->time_base = newStream->time_base;

        // 交叉写入音频和视频帧
        if(av_interleaved_write_frame(outFmtCtx, inPacket) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error during write packet\n");
        }
        av_packet_unref(inPacket);
    }

    // 写入尾部数据
    if(av_write_trailer(outFmtCtx) < 0) {
        av_packet_free(&inPacket);
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        av_log(NULL, AV_LOG_ERROR, "Write trailer failed\n");
        return;
    }

    // 释放相关资源
    avio_closep(&outFmtCtx->pb);
    av_packet_free(&inPacket);
    avformat_close_input(&inFmtCtx);
    avformat_free_context(outFmtCtx);
}