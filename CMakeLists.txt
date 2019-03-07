cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(uvc_app)

add_definitions(-DCONFIG_USB_CONFIGFS_F_UVC_ROCKCHIP)
include_directories(include ext)

set(SOURCE
    src/main.c
    src/uvc-gadget.c
    src/uvc_video.cpp
    src/yuv.c
    src/uvc_control.c
    src/uvc_encode.cpp
    ext/mediactl.c
    src/rkcam_capture.c
    src/rkcam_control.c
    src/mpi_enc.c
)

#set(CMAKE_C_FLAGS "-static ${CMAKE_C_FLAGS}")

ADD_EXECUTABLE(${PROJECT_NAME} ${SOURCE})
target_link_libraries(${PROJECT_NAME} pthread rkisp drm rockchip_mpp)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
