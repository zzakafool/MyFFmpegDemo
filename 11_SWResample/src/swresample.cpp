#include "swresample.h"
#include "stdio.h"
extern "C" {
#include "libswresample/swresample.h"
#include "libavutil/log.h"
}

const int IN_CHANNEL_NB = 2;
const int OUT_CHANNEL_NB = 1;
const int IN_SAMPLE_RATE = 48000;
const int OUT_SAMPLE_RATE = 16000;
const int IN_SAMPLE_SIZE = 2;
const int OUT_SAMPLE_SIZE = 2;

const int IN_SAMPLES_PER_CHANNEL = 1024;
const int OUT_SAMPLES_PER_CHANNEL = 1024;

// 输入播放 : ffplay -ar 48000 -ac 2 -f s16le -i test_audio.pcm
// 输出播放 : ffplay -ar 16000 -ac 1 -f s16le -i output.pcm
void resample(const std::string &dst, const std::string &src) {
    SwrContext *swrCtx = nullptr;
    AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_MONO;
    AVChannelLayout in_ch = AV_CHANNEL_LAYOUT_STEREO;
    int ret = swr_alloc_set_opts2(&swrCtx,
                                  &out_ch,              // 输出的声音布局
                                  AV_SAMPLE_FMT_S16,    // 输出的音频采样位数
                                  OUT_SAMPLE_RATE,      // 输出的采样率
                                  &in_ch,               // 输入的声音布局
                                  AV_SAMPLE_FMT_S16,    // 输入的音频采样位数
                                  IN_SAMPLE_RATE,       // 输入的采样率
                                  0, NULL);             // 日志相关
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "allocate the swrcontext failed\n");
        return;
    }

    ret = swr_init(swrCtx);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "init swrctx failed\n");
        return;
    }

    FILE *srcFile = fopen(src.c_str(), "rb");
    FILE *dstFile = fopen(dst.c_str(), "wb");
    if(srcFile == nullptr || dstFile == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "open file failed\n");
        return;
    }

    // 以下是计算buffer的大小，我们每次从输入中读取1024个采样点（每条声道）-------------
    // 每次读取1024个采样点
    // 一共是 1024 * 2(ch) * 2(bytes) = 4096bytes 
    const int IN_BUF_LEN = IN_SAMPLES_PER_CHANNEL * IN_SAMPLE_SIZE * IN_CHANNEL_NB;
    uint8_t *inBuf = (uint8_t *)malloc(IN_BUF_LEN);

    // 每次最多输出1024个采样点（因为循环中输入也是每次只取1024个）
    // 一共是 1024 * 1(ch) * 2(bytes) = 2048bytes
    const int OUT_BUF_LEN = OUT_SAMPLES_PER_CHANNEL * OUT_SAMPLE_SIZE * OUT_CHANNEL_NB;
    uint8_t *outBuf = (uint8_t *)malloc(OUT_BUF_LEN);
    // ------------------------------------------------------------------------

    int bytes_all_read = 0;
    int bytes_all_write = 0;
    // 循环，每次读取一定的采样点数做转换
    while(true) {
        // 读取完文件，退出，实际上SwrCtx中可能还有部分缓存的数据没输出
        if(feof(srcFile) != 0) {
            break;
        }
        // 每次最多读取IN_BUF_LEN大小，即最多4096个字节
        // 实际不一定是完整的4096个字节，每次实际读取的大小用bytes_read
        int bytes_read = fread(inBuf, 1, IN_BUF_LEN, srcFile);
        bytes_all_read += bytes_read;
        // 每次实际读取的采样点数，一般是4096 / 2 / 2 = 1024个
        // 在文件尾部时，可能读取的数量不满足4096的倍数
        int read_samples_per_channel = bytes_read / IN_CHANNEL_NB / IN_SAMPLE_SIZE;

        const uint8_t *p = inBuf;
        int convert_sampels = swr_convert(swrCtx, &outBuf, OUT_SAMPLES_PER_CHANNEL, &p, read_samples_per_channel);
        if(convert_sampels > 0) {
            int write_size = convert_sampels * OUT_CHANNEL_NB * OUT_SAMPLE_SIZE;
            int bytes_write = fwrite(outBuf, 1, write_size, dstFile);
            bytes_all_write += bytes_write;
        }
    }

    // flush, 为swr_convert的input设置NULL和0
    int convert_sampels = swr_convert(swrCtx, &outBuf, OUT_SAMPLES_PER_CHANNEL, NULL, 0);
    if(convert_sampels > 0) {
        int write_size = convert_sampels * OUT_CHANNEL_NB * OUT_SAMPLE_SIZE;
        int bytes_write = fwrite(outBuf, 1, write_size, dstFile);
        bytes_all_write += bytes_write;
    }

    av_log(NULL, AV_LOG_INFO, "bytes read : %d, bytes write : %d\n", bytes_all_read, bytes_all_write);

    swr_free(&swrCtx);
    free(inBuf);
    free(outBuf);

    return;
}