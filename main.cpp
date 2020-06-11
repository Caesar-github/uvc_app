#include <stdio.h>                                                                                                                       
#include <stdlib.h>
#include <unistd.h>
#include "uvc_encode.h"
#include "uvc_api.h"

struct camera_info {
    int id;
    int width;
    int height;
};

static pthread_t depth_test_id = 0;
static pthread_t rgb_test_id = 0;
static pthread_t ir_test_id = 0;


static int uvc_read_camera_buffer(struct uvc_encode *uvc_enc, void *buf,
                                  int fd, int width, int height, int id)
{
    int in_fmt;
    int ret = -1;

    in_fmt = RK_FORMAT_YCbCr_420_SP;

    uvc_enc->user_data = NULL;
    uvc_enc->user_size = 0;
    uvc_enc->video_id = uvc_video_id_get(id);
    uvc_enc->frame_size_off = 0;
    if (uvc_enc->video_id < 0)
        return ret;

    ret = uvc_pre_process(uvc_enc, width, height, buf, fd, in_fmt);
    if (ret == 0) {
        ret = uvc_encode_process(uvc_enc);
        if (ret) { 
            ret = uvc_write_process(uvc_enc);
        }
    }

    return ret;
}

static void *uvc_test_thread(void *arg)
{
    struct camera_info *cam_info = (struct camera_info *)arg;
    struct video_drm src_buf;
    struct uvc_encode uvc_enc;
    char *buffer;
    int width, height;
    int y, uv;

    printf("uvc_test: id:%d, %d x %d \n", cam_info->id,
           cam_info->width, cam_info->height);

    width = cam_info->width;
    height = cam_info->height;
    video_drm_alloc(&src_buf, width, height, 3, 2);
    buffer = (char*)src_buf.buffer;

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

    if (uvc_encode_init(&uvc_enc))
        abort();

    while (1) {
        uvc_read_camera_buffer(&uvc_enc, buffer, src_buf.handle_fd,
                               width, height, cam_info->id);
        usleep(30000);
    }

    uvc_encode_exit(&uvc_enc);
    pthread_exit(NULL);
}

static int uvc_test_create_thread(pthread_t *pthread_id, int id,
	int width, int height)
{
    struct camera_info *cam_info = new camera_info[3];

    cam_info[id].id = id;
    cam_info[id].width = width;
    cam_info[id].height = height;

    if (pthread_create(pthread_id, NULL, uvc_test_thread, &(cam_info[id])))
         return -1;

    return 0;
}

static void uvc_test_join_thread(pthread_t pthread_id)
{
    if (pthread_id) {
        pthread_join(pthread_id, NULL);
        pthread_id = 0;
    }
}

int main(int argc, char* argv[])
{
    int width, height;

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

    uvc_pthread_create();

    uvc_test_create_thread(&depth_test_id, get_uvc_depth_cnt(), width, height);
    uvc_test_create_thread(&rgb_test_id, get_uvc_rgb_cnt(), width, height);
    uvc_test_create_thread(&ir_test_id, get_uvc_ir_cnt(), width, height);

    while(1);

    uvc_test_join_thread(depth_test_id);
    uvc_test_join_thread(rgb_test_id);
    uvc_test_join_thread(ir_test_id);

    uvc_pthread_exit();
    return 0;
}
