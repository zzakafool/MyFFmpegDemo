#include <hwdecode.h>
#include <vector>
#include <sstream>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// 可以使用下面的命令进行显示
// ffplay -pixel_format nv12 -f rawvideo -video_size 640x360 firstPic.nv12
void saveNV12(AVFrame *frame, std::string outfile) {
    // 打开输出文件
    FILE* file = fopen(outfile.c_str(), "wb");
    if (!file) {
        av_log(NULL, AV_LOG_ERROR, "cannot open file");
        return;
    }
    // 写入Y分量
    for (int i = 0; i < frame->height; ++i) {
        fwrite(frame->data[0] + i * frame->linesize[0], 1, frame->width, file);
    }
    // 写入UV分量
    for (int i = 0; i < frame->height / 2; ++i) {
        fwrite(frame->data[1] + i * frame->linesize[1], 1, frame->width, file);
    }
    // 关闭输出文件
    fclose(file);
}

void transferFrameHwToCPU(AVCodecContext *codecCtx, AVFrame *cpuFrame, AVFrame *hwFrame) {
    // 这里是查找后续调用av_hwframe_transfer_data(), 可能返回的软件帧格式
    // 比如对于MAC的videotoolbox，这里可以返回的是nv12
    AVPixelFormat *formats = nullptr;
    if(av_hwframe_transfer_get_formats(codecCtx->hw_frames_ctx, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "hw frame transfer get fromats failed\n");
    }

    // 打印一下所有的拷贝支持的格式
    cpuFrame->format = AV_PIX_FMT_NONE;
    std::stringstream sstrm;
    sstrm << "\ntransfer support format : \n";
    AVPixelFormat *p = formats;
    while(p != nullptr && *p != AV_PIX_FMT_NONE) {
        sstrm << "\t Fmt " << *p << std::endl;
        ++p;
    }
    av_log(NULL, AV_LOG_INFO, "%s", sstrm.str().c_str());

    // 这里也选择第一种
    if(formats) {
        cpuFrame->format = formats[0];
    }

    // 拷贝到cpu
    if(av_hwframe_transfer_data(cpuFrame, hwFrame, 0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "transfer to cpu failed\n");
    }

    av_log(NULL, AV_LOG_INFO, "transfer to cpu success.\n");
}

void hwdecode(std::string url) {
    AVFormatContext *inFmtCtx = nullptr;
    if(avformat_open_input(&inFmtCtx, url.c_str(), NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Open input format failed\n");
        return;
    }

    if(avformat_find_stream_info(inFmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Find stream info failed\n");
        avformat_close_input(&inFmtCtx);
        return;
    }

    int vstreamid = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(vstreamid < 0) {
        avformat_close_input(&inFmtCtx);
        av_log(NULL, AV_LOG_ERROR, "Find video stream error\n");
        return;
    }

    const AVCodec* codec = avcodec_find_decoder(inFmtCtx->streams[vstreamid]->codecpar->codec_id);
    if(codec == nullptr) {
        avformat_close_input(&inFmtCtx);
        av_log(NULL, AV_LOG_ERROR, "Find Codec error\n");
        return;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if(codecCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Cannot alloc Codec context\n");
        avformat_close_input(&inFmtCtx);
        return;
    }

    if(avcodec_parameters_to_context(codecCtx, inFmtCtx->streams[vstreamid]->codecpar) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Fill Codec context failed\n");
        avcodec_free_context(&codecCtx);
        avformat_close_input(&inFmtCtx);
    }
    
    // 获取Codec支持的硬件加速配置情况，这里主要是打印看看
    int hwindex = 0;
    std::vector<const AVCodecHWConfig*> hwconfigs;
    const AVCodecHWConfig* hwconfig = nullptr;
    while((hwconfig = avcodec_get_hw_config(codec, hwindex)) != nullptr) {
        av_log(NULL, AV_LOG_INFO, "HW %d : %d, %d, %d \n", hwindex, hwconfig->pix_fmt, hwconfig->methods, hwconfig->device_type);
        hwconfigs.push_back(hwconfig);
        ++hwindex;
    }

    // 按照第一种硬件加速配置，设置解码器
    if(!hwconfigs.empty()) {
        hwconfig = hwconfigs[0];
        // 创建hwdevicectx
        if(av_hwdevice_ctx_create(&codecCtx->hw_device_ctx, hwconfig->device_type, NULL, NULL, 0) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Create hw device error.\n");
            avcodec_free_context(&codecCtx);
            avformat_close_input(&inFmtCtx);

            return;
        }
        // hwconfigs信息放入私有数据，方便后面回调使用。
        codecCtx->opaque = &hwconfigs;

        // 设置get_format回调，在处理过程中codecCtx回触发，根据其返回的格式
        // 确认是否启用硬解，完成后codecCtx会创建hw_frames_ctx
        codecCtx->get_format = [](AVCodecContext *s, const AVPixelFormat *fmt) -> enum AVPixelFormat {
            if(s->opaque) {
                auto pHwConfigs = static_cast<std::vector<const AVCodecHWConfig*> *>(s->opaque);
                if(!pHwConfigs->empty()) {
                    auto hwconfig = pHwConfigs->at(0);
                    av_log(NULL, AV_LOG_INFO, "get format : hwconfig pix_fmt : %d\n", hwconfig->pix_fmt);
                    return hwconfig->pix_fmt;
                }
            }
            av_log(NULL, AV_LOG_ERROR, "Error state, not found hw config, set YUV420P\n");
            
            return s->sw_pix_fmt ? s->sw_pix_fmt : AV_PIX_FMT_YUV420P;
        };
    }

    // 设置完硬解环境后，open
    if(avcodec_open2(codecCtx, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open codec.\n");

        if(codecCtx->hw_device_ctx) {
            av_buffer_unref(&codecCtx->hw_device_ctx);
        }
        avcodec_free_context(&codecCtx);
        avformat_close_input(&inFmtCtx);
    }

    av_log(NULL, AV_LOG_INFO, "Create hw decoder success");
    
    AVPacket *inPacket = av_packet_alloc();
    AVFrame *inFrame = av_frame_alloc();

    int decodedFrameNum = 0;
    AVFrame *cpuFrame = av_frame_alloc();

    av_log(NULL, AV_LOG_INFO, "Start decoding...\n");
    while(av_read_frame(inFmtCtx, inPacket) >= 0) {
        if(inPacket->stream_index != vstreamid) {
            continue;
        }

        int err = avcodec_send_packet(codecCtx, inPacket);
        if(err < 0) {
            // 因为尽量消耗解码输出，所以应该不会有EAGAIN，所以这种情况应该无法恢复
            av_log(NULL, AV_LOG_ERROR, "Error when decode send packet.\n");
            return;
        }

        while(err >= 0) {
            err = avcodec_receive_frame(codecCtx, inFrame);
            if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            } else if(err < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error when decode receive frame \n");
                // 无法恢复的错误
                return;
            }

            ++decodedFrameNum;
            // 解码出一张Frame
            av_log(NULL, AV_LOG_INFO, "\rdecode %d frame, format : %d", decodedFrameNum, inFrame->format);
            
            // 下面的inFrame还是得到返回的硬件帧，可以看成数据是硬件中某个buffer的索引。
            // inFrame不能直接做数据处理相关的操作，如果需要，需要先拷贝到CPU
            // 只拷贝第一张回CPU做验证
            if(decodedFrameNum == 1) {
                // 从硬件帧拷贝一张到CPU
                transferFrameHwToCPU(codecCtx, cpuFrame, inFrame);
                // 保存一张NV12图片到本地 
                saveNV12(cpuFrame, "firstPic.nv12");
            }
        }
    }

    int err = avcodec_send_packet(codecCtx, NULL);
    if(err < 0) {
        // 因为尽量消耗解码输出，所以应该不会有EAGAIN，所以这种情况应该无法恢复
        av_log(NULL, AV_LOG_ERROR, "Error when decode send packet.\n");
        return;
    }

    while(err >= 0) {
        err = avcodec_receive_frame(codecCtx, inFrame);
        if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
            break;
        } else if(err < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error when decode receive frame \n");
            // 无法恢复的错误
            return;
        }

        // 解码出一张Frame
        av_log(NULL, AV_LOG_INFO, "\rdecode %d frame, format : %d", decodedFrameNum, inFrame->format);
    }

    if(codecCtx->hw_device_ctx) {
        av_buffer_unref(&codecCtx->hw_device_ctx);
    }
    avcodec_free_context(&codecCtx);
    avformat_close_input(&inFmtCtx);
    return;
}