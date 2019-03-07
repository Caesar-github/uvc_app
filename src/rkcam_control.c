/*
 * V4L2 video capture example
 * AUTHOT : Leo Wen
 * DATA : 2019-02-15
 */
#include "rkcam_capture.h"
#include "rkcam_control.h"
#include "uvc_encode.h"
#include <pthread.h>

struct rkcam_capture {
    int seq;
    int id;
    int fd;
    int width;
    int height;
    struct buffer *buffers;
    void* rkisp_engine;
};

struct rkcam_capture cam_cap;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct uvc_encode uvc_enc;

extern int uvc_video_id_get(unsigned int seq);

int add_rkcam(int id, int width, int height)
{
    int ret = 0;
    pthread_mutex_lock(&lock);
    memset(&cam_cap, 0, sizeof(cam_cap));
    memset(&uvc_enc, 0, sizeof(uvc_enc));
    cam_cap.id = -1;
    cam_cap.seq = -1;
    cam_cap.fd = rkcam_init(id, width, height, &cam_cap.buffers, &cam_cap.rkisp_engine);
    if (cam_cap.fd < 0)
        ret = -1;
    else {
        cam_cap.id = id;
        cam_cap.width = width;
        cam_cap.height = height;
        cam_cap.seq = 0;
        if (uvc_encode_init(&uvc_enc, width, height))
            ret = -1;
    }
    pthread_mutex_unlock(&lock);
    return ret;
}

void remove_rkcam()
{
    pthread_mutex_lock(&lock);
    if (cam_cap.id >= 0) {
        if (rkcam_deinit(cam_cap.fd, &cam_cap.buffers, &cam_cap.rkisp_engine))
            printf("%s rkcam_deinit fail.\n", __func__);
        cam_cap.id = -1;
        cam_cap.fd = -1;
        cam_cap.buffers = NULL;
        cam_cap.rkisp_engine = NULL;
        uvc_encode_exit(&uvc_enc);
    }
    pthread_mutex_unlock(&lock);
}

void read_rkcam()
{
    pthread_mutex_lock(&lock);
    if (cam_cap.id >= 0) {
        uvc_enc.video_id = uvc_video_id_get(cam_cap.seq);
        if (uvc_enc.video_id >= 0 && uvc_enc.src_virt) {
            read_frame(cam_cap.fd, cam_cap.buffers, uvc_enc.src_virt);
            uvc_encode_process(&uvc_enc);
        }
    }
    pthread_mutex_unlock(&lock);
}
