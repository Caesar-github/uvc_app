#include <stdio.h>
#include <unistd.h>
#include "uvc_control.h"
#include "mpi_enc.h"

typedef void (*callback_for_uvc)(void *buffer, size_t size);
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
    if (!check_uvc_video_id()) {
        add_uvc_video();
        register_callback_for_uvc(uvc_read_camera_buffer);
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
        while(1) {
            if (cb_for_uvc)
                cb_for_uvc(buffer, size);
            usleep(30000);
       }
      uvc_video_id_exit_all();
      return 0;
    } else {
        printf("not uvc video support\n");
        return -1;
    }
}
