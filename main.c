#include <stdio.h>
#include <unistd.h>
#include "uvc_control.h"
#include "uvc_video.h"
#include "mpi_enc.h"

typedef void (*callback_for_uvc)(const void *buffer, size_t size,
                                 void* extra_data, size_t extra_size);
callback_for_uvc cb_for_uvc = NULL;
void register_callback_for_uvc(callback_for_uvc cb)
{
    cb_for_uvc = cb;
}

int main(int argc, char* argv[])
{
    char *buffer;
    size_t size;
    int i = 0;
    int width, height;
    int y, uv;
    int extra_cnt = 0;
    if (argc != 3) {
        printf("Usage: uvc_app width height\n");
        printf("e.g. uvc_app 640 480\n");
        return -1;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    if (width == 0 || height == 0) {
        printf("Usage: uvc_app width height\n");
        printf("e.g. uvc_app 640 480\n");
        return -1;
    }

    size = width * height * 3 / 2;
    buffer = (char*)malloc(size);
    if (!buffer) {
        printf("buffer alloc fail.\n");
        return -1;
    }
    y = width * height / 4;
    memset(buffer, 128, y);
    memset(buffer + y, 64, y);
    memset(buffer + y * 2, 128, y);
    memset(buffer + y * 3, 192, y);
    uv = width * height / 8;
    memset(buffer + y * 4, 0, uv);
    memset(buffer + y * 4 + uv, 64, uv);
    memset(buffer + y * 4 + uv * 2, 128, uv);
    memset(buffer + y * 4 + uv * 3, 192, uv);

    register_callback_for_uvc(uvc_read_camera_buffer);
    uvc_control_run();
    while(1) {
        extra_cnt++;
        if (cb_for_uvc)
            cb_for_uvc(buffer, size, &extra_cnt, sizeof(extra_cnt));
        usleep(30000);
    }
    uvc_control_join();
    return 0;
}
