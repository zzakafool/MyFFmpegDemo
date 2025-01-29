#include "bufferedIO.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}

const int AVIO_BUFFER_SIZE = 4096;

struct InputBufferData {
    uint8_t *ptr = nullptr;
    int size = 0;
    int pos = 0;
};

int buffer_read_packet(void *opaque, uint8_t *buf, int buf_size) {
    InputBufferData *ibd = (InputBufferData *)opaque;
    // 超出则返回eof, 0
    if(ibd->pos >= ibd->size) {
        return 0;
    }

    // 只能读到末尾，防止超出
    int read_len = buf_size;
    if(ibd->pos + read_len >= ibd->size) {
        read_len = ibd->size - ibd->pos;
    }

    memcpy(buf, ibd->ptr + ibd->pos, read_len);
    ibd->pos += read_len;

    return read_len;
}

void bufferedIO(uint8_t *buffer, int size) {
    av_log(NULL, AV_LOG_INFO, "buffer size : %d\n", size);
    // 将输入的文件buffer放入结构体中保存
    InputBufferData ibd = {
        .ptr = buffer,
        .size = size,
    };

    // 创建AVFormatContext
    AVFormatContext *fmtCtx = nullptr;
    fmtCtx = avformat_alloc_context();
    if(fmtCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate avformat context failed");
        return;
    }

    // 创建AVIOContext用到的IO缓存区
    unsigned char* avio_ctx_buffer = (unsigned char *)av_malloc(AVIO_BUFFER_SIZE);
    // 创建AVIOContext，并设置自定义的buffer_read_packet
    AVIOContext *ioCtx = avio_alloc_context(avio_ctx_buffer, AVIO_BUFFER_SIZE, 0, &ibd, buffer_read_packet, NULL, NULL);
    if(ioCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate AVIOContext failed");
        avformat_free_context(fmtCtx);
        return;
    }
    // 设置给AVFormatContext
    fmtCtx->pb = ioCtx;

    // 剩下的就是解封装流程
    if(avformat_open_input(&fmtCtx, NULL, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "AVFormat open input failed");
        avio_context_free(&ioCtx);
        avformat_free_context(fmtCtx);
        return;
    }

    // 解析流信息
    if(avformat_find_stream_info(fmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Find stream info failed");
        avio_context_free(&ioCtx);
        avformat_close_input(&fmtCtx);
    }

    AVPacket *packet = av_packet_alloc();
    if(packet == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate Packet failed.\n");
        avio_context_free(&ioCtx);
        avformat_close_input(&fmtCtx);
        return;
    }

    // 统计帧数
    int packet_num = 0;
    while(av_read_frame(fmtCtx, packet) >= 0) {
        ++packet_num;
        // do something for compressed data.
        av_packet_unref(packet);
    }
    av_log(NULL, AV_LOG_INFO, "Total %d packets\n", packet_num);

    av_packet_free(&packet);
    avio_context_free(&ioCtx);
    avformat_close_input(&fmtCtx);
}