#pragma once
#include <string>

// 会将输入的一张nv12图编码成一段10s的视频
// @param dst 输出文件
// @param srcNv12 输入的一张nv12测试图
void hwencode(std::string dst, std::string srcNv12);