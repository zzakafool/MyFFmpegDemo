cmake_minimum_required (VERSION 3.8)

project(MyFFmpegDemo)

set(LOCAL_PATH ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")

add_subdirectory(1_List_Demuxers)
add_subdirectory(2_Demuxing)
add_subdirectory(3_Remuxing)
add_subdirectory(4_BufferedIO)
add_subdirectory(5_RemuxingTrim)
add_subdirectory(6_Encode)
add_subdirectory(7_Transcode)
file(COPY res DESTINATION ${CMAKE_BINARY_DIR})