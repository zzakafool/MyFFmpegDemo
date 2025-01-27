#include "list_demuxers.h"
#include <sstream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

#define GET_FLAG(flags, BITNAME) (std::string((flags & BITNAME) ? #BITNAME : ""))
#define ADD_FLAG(res, str) do{res = ((str.empty() ? res : (res.empty() ? str : (res + " | " + str)))); } while(0);

inline std::string get_flags_str(const int &flags) {
    std::string res;
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NOFILE));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NEEDNUMBER));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_SHOW_IDS));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NOTIMESTAMPS));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_GENERIC_INDEX));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_TS_DISCONT));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NOBINSEARCH));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NOGENSEARCH));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_NO_BYTE_SEEK));
    ADD_FLAG(res, GET_FLAG(flags, AVFMT_SEEK_TO_PTS));

    return res;
}

void printInputFormat(const AVInputFormat* inputFormat) {
    std::stringstream strstream;
    
    strstream << "---------------- InputFormat ----------------" << std::endl;
    if(inputFormat->name && strlen(inputFormat->name)) {
        strstream << " name : " << inputFormat->name << std::endl;
    }
    if(inputFormat->long_name && strlen(inputFormat->long_name)) {
        strstream << " long name : " << inputFormat->long_name << std::endl;
    }
    if(inputFormat->mime_type && strlen(inputFormat->mime_type)) {
        strstream << " mime type : " << inputFormat->mime_type << std::endl;
    }
    strstream << " flags : " << get_flags_str(inputFormat->flags) << std::endl;
    strstream << "---------------------------------------------" << std::endl;

    std::string str = strstream.str();
    av_log(NULL, AV_LOG_INFO, "%s", str.c_str());
}

void list_demuxers() {
    void *state = nullptr;
    const AVInputFormat *inputFormat = nullptr;

    while((inputFormat = av_demuxer_iterate(&state)) != nullptr) {
        printInputFormat(inputFormat);
    }
}