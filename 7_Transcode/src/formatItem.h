#pragma once
#include <string>
#include <map>
#include <memory>
#include <functional>

template <typename T>
using sp = std::shared_ptr<T>;

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}



struct FormatItem {
    std::string url;                            // 输入/输出url
    AVFormatContext *fmtCtx = nullptr;          // 输入/输出fmtCtx
    std::map<int, AVCodecContext*> codecMap;    // 编码/解码codecMap，streamid -> codecContext
    
    static sp<FormatItem> openInputFormat(const std::string& url);
    static sp<FormatItem> openOutputFormat(const std::string& url);

    struct CodecSetting {
        // 一般解码器会用到从Stream读到的codecParams
        AVCodecParameters *codecParams = nullptr;

        // 编码器的信息最好手动传入
        enum AVCodecID codecID;
        enum AVMediaType codec_type;
        // video
        int width = 0;
        int height = 0;
        AVRational framerate = {30, 1};
        // audio 
        AVChannelLayout ch_layout;
        int sample_rate = 0;
        // common
        int format = 0;
        AVRational time_base = {1, 30};
        
    };
    bool openCodec(int streamid, const CodecSetting &codecSetting);

    virtual ~FormatItem();
};