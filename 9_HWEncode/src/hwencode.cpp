#include "hwencode.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
    #include <libavutil/hwcontext.h>
}

const int FRAME_WIDTH = 640;
const int FRAME_HEIGHT = 360;

const int TEST_FRAME_SIZE = 30 * 10;

// 用读到的nv12数据，初始化软件帧
void initSWFrame(AVFrame *swFrame, uint8_t *imgDataBuf, int bufSize) {

    swFrame->width = FRAME_WIDTH;
    swFrame->height= FRAME_HEIGHT;
    swFrame->format= AV_PIX_FMT_NV12;

    auto ret = av_frame_get_buffer(swFrame, 0);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate frame buffer failed\n");
        return;
    }

    ret = av_frame_make_writable(swFrame);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "av frame make writable failed\n");
        return;
    }

    int offset = 0;
    // 写入Y分量
    for (int i = 0; i < swFrame->height; ++i) {
        memcpy(swFrame->data[0] + i * swFrame->linesize[0], imgDataBuf + offset, swFrame->width);
        offset += swFrame->width;
    }
    // 写入UV分量
    for (int i = 0; i < swFrame->height / 2; ++i) {
        memcpy(swFrame->data[1] + i * swFrame->linesize[1], imgDataBuf + offset, swFrame->width);
        offset += swFrame->width;
    }

    // 应该正好符合大小
    assert(offset == bufSize);
}

// 读取输入的一张nv12图做测试数据
void readNv12ToBuf(uint8_t *&buf, int &length, std::string srcNv12) {
    FILE *file = fopen(srcNv12.c_str(), "rb"); // 以二进制模式打开文件
    if (file == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open file");
    }

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);

    buf = (uint8_t *)malloc(length);
    if (buf == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate memory");
        fclose(file);
        return;
    }

    size_t bytes_read = fread(buf, 1, length, file);
    if (bytes_read != length) {
        perror("Failed to read file");
        free(buf);
        fclose(file);
        return;
    }

    fclose(file);
}

void hwencode(std::string dst, std::string srcNv12) {
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

    // 需要显式指定h264_videotoolbox, hevc_videotoolbox(h265)
    // 如果用avcodec_find_encoder(AV_CODEC_ID_H264)的方式默认返回软件实现，如x264
    auto codec = avcodec_find_encoder_by_name("h264_videotoolbox");
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
    // codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    av_opt_set(codecCtx->priv_data, "preset", "slow", 0);
    av_opt_set(codecCtx->priv_data, "profile", "main", 0);
    av_opt_set(codecCtx->priv_data, "level", "5.0", 0);

    // 获取Codec支持的硬件加速配置情况，这里主要是打印看看
    // int hwindex = 0;
    // std::vector<const AVCodecHWConfig*> hwconfigs;
    // const AVCodecHWConfig* hwconfig = nullptr;

    // while((hwconfig = avcodec_get_hw_config(codec, hwindex)) != nullptr) {
    //     av_log(NULL, AV_LOG_INFO, "HW %d : %d, %d, %d \n", hwindex, hwconfig->pix_fmt, hwconfig->methods, hwconfig->device_type);
    //     hwconfigs.push_back(hwconfig);
    //     ++hwindex;
    // }

    // **这里实测AVCodecHWConfig找不到VideoToolBox的编码器配置,所以上面注释掉**
    // 但还是可以手动设定和使用

    // 创建hwdevicectx
    if(av_hwdevice_ctx_create(&codecCtx->hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Create hw device error.\n");

        return;
    }

    // 获取AVHWFramesContext的限制要求
    auto hwFramesConstraints = av_hwdevice_get_hwframe_constraints(codecCtx->hw_device_ctx, NULL);
    if(hwFramesConstraints == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Cannot get hwframes constraints.\n");
        return;
    }

    // 创建成功，设置对应的AVHWFramesContext
    if(codecCtx->hw_device_ctx) {
        codecCtx->hw_frames_ctx = av_hwframe_ctx_alloc(codecCtx->hw_device_ctx);
        if(codecCtx->hw_frames_ctx == nullptr) {
            av_log(NULL, AV_LOG_ERROR, "Allocate hw frames ctx error.\n");

            return;
        }

        // 设置inital_pool_size
        auto hwFramesCtx = reinterpret_cast<AVHWFramesContext *>(codecCtx->hw_frames_ctx->data);
        if(hwFramesConstraints->min_width > FRAME_WIDTH || hwFramesConstraints->max_width < FRAME_WIDTH) {
            av_log(NULL, AV_LOG_ERROR, "frames width is not suitable \n");
        }
        if(hwFramesConstraints->min_height > FRAME_HEIGHT || hwFramesConstraints->max_height < FRAME_HEIGHT) {
            av_log(NULL, AV_LOG_ERROR, "frames height is not suitable \n");
        }
        hwFramesCtx->width = FRAME_WIDTH;
        hwFramesCtx->height = FRAME_HEIGHT;
        hwFramesCtx->initial_pool_size = 30;
            
        // 设置pixelformat
        for(int i = 0; hwFramesConstraints->valid_hw_formats[i] != AV_PIX_FMT_NONE; ++i) {
            // 打印一下
            av_log(NULL, AV_LOG_INFO,"Valid HWFormats %d : %d\n", i, hwFramesConstraints->valid_hw_formats[i]);
            if(hwFramesConstraints->valid_hw_formats[i] == AV_PIX_FMT_VIDEOTOOLBOX) {
                hwFramesCtx->format = hwFramesConstraints->valid_hw_formats[i];
                codecCtx->pix_fmt = hwFramesCtx->format;
            }
        }
            
        for(int i = 0; hwFramesConstraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; ++i) {
            // 打印一下
            av_log(NULL, AV_LOG_INFO,"Valid SWFormats %d : %d\n", i, hwFramesConstraints->valid_sw_formats[i]);
            if(hwFramesConstraints->valid_sw_formats[i] == AV_PIX_FMT_NV12) {
                hwFramesCtx->sw_format = hwFramesConstraints->valid_sw_formats[i];
                codecCtx->sw_pix_fmt = hwFramesCtx->sw_format;
            }
        }

        if(av_hwframe_ctx_init(codecCtx->hw_frames_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Init hw frames ctx error.\n");
            return;
        }
    }

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

    // 读取输入的nv12图片
    uint8_t *nv12Buf = nullptr;
    int bufSize = 0;
    readNv12ToBuf(nv12Buf, bufSize, srcNv12);

    // write header
    ret = avformat_write_header(fmtCtx, NULL);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "write header failed\n");
        return;
    }

    // 软件帧
    AVFrame *swFrame = av_frame_alloc();
    initSWFrame(swFrame, nv12Buf, bufSize);

    AVPacket *packet = av_packet_alloc();

    for(int i = 0;i < TEST_FRAME_SIZE; ++i) {
        // 每次都申请一张hwFrame
        AVFrame *hwFrame = av_frame_alloc();
        if(av_hwframe_get_buffer(codecCtx->hw_frames_ctx, hwFrame, 0) < 0) {
            av_log(NULL, AV_LOG_ERROR, "get a hwFrame error\n");
            return;
        }
        // 将帧数据传给设备
        if(av_hwframe_transfer_data(hwFrame, swFrame, 0) < 0) {
            av_log(NULL, AV_LOG_ERROR, "transfer data to hw failed\n");
            return;
        }

        hwFrame->pts = i;
        int ret = avcodec_send_frame(codecCtx, hwFrame);
        if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "send hwFrame failed\n");
            return;
        }
        av_frame_unref(hwFrame);
        av_frame_free(&hwFrame);

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
    av_frame_free(&swFrame);
    av_packet_free(&packet);
    avformat_free_context(fmtCtx);
}