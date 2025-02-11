#include "formatItem.h"

extern "C" {
    #include "libavutil/opt.h"
}

sp<FormatItem> FormatItem::openInputFormat(const std::string& url) {
    sp<FormatItem> formatItem = std::make_shared<FormatItem>();
    formatItem->url = url;

    // 初始化解封装相关的组件
    //   创建AVFormatContext和AVInputFormat
    if(avformat_open_input(&formatItem->fmtCtx, url.c_str(), NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Open src file failed.\n");
        return nullptr;
    }

    // 读取输入多媒体文件的相关信息
    if(avformat_find_stream_info(formatItem->fmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Find stream info failed\n");
        return nullptr;
    }

    return formatItem;
}

sp<FormatItem> FormatItem::openOutputFormat(const std::string& url) {
    sp<FormatItem> formatItem = std::make_shared<FormatItem>();
    formatItem->url = url;

    // 初始化封装相关的组件
    //   初始化AVFormatContext，同步文件名确定AVOutputFormat
    if(avformat_alloc_output_context2(&formatItem->fmtCtx, NULL, NULL, url.c_str()) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Allocate output context failed.\n");
        return nullptr;
    }

    // 打开输出的文件
    if(!(formatItem->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        auto ret = avio_open(&formatItem->fmtCtx->pb, url.c_str(), AVIO_FLAG_WRITE);
        if(ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "IO Open failed.\n");
            return nullptr;
        }
    }

    return formatItem;
}

bool FormatItem::openCodec(int streamid, const CodecSetting &codecSetting) {
    const AVCodec *codec = nullptr;
    // 根据输入还是输出，寻找对应的编解码器AVCodec*
    if(fmtCtx->iformat) {
        codec = avcodec_find_decoder(codecSetting.codecID);
    } else if(fmtCtx->oformat) {
        codec = avcodec_find_encoder(codecSetting.codecID);
    }

    if(codec == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Open Codec failed.\n");
        return false;
    }

    // 创建AVCodecContext, 加入到codecMap中
    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if(codecCtx == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "Allocate Codec ctx failed.\n");
        return false;
    }
    codecMap.insert({streamid, codecCtx});

    if(codecSetting.codecParams) {
        if(avcodec_parameters_to_context(codecCtx, codecSetting.codecParams) < 0) {
            av_log(NULL, AV_LOG_ERROR, "copy parameters to context failed\n");
            return false;
        }
    }

    codecCtx->codec_type = codecSetting.codec_type;
    if(codecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
        codecCtx->pix_fmt = (AVPixelFormat)codecSetting.format;
        codecCtx->width = codecSetting.width;
        codecCtx->height = codecSetting.height;
    } else if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO){
        codecCtx->sample_fmt = (AVSampleFormat)codecSetting.format;
        codecCtx->ch_layout = codecSetting.ch_layout;
        codecCtx->sample_rate = codecSetting.sample_rate;
    }
    
    codecCtx->time_base = codecSetting.time_base;
    codecCtx->framerate = codecSetting.framerate;
    
    if(fmtCtx->oformat && codecCtx->codec->id == AV_CODEC_ID_H264) {
        // codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        // codecCtx->mb_decision = 1;
        codecCtx->bit_rate = 1000000;
        av_opt_set(codecCtx->priv_data, "preset", "slow", 0);
        av_opt_set(codecCtx->priv_data, "profile", "high", 0);
        av_opt_set(codecCtx->priv_data, "level", "5.0", 0);
    }

    if(avcodec_open2(codecCtx, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Open Codec Failed.\n");
        return false;
    }
    
    return true;
}

FormatItem::~FormatItem()
{
    for (auto pair : codecMap) {
        avcodec_close(pair.second);
        avcodec_free_context(&pair.second);
    }

    if (fmtCtx && fmtCtx->iformat) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }
    if (fmtCtx && fmtCtx->oformat) {
        if (fmtCtx->pb) {
            avio_closep(&fmtCtx->pb);
        }
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
    }
    if (fmtCtx) {
        avformat_free_context(fmtCtx);
    }

}