#include "swscale.h"

extern "C" {
#include "libavutil/log.h"
#include "libswscale/swscale.h"
#include "libavutil/frame.h"
}

const int FRAME_WIDTH = 640;
const int FRAME_HEIGHT = 360;

// nv12 可以用这条命令播放
// ffplay -pixel_format nv12 -f rawvideo -video_size 640x360 test_pic_640x360.nv12
void readNv12ToAVFrame(AVFrame* &frame, std::string srcNv12) {
    // 申请Frame的存储空间
    frame->width = FRAME_WIDTH;
    frame->height= FRAME_HEIGHT;
    frame->format= AV_PIX_FMT_NV12;

    auto ret = av_frame_get_buffer(frame, 0);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate frame buffer failed\n");
        return;
    }

    ret = av_frame_make_writable(frame);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "av frame make writable failed\n");
        return;
    }
    
    FILE *file = fopen(srcNv12.c_str(), "rb"); // 以二进制模式打开文件
    if (file == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open file");
        return;
    }

    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    fseek(file, 0, SEEK_SET);

    size_t bytes_read = 0;

    // Y分量
    for(int i = 0; i < FRAME_HEIGHT; ++i) {
        fread(frame->data[0] + i * frame->linesize[0], frame->width, 1, file);
        bytes_read += frame->width;
    }
    // UV分量
    for(int i = 0; i < FRAME_HEIGHT / 2; ++i) {
        fread(frame->data[1] + i * frame->linesize[1], frame->width, 1, file);
        bytes_read += frame->width;
    }

    if (bytes_read != length) {
        av_log(NULL, AV_LOG_ERROR, "Failed to read file\n");
        fclose(file);
        return;
    }

    fclose(file);
}

// ffplay -pixel_format rgba -f rawvideo -video_size 1280x720 test_pic_1280x640.rgba
void writeRGBAToFile(const AVFrame *frame, std::string dstRGBA) {
    FILE *file = fopen(dstRGBA.c_str(), "wb");
    if (file == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open rgba file\n");
        return;
    }

    size_t bytes_write = 0;
    for(int i = 0; i < frame->height; ++i) {
        fwrite(frame->data[0] + i * frame->linesize[0], frame->width * 4, 1, file);
        bytes_write += frame->width * 4;
    }

    av_log(NULL, AV_LOG_INFO, "write data : %zu bytes\n", bytes_write);

    fclose(file);
}

// 将nv12的图片转换成rgba格式
void nv12toRGBA(std::string dst, std::string src) {
    SwsContext *swsCtx = sws_getContext(FRAME_WIDTH,      // 原始图像的宽度
                                        FRAME_HEIGHT,     // 原始图像的高度
                                        AV_PIX_FMT_NV12,  // 原始图像的格式
                                        FRAME_WIDTH * 2,  // 目标图像的宽度，这里放大2倍
                                        FRAME_HEIGHT * 2, // 目标图像的高度，放大2倍
                                        AV_PIX_FMT_RGBA,  // 目标图像格式
                                        SWS_BILINEAR,     // 缩放时使用的算法
                                        NULL, NULL, NULL);// 输入图像的滤波信息，输出图像的滤波信息，缩放算法调节参数
    if(swsCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "create sws context failed\n");
    }

    // 申请元数据NV12帧的空间，会从文件中读取数据
    AVFrame *srcFrame = av_frame_alloc();
    if(srcFrame == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "allocate the src frame failed\n");
        return;
    }
    readNv12ToAVFrame(srcFrame, src);

    // 申请待转换的RGBA帧的空间
    AVFrame *dstFrame = av_frame_alloc();
    if(dstFrame == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "allocate the dst frame failed\n");
        return;
    }
    dstFrame->width = 2 * FRAME_WIDTH;
    dstFrame->height = 2 * FRAME_HEIGHT;
    dstFrame->format = AV_PIX_FMT_RGBA;
    auto ret = av_frame_get_buffer(dstFrame, 0);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate frame buffer failed\n");
        return;
    }

    // 转换
    sws_scale(swsCtx,
              srcFrame->data,
              srcFrame->linesize,
              0,
              srcFrame->height,
              dstFrame->data,
              dstFrame->linesize);

    // 将rgba数据写入文件中
    writeRGBAToFile(dstFrame, dst);

    av_frame_free(&srcFrame);
    av_frame_free(&dstFrame);
    sws_freeContext(swsCtx);
}