#include "swscale.h"

int main() {
    nv12toRGBA("./test_pic_1280x640.rgba", "../res/test_pic_640x360.nv12");
    return 0;
}