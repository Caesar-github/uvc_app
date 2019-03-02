cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(uvc_app)

add_definitions(-DCONFIG_USB_CONFIGFS_F_UVC_ROCKCHIP)

set(SOURCE
    src/main.c
    src/uvc-gadget.c
    src/uvc_video.cpp
    src/yuv.c
    src/uvc_control.c
)

#set(CMAKE_C_FLAGS "-static ${CMAKE_C_FLAGS}")

ADD_EXECUTABLE(${PROJECT_NAME} ${SOURCE})
target_link_libraries(${PROJECT_NAME} pthread)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
