cmake_minimum_required(VERSION 3.21)

project(ModernPlayer)


file(GLOB SOURCES *.cpp *.h)

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES} qrc.qrc)

find_package(spdlog CONFIG REQUIRED)
find_package(Qt5  CONFIG COMPONENTS REQUIRED Core Gui Widgets Network Multimedia)
find_package(Boost CONFIG COMPONENTS REQUIRED thread)
find_package(SoundTouch REQUIRED CONFIG)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        /usr/include/ffmpeg install/include
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Boost::thread spdlog::spdlog
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Multimedia
        avformat avcodec avutil swscale SoundTouch::SoundTouch
        swresample avdevice avfilter postproc avformat yuv GL
)
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE install/lib)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Werror=return-type
        -Wall
        -Wextra
)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        AUTOMOC ON
        AUTORCC ON
)
