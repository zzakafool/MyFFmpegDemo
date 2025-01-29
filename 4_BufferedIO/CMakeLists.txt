cmake_minimum_required (VERSION 3.8)

project(4_BufferedIO)

include_directories (
    /usr/local/include/
    src
)

set(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")

# link目录，必须放在add_executable之前
link_directories(
    "/usr/local/lib/"
)

# files
file(GLOB_RECURSE FILE_SRC
	"src/**.h"
	"src/**.cpp"
)

# 将源代码添加到此项目的可执行文件。
add_executable (${PROJECT_NAME}
	${FILE_SRC}
)

# 静态链接库，链接两个静态库
target_link_libraries(${PROJECT_NAME}
            avutil
            avformat
            avdevice
            avfilter
            swresample
            swscale
            postproc
            avcodec
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()
