#include "bufferedIO.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>

int main() {
    // 打开测试资源
    int fd = open("../res/big_buck_bunny.mp4", O_RDONLY);
    if(fd < 0) {
        std::cerr << "Open file failed" <<std::endl;
        return -1;
    }

    // 获取文件长度
    int len = lseek(fd, 0, SEEK_END);
    if(len == -1) {
        std::cerr << "Get file len failed" <<std::endl;
        return -2;
    }

    // 映射至内存中
    uint8_t* buf = nullptr;
    buf = (uint8_t *)mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    if(buf == nullptr) {
        std::cerr << "Mmap file failed" << std::endl;
        return -3;
    }

    bufferedIO(buf, len);

    // 解除映射
    if(munmap(buf, len) < 0) {
        std::cerr << "Munmap file failed" << std::endl;
        return -4;
    }

    return 0;
}