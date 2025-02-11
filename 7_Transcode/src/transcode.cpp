#include "transcode.h"
#include "formatItem.h"

// 输入是原始文件待解码的流
FormatItem::CodecSetting getDecodeSetting(AVStream *stream) {
    FormatItem::CodecSetting decodeSetting;
    decodeSetting.codecParams = stream->codecpar;
    decodeSetting.codecID = stream->codecpar->codec_id;
    decodeSetting.codec_type = stream->codecpar->codec_type;
    decodeSetting.width = stream->codecpar->width;
    decodeSetting.height = stream->codecpar->height;
    decodeSetting.framerate = stream->avg_frame_rate;
    decodeSetting.ch_layout = stream->codecpar->ch_layout;
    decodeSetting.sample_rate = stream->codecpar->sample_rate;
    decodeSetting.format = stream->codecpar->format;
    decodeSetting.time_base = stream->time_base;

    return decodeSetting;
}

// 输入是原始文件待解码的流，编码参数将用于新的转码流
FormatItem::CodecSetting getEncodeSetting(AVStream *oldStream) {
    // 只用于转码，拷贝部分参数
    FormatItem::CodecSetting encodeSetting;
    encodeSetting.codecParams = nullptr;
    encodeSetting.codecID = oldStream->codecpar->codec_id;
    encodeSetting.codec_type = oldStream->codecpar->codec_type;
    encodeSetting.width = oldStream->codecpar->width;
    encodeSetting.height = oldStream->codecpar->height;
    encodeSetting.framerate = oldStream->avg_frame_rate;
    encodeSetting.ch_layout = oldStream->codecpar->ch_layout;
    encodeSetting.sample_rate = oldStream->codecpar->sample_rate;
    encodeSetting.format = oldStream->codecpar->format;
    encodeSetting.time_base = oldStream->time_base;

    return encodeSetting;
}

// 模拟解码后的图像处理方法，处理解码好的帧，处理完后准备重新编码
void handleVideoFrame(AVFrame* inFrame, AVFrame *outFrame) {
    if(inFrame->format != AV_PIX_FMT_YUV420P) {
        return;
    }

    // 模拟后处理解码后的图片的过程，拷贝一份帧
    outFrame->width = inFrame->width;
    outFrame->height = inFrame->height;
    outFrame->format = inFrame->format;
    av_frame_get_buffer(outFrame, 0);
    av_frame_copy(outFrame, inFrame);
    outFrame->pts = inFrame->pts;

    //// 这里先注释掉，如果取消注释，就是写入全0，视频绿色
    // av_frame_make_writable(outFrame);
    //
    // // Y
    // for(int y = 0; y < outFrame->height; ++y) {
    //     for(int x = 0; x < outFrame->width; ++x) {
    //         outFrame->data[0][y * outFrame->linesize[0] + x] = 0;
    //     }
    // }

    // // U & V
    // for(int y = 0; y < outFrame->height/2; ++y) {
    //     for(int x = 0; x < outFrame->width/2; ++x) {
    //         outFrame->data[1][y * outFrame->linesize[1] + x] = 0;
    //         outFrame->data[2][y * outFrame->linesize[2] + x] = 0;
    //     }
    // }

}

void transcode(std::string src, std::string dst) {
    sp<FormatItem> inItem = FormatItem::openInputFormat(src);
    sp<FormatItem> outItem = FormatItem::openOutputFormat(dst);

    if(inItem == nullptr || outItem == nullptr) {
        av_log(NULL, AV_LOG_ERROR, "open input or output item failed.\n");
        return;
    }

    // 从原文件的流信息，创建新的输出流
    // 这里用一个map来记录 输入流id 和 输出流id 的对应关系
    std::map<int, int> streamIdxMap;
    for(int i = 0; i < inItem->fmtCtx->nb_streams; ++i) {
        if(inItem->fmtCtx->streams[i]->codecpar->codec_id == AV_CODEC_ID_NONE) {
            continue;
        }

        AVStream * strm = avformat_new_stream(outItem->fmtCtx, NULL);
        strm->id = outItem->fmtCtx->nb_streams - 1;
        strm->index = outItem->fmtCtx->nb_streams - 1;
        streamIdxMap[i] = strm->index;
        strm->time_base = inItem->fmtCtx->streams[i]->time_base;

        // 打开解码器
        FormatItem::CodecSetting decodeSetting = getDecodeSetting(inItem->fmtCtx->streams[i]);
        inItem->openCodec(i, decodeSetting);

        // 打开编码器
        FormatItem::CodecSetting encodeSetting = getEncodeSetting(inItem->fmtCtx->streams[i]);
        outItem->openCodec(strm->index, encodeSetting);
        
        avcodec_parameters_from_context(strm->codecpar, outItem->codecMap[strm->index]);
        av_log(NULL, AV_LOG_INFO, "src timebase : %d / %d\n", strm->time_base.num, strm->time_base.den);
    }

    AVPacket *inPacket = av_packet_alloc();
    AVFrame *inFrame = av_frame_alloc();
    AVPacket *outPacket = av_packet_alloc();
    AVFrame *outFrame = av_frame_alloc();

    // 写入头部信息
    if(avformat_write_header(outItem->fmtCtx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Write header failed.\n");
        return;
    }

    // 注意，写完header之后，stream的timebase可能会发生改变。
    for(int i = 0; i < outItem->fmtCtx->nb_streams; ++i) {
        auto &strm = outItem->fmtCtx->streams[i];
        av_log(NULL, AV_LOG_INFO, "new timebase : %d / %d\n", strm->time_base.num, strm->time_base.den);
    }

    // 逐帧写入
    while(av_read_frame(inItem->fmtCtx, inPacket) >= 0) {
        if(streamIdxMap.find(inPacket->stream_index) == streamIdxMap.end()) {
            av_packet_unref(inPacket);
            continue;
        }
        if(inItem->codecMap.find(inPacket->stream_index) == inItem->codecMap.end()) {
            av_log(NULL, AV_LOG_ERROR, "Cannt find stream codec\n");
            av_packet_unref(inPacket);
            continue;
        }

        int inStreamId = inPacket->stream_index;
        int outStreamId = streamIdxMap[inPacket->stream_index];
        auto &oldStream = inItem->fmtCtx->streams[inStreamId];
        auto &newStream = outItem->fmtCtx->streams[outStreamId];

        int err = avcodec_send_packet(inItem->codecMap[inPacket->stream_index], inPacket);
        if(err < 0) {
            // 因为尽量消耗解码输出，所以应该不会有EAGAIN，所以这种情况应该无法恢复
            av_log(NULL, AV_LOG_ERROR, "Error when decode send packet.\n");
            return;
        }

        // 送进解码器后可以释放packet了
        av_packet_unref(inPacket);

        // while尽量消耗解码的输出
        while(err >= 0) {
            err = avcodec_receive_frame(inItem->codecMap[inStreamId], inFrame);
            if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            } else if(err < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error when decode receive frame \n");
                // 无法恢复的错误
                return;
            }

            // 模拟解码完后的处理，这里音频直接拷贝不处理，视频过handleVideoFrame处理
            if(oldStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                av_frame_move_ref(outFrame, inFrame);
            } else {    // VIDEO
                handleVideoFrame(inFrame, outFrame);
            }
            av_frame_unref(inFrame);

            outFrame->pict_type = AV_PICTURE_TYPE_NONE;
            outFrame->pts = av_rescale_q(outFrame->pts, oldStream->time_base, newStream->time_base);
            outFrame->time_base = newStream->time_base;

            err = avcodec_send_frame(outItem->codecMap[outStreamId], outFrame);
            if(err < 0) {
                // 因为尽量消耗编码输出，所以应该不会有EAGAIN，这种错误情况应该无法恢复
                av_log(NULL, AV_LOG_ERROR, "Error when encode send frame %d \n", err);
                return;
            }
            // 送入编码器后释放掉frame
            av_frame_unref(outFrame);

            // while尽量消耗编码输出
            while(err >= 0) {
                err = avcodec_receive_packet(outItem->codecMap[outStreamId], outPacket);
                if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                } else if(err < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error when encode receive packet \n");
                    return;
                }

                outPacket->stream_index = outStreamId;

                // 交叉写入音频和视频帧
                if(av_interleaved_write_frame(outItem->fmtCtx, outPacket) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error during write packet\n");
                }
                av_packet_unref(outPacket);
            }
        }
    }

    // 最后需要对所有解码器送NULL，解码完所有的帧
    for(auto &p : inItem->codecMap) {
        auto inStreamId = p.first;
        auto codecCtx = p.second;
        int outStreamId = streamIdxMap[inStreamId];
        auto &oldStream = inItem->fmtCtx->streams[inStreamId];
        auto &newStream = outItem->fmtCtx->streams[outStreamId];

        int err = avcodec_send_packet(codecCtx, NULL);
        if(err < 0) {
            // 因为尽量消耗解码输出，所以应该不会有EAGAIN，所以这种情况应该无法恢复
            av_log(NULL, AV_LOG_ERROR, "Error when decode send NULL.\n");
            return;
        }

        // while尽量消耗解码的输出
        while(err >= 0) {
            err = avcodec_receive_frame(inItem->codecMap[inStreamId], inFrame);
            if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            } else if(err < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error when decode receive frame \n");
                // 无法恢复的错误
                return;
            }

            // 模拟解码完后的处理，这里音频直接拷贝不处理，视频过handleVideoFrame处理
            if(oldStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                av_frame_move_ref(outFrame, inFrame);
            } else {    // VIDEO
                handleVideoFrame(inFrame, outFrame);
            }
            av_frame_unref(inFrame);

            outFrame->pict_type = AV_PICTURE_TYPE_NONE;
            outFrame->pts = av_rescale_q(outFrame->pts, oldStream->time_base, newStream->time_base);
            outFrame->time_base = newStream->time_base;

            err = avcodec_send_frame(outItem->codecMap[outStreamId], outFrame);
            if(err < 0) {
                // 因为尽量消耗编码输出，所以应该不会有EAGAIN，这种错误情况应该无法恢复
                av_log(NULL, AV_LOG_ERROR, "Error when encode send frame 2 \n");
                return;
            }
            // 送入编码器后释放掉frame
            av_frame_unref(outFrame);

            // while尽量消耗编码输出
            while(err >= 0) {
                err = avcodec_receive_packet(outItem->codecMap[outStreamId], outPacket);
                if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                } else if(err < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error when encode receive packet 2 \n");
                    return;
                }

                outPacket->stream_index = outStreamId;

                // 交叉写入音频和视频帧
                if(av_interleaved_write_frame(outItem->fmtCtx, outPacket) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error during write packet\n");
                }
                av_packet_unref(outPacket);
            }
        }
    }

    // 对所有编码器送NULL
    for(auto &p : outItem->codecMap) {
        int outStreamId = p.first;
        auto inStreamId = -1;
        for(auto &p: streamIdxMap) {
            if(p.second == outStreamId) {
                inStreamId = p.first;
            }
        }
        assert(inStreamId != -1);

        auto &oldStream = inItem->fmtCtx->streams[inStreamId];
        auto &newStream = outItem->fmtCtx->streams[outStreamId];

        int err = avcodec_send_frame(outItem->codecMap[outStreamId], NULL);
        if(err < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error when encode send NULL \n");
            return;
        }

        // while尽量消耗编码输出
        while(err >= 0) {
            err = avcodec_receive_packet(outItem->codecMap[outStreamId], outPacket);
            if(err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            } else if(err < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error when encode send frame \n");
                return;
            }

            outPacket->stream_index = outStreamId;

            // 交叉写入音频和视频帧
            if(av_interleaved_write_frame(outItem->fmtCtx, outPacket) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error during write packet\n");
            }
            av_packet_unref(outPacket);
        }
    }

    // 写入尾部数据
    if(av_write_trailer(outItem->fmtCtx) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Write trailer failed\n");
        return;
    }

    av_packet_free(&inPacket);
    av_packet_free(&outPacket);
    av_frame_free(&inFrame);
    av_frame_free(&outFrame);
}