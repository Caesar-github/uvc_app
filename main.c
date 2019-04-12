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

#define BUFFER_WIDTH 640
#define BUFFER_HEIGHT 480

int main(void)
{
    void *buffer;
    size_t size;
    int i = 0;
    FILE *fp;
    char name[] = "/usr/bin/test_image.nv12";
    mpi_enc_set_format(MPP_FMT_YUV420P);
    check_video_id();
    add_uvc_video();
    register_callback_for_uvc(uvc_read_camera_buffer);
    size = 640 * 480 * 3 / 2;
    buffer = malloc(size);
    if (!buffer) {
        printf("buffer alloc fail.\n");
        return -1;
    }
    fp = fopen(name, "rb");
    if (!fp) {
        printf("open %s fail.\n", name);
        memset(buffer, 0, size);
    } else {
        fread(buffer, 1, size, fp);
        fclose(fp);
    }
    while(1) {
        if (cb_for_uvc)
            cb_for_uvc(buffer, size);
        usleep(30000);
    }
    uvc_video_id_exit_all();
    return 0;
}
