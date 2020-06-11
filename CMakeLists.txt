cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(uvc_app)

add_definitions(-DSN_CODE="${SN_CODE}")
add_definitions(-DUSB_UVC_FW_VERSION="${USB_FW_VERSION}")
add_definitions(-DYUYV_AS_RAW)

include_directories(uvc)

set(LIB_SOURCE
    uvc/uvc_configfs.c
    uvc/uvc_gadget.c
    uvc/uvc_video.cpp
    uvc/uvc_encode.cpp
    uvc/yuv.c
    uvc/vpu.c
    uvc/drm.c
)
add_library(rkuvc SHARED ${LIB_SOURCE})

link_libraries("librga.so")
target_link_libraries(rkuvc pthread drm rockchip_mpp)

set(SOURCE
    main.cpp
    ${LIB_SOURCE}
)

ADD_EXECUTABLE(uvc_app ${SOURCE})
target_link_libraries(uvc_app pthread drm rockchip_mpp)

install(TARGETS rkuvc DESTINATION lib)
install(DIRECTORY ./uvc DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(TARGETS uvc_app DESTINATION bin)
install(DIRECTORY . DESTINATION bin
        FILES_MATCHING PATTERN "*.sh"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_WRITE WORLD_EXECUTE)
