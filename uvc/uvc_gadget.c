/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * UVC protocol handling
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>

#include "uvc_gadget.h"
#include "uvc_video.h"
#include "uvc_api.h"

/* forward declarations */
static int uvc_depth_cnt;
static int uvc_rgb_cnt;
static int uvc_ir_cnt;

static bool cif_enable = false;
static bool cif_vga_enable = false; 
static bool cif_depth_ir_start = false;
static int g_cif_cnt = 0;

/* uvc run thread */
static pthread_t run_id = 0;
static bool run_flag;
static pthread_cond_t run_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t run_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;

/* callbacks */
void (*uvc_video_open_cb)(void *);
void (*uvc_video_close_cb)(void *);
void (*uvc_video_set_time_cb)(unsigned short, int) = NULL;
void (*uvc_video_cif_set_gain_cb)(int) = NULL;
void (*uvc_video_get_flt_cb)(uint8_t, uint8_t) = NULL;
int (*uvc_video_addvideo_cb)(AddVideoParam_t *) = NULL;
int (*uvc_video_deletevideo_cb)(int) = NULL; 
void (*uvc_video_deinit_cb)(bool) = NULL;
/*void (*uvc_video_set_frame_type_cb)(enum disp_frame_type) = NULL;*/
void (*uvc_video_set_iq_mode_cb)(int) = NULL;
void (*uvc_video_set_focus_pos_cb)(int) = NULL;
void (*uvc_video_set_image_effect_cb)(int) = NULL;
void (*uvc_video_stream_control_cb)(int, int) = NULL;
void (*uvc_camera_control_cb)(int flag) = NULL;
void (*uvc_iq_get_data_cb)(unsigned char *, unsigned char *, int) = NULL;
void (*uvc_iq_set_data_cb)(unsigned char *, int) = NULL;
void (*uvc_hue_set_cb)(short, void *) = NULL;
short (*uvc_hue_get_cb)(void *) = NULL;
void (*uvc_sat_data_cb)(void *, unsigned short, void *) = NULL;
void (*uvc_set_one_frame_liveness_cb)(void) = NULL;
void (*uvc_set_continuous_liveness_cb)(int) = NULL;
void (*uvc_set_frame_output_cb)(int) = NULL;
void (*uvc_set_pro_time_cb)(unsigned int flag) = NULL;
void (*uvc_set_pro_current_cb)(unsigned int flag) = NULL;
void (*uvc_set_denoise_cb)(unsigned int center, unsigned int enhance) = NULL;
void (*uvc_write_eeprom_cb)(void) = NULL;
void (*uvc_ov_set_hflip_cb)(int val) = NULL;
void (*uvc_ov_set_vflip_cb)(int val) = NULL;

void *hue_set_device = NULL;
void *hue_get_device = NULL;
void *sat_device = NULL;
void *ctrl_device = NULL;

int get_uvc_depth_cnt(void)
{
    return uvc_depth_cnt;
}

int get_uvc_ir_cnt(void)
{
    return uvc_ir_cnt;
}

int get_uvc_rgb_cnt(void)
{
    return uvc_rgb_cnt;
}

bool get_cif_vga_enable(void)
{
    return cif_vga_enable;
}

bool get_cif_ir_depth_enable(void)
{
    printf("%s :%d\n", __func__, cif_depth_ir_start);

    return cif_depth_ir_start;
}

static void uvc_pthread_signal(void)
{
    pthread_mutex_lock(&run_mutex);
    pthread_cond_signal(&run_cond);
    pthread_mutex_unlock(&run_mutex);
} 

/* ---------------------------------------------------------------------------
 * UVC generic stuff
 */

static int
uvc_video_set_format(struct uvc_device *dev)
{
    struct v4l2_format fmt;
    int ret;

    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = dev->width;
    fmt.fmt.pix.height = dev->height;
    fmt.fmt.pix.pixelformat = dev->fcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (dev->fcc == V4L2_PIX_FMT_MJPEG)
        fmt.fmt.pix.sizeimage = dev->width * dev->height * 3 / 2;
    if (dev->fcc == V4L2_PIX_FMT_H264)
        fmt.fmt.pix.sizeimage = dev->width * dev->height * 3 / 2;

    ret = ioctl(dev->uvc_fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        printf("UVC: Unable to set format %s (%d).\n",
               strerror(errno), errno);
        return ret;
    }

    printf("UVC: Setting format to: %c%c%c%c %ux%u\n",
           pixfmtstr(dev->fcc), dev->width, dev->height);

    return 0;
}

static int
uvc_video_stream(struct uvc_device *dev, int enable)
{
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int ret;

    if (!enable) {
        ret = ioctl(dev->uvc_fd, VIDIOC_STREAMOFF, &type);
        if (ret < 0) {
            printf("UVC: VIDIOC_STREAMOFF failed: %s (%d).\n",
                   strerror(errno), errno);
            return ret;
        }

        printf("UVC: Stopping video stream.\n");

        return 0;
    }

    ret = ioctl(dev->uvc_fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        printf("UVC: Unable to start streaming %s (%d).\n",
               strerror(errno), errno);
        return ret;
    }

    printf("UVC: Starting video stream.\n");

    dev->uvc_shutdown_requested = 0;

    return 0;
}

static int
uvc_uninit_device(struct uvc_device *dev)
{
    unsigned int i;
    int ret;

    switch (dev->io) {
    case IO_METHOD_MMAP:
        for (i = 0; i < dev->nbufs; ++i) {
            ret = munmap(dev->mem[i].start, dev->mem[i].length);
            if (ret < 0) {
                printf("UVC: munmap failed\n");
                return ret;
            }
        }

        free(dev->mem);
        break;

    case IO_METHOD_USERPTR:
    default:
        if (dev->run_standalone) {
            for (i = 0; i < dev->nbufs; ++i)
                free(dev->dummy_buf[i].start);

            free(dev->dummy_buf);
        }
        break;
    }

    return 0;
}

static int
uvc_open(struct uvc_device **uvc, char *devname)
{
    struct uvc_device *dev;
    struct v4l2_capability cap;
    int fd;
    int ret = -EINVAL;

    fd = open(devname, O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        printf("UVC: device open failed: %s (%d).\n",
               strerror(errno), errno);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        printf("UVC: unable to query uvc device: %s (%d)\n",
               strerror(errno), errno);
        goto err;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        printf("UVC: %s is no video output device\n", devname);
        goto err;
    }

    dev = calloc(1, sizeof * dev);
    if (dev == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    printf("uvc device is %s on bus %s\n", cap.card, cap.bus_info);
    printf("uvc open succeeded, file descriptor = %d\n", fd);

    dev->uvc_fd = fd;

    dev->brightness_val = PU_BRIGHTNESS_DEFAULT_VAL;
    dev->contrast_val = PU_CONTRAST_DEFAULT_VAL;
    dev->hue_val = PU_HUE_DEFAULT_VAL;
    dev->saturation_val = PU_SATURATION_DEFAULT_VAL;
    dev->sharpness_val = PU_SHARPNESS_DEFAULT_VAL;
    dev->gamma_val = PU_GAMMA_DEFAULT_VAL;
    dev->white_balance_temperature_val = PU_WHITE_BALANCE_TEMPERATURE_DEFAULT_VAL;
    dev->gain_val = PU_GAIN_DEFAULT_VAL;
    dev->hue_auto_val = PU_HUE_AUTO_DEFAULT_VAL;
    dev->power_line_frequency_val = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;

    pthread_mutex_init(&dev->sat_mutex, NULL);
    pthread_cond_init(&dev->sat_cond, NULL);
    *uvc = dev;

    return 0;

err:
    close(fd);
    return ret;
}

static void saturation_clear(struct uvc_device *dev)
{
    memset(&dev->sat, 0, sizeof(dev->sat));
    pthread_mutex_lock(&dev->sat_mutex);
    pthread_cond_signal(&dev->sat_cond);
    pthread_mutex_unlock(&dev->sat_mutex);
}

static void
uvc_close(struct uvc_device *dev)
{
    saturation_clear(dev);
    pthread_mutex_destroy(&dev->sat_mutex);
    pthread_cond_destroy(&dev->sat_cond);
    close(dev->uvc_fd);
    free(dev);
}

/* ---------------------------------------------------------------------------
 * UVC streaming related
 */

static int
uvc_video_process(struct uvc_device *dev)
{
    struct v4l2_buffer vbuf;
    unsigned int i;
    int ret;

    /*
     * Return immediately if UVC video output device has not started
     * streaming yet.
     */
    if (!dev->is_streaming) {
        usleep(10000);
        return 0;
    }

    /* Prepare a v4l2 buffer to be dequeued from UVC domain. */
    CLEAR(dev->ubuf);

    dev->ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    switch (dev->io) {
    case IO_METHOD_MMAP:
        dev->ubuf.memory = V4L2_MEMORY_MMAP;
        break;

    case IO_METHOD_USERPTR:
    default:
        dev->ubuf.memory = V4L2_MEMORY_USERPTR;
        break;
    }

    if (dev->run_standalone) {
        /* UVC stanalone setup. */

        ret = ioctl(dev->uvc_fd, VIDIOC_DQBUF, &dev->ubuf);
        if (ret < 0)
            return ret;

        dev->dqbuf_count++;

#ifdef ENABLE_BUFFER_DEBUG
        printf("%d: DeQueued buffer at UVC side = %d\n", dev->video_id, dev->ubuf.index);
#endif
        uvc_user_fill_buffer(dev, &dev->ubuf, dev->video_id);

        ret = ioctl(dev->uvc_fd, VIDIOC_QBUF, &dev->ubuf);
        if (ret < 0) {
            printf("%d: UVC: Unable to queue buffer: %s (%d).\n",
                   dev->video_id, strerror(errno), errno);
            return ret;
        }

        dev->qbuf_count++;

#ifdef ENABLE_BUFFER_DEBUG
        printf("%d: ReQueueing buffer at UVC side = %d\n", dev->video_id, dev->ubuf.index);
#endif
    }

    return 0;
}

static int
uvc_video_qbuf_mmap(struct uvc_device *dev)
{
    unsigned int i;
    int ret;

    for (i = 0; i < dev->nbufs; ++i) {
        memset(&dev->mem[i].buf, 0, sizeof(dev->mem[i].buf));

        dev->mem[i].buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        dev->mem[i].buf.memory = V4L2_MEMORY_MMAP;
        dev->mem[i].buf.index = i;

        /* UVC standalone setup. */

        ret = ioctl(dev->uvc_fd, VIDIOC_QBUF, &(dev->mem[i].buf));
        if (ret < 0) {
            printf("UVC: VIDIOC_QBUF failed : %s (%d).\n",
                   strerror(errno), errno);
            return ret;
        }

        dev->qbuf_count++;
    }

    return 0;
}

static int
uvc_video_qbuf_userptr(struct uvc_device *dev)
{
    unsigned int i;
    int ret;

    /* UVC standalone setup. */
    if (dev->run_standalone) {
        for (i = 0; i < dev->nbufs; ++i) {
            struct v4l2_buffer buf;

            CLEAR(buf);
            buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            buf.memory = V4L2_MEMORY_USERPTR;
            buf.m.userptr = (unsigned long)dev->dummy_buf[i].start;
            buf.length = dev->dummy_buf[i].length;
            buf.index = i;

            ret = ioctl(dev->uvc_fd, VIDIOC_QBUF, &buf);
            if (ret < 0) {
                printf("UVC: VIDIOC_QBUF failed : %s (%d).\n",
                       strerror(errno), errno);
                return ret;
            }

            dev->qbuf_count++;
        }
    }

    return 0;
}

static int
uvc_video_qbuf(struct uvc_device *dev)
{
    int ret = 0;

    switch (dev->io) {
    case IO_METHOD_MMAP:
        ret = uvc_video_qbuf_mmap(dev);
        break;

    case IO_METHOD_USERPTR:
        ret = uvc_video_qbuf_userptr(dev);
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int
uvc_video_reqbufs_mmap(struct uvc_device *dev, int nbufs)
{
    struct v4l2_requestbuffers rb;
    unsigned int i;
    int ret;

    CLEAR(rb);

    rb.count = nbufs;
    rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(dev->uvc_fd, VIDIOC_REQBUFS, &rb);
    if (ret < 0) {
        if (ret == -EINVAL)
            printf("UVC: does not support memory mapping\n");
        else
            printf("UVC: Unable to allocate buffers: %s (%d).\n",
                   strerror(errno), errno);
        goto err;
    }

    if (!rb.count)
        return 0;

    if (rb.count < 2) {
        printf("UVC: Insufficient buffer memory.\n");
        ret = -EINVAL;
        goto err;
    }

    /* Map the buffers. */
    dev->mem = calloc(rb.count, sizeof dev->mem[0]);
    if (!dev->mem) {
        printf("UVC: Out of memory\n");
        ret = -ENOMEM;
        goto err;
    }

    for (i = 0; i < rb.count; ++i) {
        memset(&dev->mem[i].buf, 0, sizeof(dev->mem[i].buf));

        dev->mem[i].buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        dev->mem[i].buf.memory = V4L2_MEMORY_MMAP;
        dev->mem[i].buf.index = i;

        ret = ioctl(dev->uvc_fd, VIDIOC_QUERYBUF, &(dev->mem[i].buf));
        if (ret < 0) {
            printf("UVC: VIDIOC_QUERYBUF failed for buf %d: "
                   "%s (%d).\n", i, strerror(errno), errno);
            ret = -EINVAL;
            goto err_free;
        }

        dev->mem[i].start = mmap(NULL /* start anywhere */,
                                 dev->mem[i].buf.length,
                                 PROT_READ | PROT_WRITE /* required */,
                                 MAP_SHARED /* recommended */,
                                 dev->uvc_fd, dev->mem[i].buf.m.offset);

        if (MAP_FAILED == dev->mem[i].start) {
            printf("UVC: Unable to map buffer %u: %s (%d).\n", i,
                   strerror(errno), errno);
            dev->mem[i].length = 0;
            ret = -EINVAL;
            goto err_free;
        }

        dev->mem[i].length = dev->mem[i].buf.length;
        printf("UVC: Buffer %u mapped at address %p.\n", i,
               dev->mem[i].start);
    }

    dev->nbufs = rb.count;
    printf("UVC: %u buffers allocated.\n", rb.count);

    return 0;

err_free:
    free(dev->mem);
err:
    return ret;
}

static int
uvc_video_reqbufs_userptr(struct uvc_device *dev, int nbufs)
{
    struct v4l2_requestbuffers rb;
    unsigned int i, j, bpl = 0, payload_size;
    int ret;

    CLEAR(rb);

    rb.count = nbufs;
    rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb.memory = V4L2_MEMORY_USERPTR;

    ret = ioctl(dev->uvc_fd, VIDIOC_REQBUFS, &rb);
    if (ret < 0) {
        if (ret == -EINVAL)
            printf("UVC: does not support user pointer i/o\n");
        else
            printf("UVC: VIDIOC_REQBUFS error %s (%d).\n",
                   strerror(errno), errno);
        goto err;
    }

    if (!rb.count)
        return 0;

    dev->nbufs = rb.count;
    printf("UVC: %u buffers allocated.\n", rb.count);

    if (dev->run_standalone) {
        /* Allocate buffers to hold dummy data pattern. */
        dev->dummy_buf = calloc(rb.count, sizeof dev->dummy_buf[0]);
        if (!dev->dummy_buf) {
            printf("UVC: Out of memory\n");
            ret = -ENOMEM;
            goto err;
        }

        switch (dev->fcc) {
        case V4L2_PIX_FMT_YUYV:
            bpl = dev->width * 2;
            payload_size = dev->width * dev->height * 2;
            break;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_H264:
            payload_size = dev->width * dev->height * 3 / 2;
            break;
        default:
            return -1;
        }

        for (i = 0; i < rb.count; ++i) {
            dev->dummy_buf[i].length = payload_size;
            dev->dummy_buf[i].start = malloc(payload_size);
            if (!dev->dummy_buf[i].start) {
                printf("UVC: Out of memory\n");
                ret = -ENOMEM;
                goto err;
            }

            if (V4L2_PIX_FMT_YUYV == dev->fcc)
                for (j = 0; j < dev->height; ++j)
                    memset(dev->dummy_buf[i].start + j * bpl,
                           dev->color++, bpl);
        }
    }

    return 0;

err:
    return ret;

}

static int
uvc_video_reqbufs(struct uvc_device *dev, int nbufs)
{
    int ret = 0;

    switch (dev->io) {
    case IO_METHOD_MMAP:
        ret = uvc_video_reqbufs_mmap(dev, nbufs);
        break;

    case IO_METHOD_USERPTR:
        ret = uvc_video_reqbufs_userptr(dev, nbufs);
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static void
uvc_video_open(struct uvc_device *dev, int width, int height, int fps)
{
    AddVideoParam_t param;
    /*enum disp_frame_type type;*/
    int cam_width = (width > height) ? width : height;
    int cam_height = (width > height) ? height : width;

    int depth_id = get_uvc_depth_cnt();
    int ir_id = get_uvc_ir_cnt();

    printf("%s: video_id = %d\n", __func__, dev->video_id);
    pthread_mutex_lock(&start_mutex);
    if (dev->fc->index == get_uvc_rgb_cnt()) {
         param.id = dev->video_src;
         param.width = cam_width;
         param.height = cam_height;
         param.fps = fps;
         param.img_effect = uvc_get_user_image_effect(dev->video_id);
         param.dcrop = uvc_get_user_dcrop_state(dev->video_id);
         if (uvc_video_addvideo_cb)
             uvc_video_addvideo_cb(&param);
    } else if ((dev->fc->index == get_uvc_depth_cnt()) ||
               (dev->fc->index == get_uvc_ir_cnt())) {
        if (cam_width == 640 && cam_height == 480)
            cif_vga_enable = true;

        cam_width = 1280;
        if (cif_vga_enable || (cam_height % 360 == 0))
            cam_height = 900;
        else
            cam_height = 1000;

        if (!cif_enable) {
            param.id = dev->video_src;
            param.width = cam_width;
            param.height = cam_height;
            param.fps = fps;
            param.dcrop = true;
            param.continuous = dev->continuous;
            if (uvc_video_addvideo_cb)
                uvc_video_addvideo_cb(&param);
            cif_enable = true;
        }
        /*
        if (depth_id >= 0 && ir_id >= 0) {
            cif_depth_ir_start = true;
            if (cif_vga_enable)
                type = DISP_FULL_DEPTH_VGA_IR_VGA_FRAME;
            else
                type = DISP_FULL_DEPTH_IR_FRAME;
        } else if (depth_id >= 0) {
             if (cif_vga_enable)
                 type = DISP_FULL_DEPTH_VGA_FRAME;
             else
                 type = DISP_FULL_DEPTH_FRAME;
        } else if (ir_id >= 0) {
            if (cif_vga_enable)
                type = DISP_FULL_IR_VGA_FRAME;
            else
                type = DISP_FULL_IR_FRAME;
        }
        if (uvc_video_set_frame_type_cb)
            uvc_video_set_frame_type_cb(type);
        */
    }
    pthread_mutex_unlock(&start_mutex);
    if (uvc_video_open_cb)
        uvc_video_open_cb(ctrl_device);
}


/*
 * This function is called in response to either:
 *  - A SET_ALT(interface 1, alt setting 1) command from USB host,
 *    if the UVC gadget supports an ISOCHRONOUS video streaming endpoint
 *    or,
 *
 *  - A UVC_VS_COMMIT_CONTROL command from USB host, if the UVC gadget
 *    supports a BULK type video streaming endpoint.
 */
static int
uvc_handle_streamon_event(struct uvc_device *dev)
{
    int ret;

    ret = uvc_video_reqbufs(dev, dev->nbufs);
    if (ret < 0)
        goto err;

    /* Queue buffers to UVC domain and start streaming. */
    ret = uvc_video_qbuf(dev);
    if (ret < 0)
        goto err;

    if (dev->run_standalone) {
        uvc_video_stream(dev, 1);
        dev->first_buffer_queued = 1;
        dev->is_streaming = 1;
    }

    return 0;

err:
    return ret;
}

/* ---------------------------------------------------------------------------
 * UVC Request processing
 */

static void
uvc_fill_streaming_control(struct uvc_device *dev,
                           struct uvc_streaming_control *ctrl,
                           int iformat, int iframe, unsigned int ival)
{
    const struct uvc_function_config_format *format;
    const struct uvc_function_config_frame *frame;
    unsigned int i;

    /*
     * Restrict the iformat, iframe and ival to valid values. Negative
     * values for iformat or iframe will result in the maximum valid value
     * being selected.
     */
    iformat = clamp((unsigned int)iformat, 1U,
                    dev->fc->streaming.num_formats);
    format = &dev->fc->streaming.formats[iformat-1];

    iframe = clamp((unsigned int)iframe, 1U, format->num_frames);
    frame = &format->frames[iframe-1];

    for (i = 0; i < frame->num_intervals; ++i) {
        if (ival <= frame->intervals[i]) {
            ival = frame->intervals[i];
            break;
        }
    }
    if (i == frame->num_intervals)
        ival = frame->intervals[frame->num_intervals-1];

    memset(ctrl, 0, sizeof *ctrl);

    ctrl->bmHint = 1;
    ctrl->bFormatIndex = iformat;
    ctrl->bFrameIndex = iframe;
    ctrl->dwFrameInterval = ival;
    switch (format->fcc) {
    case V4L2_PIX_FMT_YUYV:
        ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 2;
        break;
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_H264:
        ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 3 / 2;
        break;
    }

    /* TODO: the UVC maxpayload transfer size should be filled
     * by the driver.
     */
    if (!dev->bulk)
        ctrl->dwMaxPayloadTransferSize = (dev->maxpkt) *
                                         (dev->mult + 1) * (dev->burst + 1);
    else
        ctrl->dwMaxPayloadTransferSize = UVC_MAX_PAYLOAD_SIZE;

    ctrl->bmFramingInfo = 3;
    ctrl->bPreferedVersion = 1;
    ctrl->bMaxVersion = 1;
}

static void
uvc_events_process_standard(struct uvc_device *dev,
                            struct usb_ctrlrequest *ctrl,
                            struct uvc_request_data *resp)
{
    printf("standard request\n");
    (void)dev;
    (void)ctrl;
    (void)resp;
}

static void uvc_events_proc_ct(struct uvc_device *dev, uint8_t req,
        int8_t len, struct uvc_request_data *resp)
{
    switch (dev->cs) {
        /*
         * We support only 'UVC_CT_AE_MODE_CONTROL' for CAMERA
         * terminal, as our bmControls[0] = 2 for CT. Also we
         * support only auto exposure.
         */
    case UVC_CT_AE_MODE_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            /* Incase of auto exposure, attempts to
             * programmatically set the auto-adjusted
             * controls are ignored.
             */
            resp->data[0] = 0x01;
            resp->length = 1;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;

        case UVC_GET_INFO:
            /*
             * TODO: We support Set and Get requests, but
             * don't support async updates on an video
             * status (interrupt) endpoint as of
             * now.
             */
            resp->data[0] = 0x03;
            resp->length = 1;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;

        case UVC_GET_CUR:
        case UVC_GET_DEF:
        case UVC_GET_RES:
            /* Auto Mode Ã¢?? auto Exposure Time, auto Iris. */
            resp->data[0] = 0x02;
            resp->length = 1;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            /*
             * We don't support this control, so STALL the
             * control ep.
             */
            resp->length = -EL2HLT;
            /*
             * For every unsupported control request
             * set the request error code to appropriate
             * value.
             */
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;
    case UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL:
        switch (req) {
        case UVC_GET_INFO:
        case UVC_GET_MIN:
        case UVC_GET_MAX:
        case UVC_GET_CUR:
        case UVC_GET_DEF:
        case UVC_GET_RES:
            resp->data[0] = 100;
            resp->length = len;

            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            /*
             * We don't support this control, so STALL the
             * control ep.
             */
            resp->length = -EL2HLT;
            /*
             * For every unsupported control request
             * set the request error code to appropriate
             * value.
             */
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
        }
        break;
    case UVC_CT_IRIS_ABSOLUTE_CONTROL:
        switch (req) {
        case UVC_GET_INFO:
        case UVC_GET_CUR:
        case UVC_GET_MIN:
        case UVC_GET_MAX:
        case UVC_GET_DEF:
        case UVC_GET_RES:
            resp->data[0] = 10;
            resp->length = len;

            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            /*
             * We don't support this control, so STALL the
             * control ep.
             */
            resp->length = -EL2HLT;
            /*
             * For every unsupported control request
             * set the request error code to appropriate
             * value.
             */
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
        }
        break;

    default:
        /*
         * We don't support this control, so STALL the control
         * ep.
         */
        resp->length = -EL2HLT;
        /*
         * If we were not supposed to handle this
         * 'cs', prepare a Request Error Code response.
         */
        dev->request_error_code.data[0] = 0x06;
        dev->request_error_code.length = 1;
        break;
    }
}

static void uvc_events_proc_pu(struct uvc_device *dev, uint8_t req,
        int8_t len, struct uvc_request_data *resp)
{
    switch (dev->cs) {
    case UVC_PU_BRIGHTNESS_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            printf("set brightness\n");
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_BRIGHTNESS_MIN_VAL;
            resp->length = 2;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_BRIGHTNESS_MAX_VAL;
            resp->length = 2;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->brightness_val,
                   resp->length);
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            /*
             * We support Set and Get requests and don't
             * support async updates on an interrupt endpt
             */
            resp->data[0] = 0x03;
            resp->length = 1;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_BRIGHTNESS_DEFAULT_VAL;
            resp->length = 2;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_BRIGHTNESS_STEP_SIZE;
            resp->length = 2;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            /*
             * We don't support this control, so STALL the
             * default control ep.
             */
            resp->length = -EL2HLT;
            /*
             * For every unsupported control request
             * set the request error code to appropriate
             * code.
             */
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_CONTRAST_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_CONTRAST_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_CONTRAST_MAX_VAL % 256;
            resp->data[1] = PU_CONTRAST_MAX_VAL / 256;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->contrast_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_CONTRAST_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_CONTRAST_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_HUE_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_HUE_MIN_VAL % 256;
            resp->data[1] = PU_HUE_MIN_VAL / 256;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_HUE_MAX_VAL % 256;
            resp->data[1] = PU_HUE_MAX_VAL / 256;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            if (uvc_hue_get_cb)
                dev->hue_val = uvc_hue_get_cb(hue_get_device);
            memcpy(&resp->data[0], &dev->hue_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_HUE_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_HUE_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_SATURATION_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_SATURATION_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_SATURATION_MAX_VAL % 256;
            resp->data[1] = PU_SATURATION_MAX_VAL / 256;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->saturation_val,
                   resp->length);
            memset(&dev->saturation_val, 0, sizeof(dev->saturation_val));
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_SATURATION_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_SATURATION_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_SHARPNESS_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_SHARPNESS_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_SHARPNESS_MAX_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->sharpness_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_SHARPNESS_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_SHARPNESS_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_GAMMA_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_GAMMA_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_GAMMA_MAX_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->gamma_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_GAMMA_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_GAMMA_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_WHITE_BALANCE_TEMPERATURE_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_WHITE_BALANCE_TEMPERATURE_MAX_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->white_balance_temperature_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_WHITE_BALANCE_TEMPERATURE_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_WHITE_BALANCE_TEMPERATURE_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_GAIN_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_GAIN_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_GAIN_MAX_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->gain_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_GAIN_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_GAIN_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_HUE_AUTO_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = PU_HUE_AUTO_MIN_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = PU_HUE_AUTO_MAX_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 2;
            memcpy(&resp->data[0], &dev->hue_auto_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = PU_HUE_AUTO_DEFAULT_VAL;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = PU_HUE_AUTO_STEP_SIZE;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case UVC_PU_POWER_LINE_FREQUENCY_CONTROL:
        switch (req) {
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = 1;
            memcpy(&resp->data[0], &dev->power_line_frequency_val,
                   resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = 1;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    default:
        /*
         * We don't support this control, so STALL the control
         * ep.
         */
        resp->length = -EL2HLT;
        /*
         * If we were not supposed to handle this
         * 'cs', prepare a Request Error Code response.
         */
        dev->request_error_code.data[0] = 0x06;
        dev->request_error_code.length = 1;
        break;
    }
}

static void uvc_events_proc_xu(struct uvc_device *dev, uint8_t req,
        int8_t len, struct uvc_request_data *resp)
{
    switch (dev->cs) {
    case 1:
        switch (req) {
        case UVC_GET_LEN:
            resp->data[0] = 0x4;
            resp->length = len;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = 0;
            resp->length = 4;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = 0xFF;
            resp->data[1] = 0xFF;
            resp->data[2] = 0xFF;
            resp->data[3] = 0xFF;
            resp->length = 4;
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = len < sizeof(dev->xu_query.ctrl1) ? len : sizeof(dev->xu_query.ctrl1);
            memcpy(resp->data, dev->xu_query.ctrl1, resp->length);
            /*
             * For every successfully handled control
             * request set the request error code to no
             * error
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            /*
             * We support Set and Get requests and don't
             * support async updates on an interrupt endpt
             */
            resp->data[0] = 0x03;
            resp->length = 1;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = 0;
            resp->length = 4;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = 1;
            resp->length = 4;
            /*
             * For every successfully handled control
             * request, set the request error code to no
             * error.
             */
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            /*
             * We don't support this control, so STALL the
             * default control ep.
             */
            resp->length = -EL2HLT;
            /*
             * For every unsupported control request
             * set the request error code to appropriate
             * code.
             */
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case 2:
        switch (req) {
        case UVC_GET_LEN:
            resp->data[0] = 0x10;
            resp->data[1] = 0x00;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = 0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = 0xFF;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = len;
            if (sizeof(dev->xu_query.ctrl2) >= resp->length)
                memcpy(resp->data, dev->xu_query.ctrl2, resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = 0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = 1;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case 3:
        switch (req) {
        case UVC_GET_LEN:
            if (dev->xu_query.length > dev->xu_query.index)
                resp->data[0] = __min(dev->xu_query.length - dev->xu_query.index, 60);
            else
                resp->data[0] = 2;
            resp->data[1] = 0;
            resp->length = 2;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_SET_CUR:
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            resp->data[0] = 0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            resp->data[0] = 0xFF;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = len;
            if (dev->xu_query.ctrl2[0] == EXT_QUERY_CMD) {
                if (resp->length == 4) {
                    resp->data[0] = EXT_QUERY_CMD;
                    resp->data[1] = dev->xu_query.result;
                    memcpy(&resp->data[2], &dev->xu_query.get_checksum, 2);
                    dev->xu_query.result = 0;
                } else {
                    printf("%d: iq tool get data error.\n", __LINE__);
                }
            } else if (uvc_iq_get_data_cb && dev->xu_query.length) {
                if (dev->xu_query.index + resp->length < MAX_UVC_REQUEST_DATA_LENGTH) {
                    memcpy(resp->data, dev->xu_query.data + dev->xu_query.index, resp->length);
                    dev->xu_query.index += resp->length;
                } else {
                    printf("%d: iq tool get data error.\n", __LINE__);
                }
            }
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            resp->data[0] = 0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            resp->data[0] = 1;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    case 4:
        switch (req) {
        case UVC_GET_LEN:
            memset(resp->data, 0, sizeof(resp->data));
            resp->data[0] = sizeof(resp->data);
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_SET_CUR:
            memset(resp->data, 0, sizeof(resp->data));
            resp->data[0] = 0x0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MIN:
            memset(resp->data, 0, sizeof(resp->data));
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_MAX:
            memset(resp->data, 0xFF, sizeof(resp->data));
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_CUR:
            resp->length = len < sizeof(dev->xu_query.ctrl4) ? len : sizeof(dev->xu_query.ctrl4);
            memcpy(resp->data, dev->xu_query.ctrl4, resp->length);
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_INFO:
            resp->data[0] = 0x03;
            resp->length = 1;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_DEF:
            memset(resp->data, 0, sizeof(resp->data));
            resp->data[0] = 0;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        case UVC_GET_RES:
            memset(resp->data, 0, sizeof(resp->data));
            resp->data[0] = 1;
            resp->length = len;
            dev->request_error_code.data[0] = 0x00;
            dev->request_error_code.length = 1;
            break;
        default:
            resp->length = -EL2HLT;
            dev->request_error_code.data[0] = 0x07;
            dev->request_error_code.length = 1;
            break;
        }
        break;

    default:
        /*
         * We don't support this control, so STALL the control
         * ep.
         */
        resp->length = -EL2HLT;
        /*
         * If we were not supposed to handle this
         * 'cs', prepare a Request Error Code response.
         */
        dev->request_error_code.data[0] = 0x06;
        dev->request_error_code.length = 1;
        break;
    }
}

static void
uvc_events_process_control(struct uvc_device *dev, uint8_t req,
                           uint8_t cs, uint8_t entity_id,
                           uint8_t len, struct uvc_request_data *resp)
{
    if (!dev->sat.is_file)
        printf("video_id = %d req = %d cs = %d entity_id =%d len = %d \n",
               dev->video_id, req, cs, entity_id, len);

    dev->cs = cs;
    dev->entity_id = entity_id;

    switch (entity_id) {
    case 0:
        switch (cs) {
        case UVC_VC_REQUEST_ERROR_CODE_CONTROL:
            /* Send the request error code last prepared. */
            resp->data[0] = dev->request_error_code.data[0];
            resp->length = dev->request_error_code.length;
            break;

        default:
            /*
             * If we were not supposed to handle this
             * 'cs', prepare an error code response.
             */
            dev->request_error_code.data[0] = 0x06;
            dev->request_error_code.length = 1;
            break;
        }
        break;

        /* Camera terminal unit 'UVC_VC_INPUT_TERMINAL'. */
    case 1:
        uvc_events_proc_ct(dev, req, len, resp);
        break;

        /* processing unit 'UVC_VC_PROCESSING_UNIT' */
    case 2:
        uvc_events_proc_pu(dev, req, len, resp);
        break;

    case 6:
        uvc_events_proc_xu(dev, req, len, resp);
        break;

    default:
        /*
         * If we were not supposed to handle this
         * 'cs', prepare a Request Error Code response.
         */
        dev->request_error_code.data[0] = 0x06;
        dev->request_error_code.length = 1;
        break;

    }
    if (resp->length == -EL2HLT) {
        printf("unsupported: req=%02x, cs=%d, entity_id=%d, len=%d\n",
               req, cs, entity_id, len);
        resp->length = 0;
    }
    if (!dev->sat.is_file)
        printf("control request (req %02x cs %02x)\n", req, cs);
}


static void
uvc_events_process_streaming(struct uvc_device *dev, uint8_t req, uint8_t cs,
                             struct uvc_request_data *resp)
{
    struct uvc_streaming_control *ctrl;

    printf("streaming request (req %02x cs %02x)\n", req, cs);

    if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
        return;

    ctrl = (struct uvc_streaming_control *)&resp->data;
    resp->length = sizeof * ctrl;

    switch (req) {
    case UVC_SET_CUR:
        dev->control = cs;
        resp->length = 34;
        break;

    case UVC_GET_CUR:
        if (cs == UVC_VS_PROBE_CONTROL)
            memcpy(ctrl, &dev->probe, sizeof * ctrl);
        else
            memcpy(ctrl, &dev->commit, sizeof * ctrl);
        break;

    case UVC_GET_MIN:
    case UVC_GET_MAX:
    case UVC_GET_DEF:
        if (req == UVC_GET_MAX)
            uvc_fill_streaming_control(dev, ctrl, -1, -1, UINT_MAX);
        else
            uvc_fill_streaming_control(dev, ctrl, 1, 1, 0);
        break;

    case UVC_GET_RES:
        CLEAR(ctrl);
        break;

    case UVC_GET_LEN:
        resp->data[0] = 0x00;
        resp->data[1] = 0x22;
        resp->length = 2;
        break;

    case UVC_GET_INFO:
        resp->data[0] = 0x03;
        resp->length = 1;
        break;
    }
}

static void
uvc_events_process_class(struct uvc_device *dev, struct usb_ctrlrequest *ctrl,
                         struct uvc_request_data *resp)
{
    unsigned int interface = ctrl->wIndex & 0xff;

    if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
        return;

    if (interface == dev->fc->control.intf.bInterfaceNumber)
        uvc_events_process_control(dev, ctrl->bRequest,
                                   ctrl->wValue >> 8,
                                   ctrl->wIndex >> 8,
                                   ctrl->wLength, resp);
    else if (interface == dev->fc->streaming.intf.bInterfaceNumber)
        uvc_events_process_streaming(dev, ctrl->bRequest,
                                     ctrl->wValue >> 8, resp);
}

static void
uvc_events_process_setup(struct uvc_device *dev, struct usb_ctrlrequest *ctrl,
                         struct uvc_request_data *resp)
{
    dev->control = 0;

#ifdef ENABLE_USB_REQUEST_DEBUG
    printf("\nbRequestType %02x bRequest %02x wValue %04x wIndex %04x "
           "wLength %04x\n", ctrl->bRequestType, ctrl->bRequest,
           ctrl->wValue, ctrl->wIndex, ctrl->wLength);
#endif

    switch (ctrl->bRequestType & USB_TYPE_MASK) {
    case USB_TYPE_STANDARD:
        uvc_events_process_standard(dev, ctrl, resp);
        break;

    case USB_TYPE_CLASS:
        uvc_events_process_class(dev, ctrl, resp);
        break;

    default:
        break;
    }
}

static void
saturation_communicate(struct uvc_device *dev, struct uvc_request_data *data)
{
    gettimeofday(&dev->sat.t1, NULL);
    if (dev->sat.t0.tv_sec && dev->sat.t0.tv_usec) {
        if ((dev->sat.t1.tv_sec - dev->sat.t0.tv_sec) * 1000 +
                (dev->sat.t1.tv_usec - dev->sat.t0.tv_usec) / 1000 > 1000) {
            printf("file transfer timeout!\n");
            saturation_clear(dev);
        }
    }
    dev->sat.t0 = dev->sat.t1;
    if (!dev->sat.is_file) {
        memcpy(&dev->sat.cmd[dev->sat.cmd_cnt], data->data, data->length);
        if (dev->sat.cmd[dev->sat.cmd_cnt] == 0x5555
            && dev->sat.cmd[(dev->sat.cmd_cnt + 3) % 4] == 0xEEEE
            && dev->sat.cmd[(dev->sat.cmd_cnt + 2) % 4] == 0xAAAA
            && dev->sat.cmd[(dev->sat.cmd_cnt + 1) % 4] == 0x5555) {
            saturation_clear(dev);
            dev->sat.is_file = true;
            memset(&dev->saturation_val, 0, sizeof(dev->saturation_val));
            printf("file transfer begin.\n");
        } else {
            dev->sat.cmd_cnt++;
            dev->sat.cmd_cnt = dev->sat.cmd_cnt % 4;
        }
    } else if (!dev->sat.file_size) {
        memcpy(&dev->sat.file_size, data->data, data->length);
        if (dev->sat.file_size > SAT_FILE_MAX_SIZE) {
            printf("file transfer data file_size error!\n");
            saturation_clear(dev);
        }
    } else if (dev->sat.data_cnt < dev->sat.file_size / 2) {
        memcpy(&dev->sat.data[dev->sat.data_cnt], data->data, data->length);
        dev->sat.data_cnt++;
        if (dev->sat.data_cnt == dev->sat.file_size / 2) {
            dev->sat.file_checksum = 0;
            for (unsigned short i = 0; i < dev->sat.file_size / 2; i++)
                dev->sat.file_checksum += dev->sat.data[i];
        }
    } else if (dev->sat.data_cnt == dev->sat.file_size / 2) {
        if (memcmp(&dev->sat.file_checksum, data->data, data->length) == 0) {
            printf("file transfer end.\n");
            if (uvc_sat_data_cb)
                uvc_sat_data_cb(dev->sat.data, dev->sat.file_size, sat_device);
            dev->saturation_val = 0x6666;
        } else {
            unsigned short checksum;
            memcpy(&checksum, data->data, data->length);
            printf("checksum error, %hu != %hu\n",
                    dev->sat.file_checksum, checksum);
        }
        saturation_clear(dev);
    } else {
        saturation_clear(dev);
    }
}

static void
uvc_get_release_version(unsigned char *ctrl, unsigned int size) {
    char buf[1024];

    memset(ctrl, 0, size);
    memcpy(ctrl, "Version", sizeof("Version"));
    memset(buf, 0, sizeof(buf));
    memcpy(buf, USB_UVC_FW_VERSION, sizeof(USB_UVC_FW_VERSION));
    memcpy(ctrl, buf, size - 1);
}

static void
uvc_get_sn_code(unsigned char *ctrl, unsigned int size) {
#define DATE_PATH "/sys/devices/10270000.spi/spi_master/spi0/spi0.0/product_date"

    char sn_data[64];
    int fd;

    memset(sn_data, 0, sizeof(sn_data));
    fd = open(DATE_PATH, O_RDONLY);
    if (fd >= 0) {
        if (lseek(fd, 0, SEEK_SET) != -1) {
            read(fd, sn_data, sizeof(sn_data) - 1);
        }
        close(fd);
    }
    memset(ctrl, 0, size);
    memcpy(ctrl, sn_data, size - 1);
}

static int
uvc_get_calib_pos(unsigned char *ctrl, unsigned int size) {
    int fd;
    int ret = 0;
    loff_t pos = 0;
    unsigned int file_size = 0;
    char *file_data = NULL;
    struct calib_head head;
    short xoffset, yoffset;
    int ratio;
    int i;
    int pos_x1, pos_y1, pos_x2, pos_y2;

    memset(ctrl, 0, size);

    fd = open("/dev/ref", O_RDONLY);
    if (fd < 0) {
		ret = -EFAULT;
        printf("open ref failed!\n");
		goto err;
    }

    lseek(fd, 0, SEEK_SET);
    read(fd, (char *)&head, sizeof(head));
    if (strncmp(head.magic, PREISP_CALIB_MAGIC, sizeof(head.magic))) {
        ret = -EFAULT;
        printf("%s: magic(%s) is unmatch\n", __func__, head.magic);
        goto err;
    }

    for (i = 0; i < head.items_number; i++) {
        if (!strncmp(head.item[i].name, PREISP_DCROP_ITEM_NAME, sizeof(head.item[i].name)))
            break;
    }

    if (i >= head.items_number) {
        ret = -EFAULT;
        printf("%s: cannot find %s\n", __func__, PREISP_DCROP_ITEM_NAME);
        goto err;
    }

    file_size = head.item[i].size;
    if (file_size < (PREISP_DCROP_CALIB_YOFFSET + 2)) {
        ret = -EFAULT;
        printf("%s: file_size is not correct\n", __func__);
        goto err;
    }

    file_data = (char *)malloc(file_size);
    if (!file_data) {
        ret = -ENOMEM;
        printf("%s: no memmory\n", __func__);
        goto err;
    }

    pos = head.item[i].offset;
    lseek(fd, pos, SEEK_SET);
    ret = read(fd, file_data, head.item[i].size);
    if (ret <= 0) {
        ret = -EFAULT;
        printf("%s: read error: ret=%d\n", __func__, ret);
        goto err;
    }

    ratio = *(int *)(file_data + PREISP_DCROP_CALIB_RATIO);
    xoffset = *(short *)(file_data + PREISP_DCROP_CALIB_XOFFSET);
    yoffset = *(short *)(file_data + PREISP_DCROP_CALIB_YOFFSET);
    printf("item %s: file_size %d, ratio 0x%x, xoffset 0x%x, yoffset 0x%x\n",
        head.item[i].name, file_size, ratio, xoffset, yoffset);
    if (ratio > 0x10000 || ratio == 0) {
        ret = -EFAULT;
        goto err;
    }

    float *calib_data = (float *)file_data;
    short *cam_offset = (short*)((char *)file_data + 192 + 4);

    float f_ir = (calib_data[11] + calib_data[12]) / 2;
    float f_rgb = (calib_data[2] + calib_data[3]) / 2;
    float real_offset = ((float)cam_offset[1])*0.023148148 * f_ir / f_rgb; // offset/16/1.35/2*f_ir/f_rgb

    float ir_vga_height = 360;//or 400
    float center_offset = (480 - ir_vga_height)/2;

    pos_x1 = center_offset - real_offset + 0.5;
    pos_y1 = 0;
    pos_x2 = pos_x1 + ir_vga_height + 0.5;
    pos_y2 = 640;

    // convert to [-1000, 1000]
    pos_x1 = (pos_x1 - 240) * 1000 / 240;
    pos_x2 = (pos_x2 - 240) * 1000 / 240;
    pos_y1 = (pos_y1 - 320) * 1000 / 320;
    pos_y2 = (pos_y2 - 320) * 1000 / 320;
    printf("f_ir %f, f_rgb %f, real_offset %f, pos_x1 %d, pos_y1 %d, pos_x2 %d, pos_y2 %d\n",
        f_ir, f_rgb, real_offset, pos_x1, pos_y1, pos_x2, pos_y2);
    char *ppos = &ctrl[0];
    memcpy(ppos, &pos_x1, sizeof(pos_x1));
    ppos += sizeof(pos_x1);
    memcpy(ppos, &pos_y1, sizeof(pos_y1));
    ppos += sizeof(pos_y1);
    memcpy(ppos, &pos_x2, sizeof(pos_x2));
    ppos += sizeof(pos_x2);
    memcpy(ppos, &pos_y2, sizeof(pos_y2));

err:
    if (file_data)
        free(file_data);
    if (fd)
        close(fd);

    return ret;
}

static void
uvc_get_model_name(unsigned char *ctrl, unsigned int size)
{
    printf("%s %s\n", __func__, SN_CODE);
    memset(ctrl, 0, size);
    strncpy(ctrl, SN_CODE, size - 1);
}

static void
uvc_get_support_list(unsigned char *ctrl, unsigned int size)
{
    memset(ctrl, 0, size);
    if (0 == strcmp(SN_CODE, "R2011200") ||
        0 == strcmp(SN_CODE, "R2011301")) {
        ctrl[0] = 1;
        ctrl[1] = 1;
        ctrl[2] = 1;
    }
}

static void
uvc_get_current_output(unsigned char *ctrl, unsigned int size,
                       struct uvc_device *dev)
{
    size_t out_size = 0;

    memset(ctrl, 0, size);
    memcpy(ctrl, &dev->fcc, sizeof(dev->fcc));
    out_size += sizeof(dev->fcc);
    memcpy(ctrl + out_size, &dev->width, sizeof(dev->width));
    out_size += sizeof(dev->width);
    memcpy(ctrl + out_size, &dev->height, sizeof(dev->height));
}

static void uvc_pu_ctrl(struct uvc_device *dev, uint8_t cs, struct uvc_request_data *data)
{
    switch (cs) {
    case UVC_PU_BRIGHTNESS_CONTROL:
        if (sizeof(dev->brightness_val) >= data->length) {
            memcpy(&dev->brightness_val, data->data, data->length);
            /* video_record_set_brightness(*val); */
        }
        break;
    case UVC_PU_CONTRAST_CONTROL:
        printf("UVC_PU_CONTRAST_CONTROL receive\n");
        if (sizeof(dev->contrast_val) >= data->length) {
            memcpy(&dev->contrast_val, data->data, data->length);
            if (uvc_video_set_time_cb)
                uvc_video_set_time_cb(dev->contrast_val, 1);
            printf("UVC_PU_CONTRAST_CONTROL: 0x%02x 0x%02x\n",
                    data->data[0], data->data[1]);
            /* video_record_set_contrast(*val); */
        }
        break;
    case UVC_PU_HUE_CONTROL:
        printf("UVC_PU_HUE_CONTROL receive\n");
        if (sizeof(dev->hue_val) >= data->length) {
            memcpy(&dev->hue_val, data->data, data->length);
            printf("UVC_PU_HUE_CONTROL: 0x%02x 0x%02x\n",
                    data->data[0], data->data[1]);
            if (uvc_hue_set_cb)
                uvc_hue_set_cb(dev->hue_val, hue_set_device);
            //video_record_set_hue(*val);
        }
        break;
    case UVC_PU_SATURATION_CONTROL:
        if (sizeof(dev->saturation_val) >= data->length) {
            //memcpy(&dev->saturation_val, data->data, data->length);
            saturation_communicate(dev, data);
            //video_record_set_saturation(*val);
        }
        break;
    case UVC_PU_SHARPNESS_CONTROL:
        if (sizeof(dev->sharpness_val) >= data->length)
            memcpy(&dev->sharpness_val, data->data, data->length);
        break;
    case UVC_PU_GAMMA_CONTROL:
        if (sizeof(dev->gamma_val) >= data->length)
            memcpy(&dev->gamma_val, data->data, data->length);
        break;
    case UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
        /* 0:auto, 1:Daylight 2:fluocrescence 3:cloudysky 4:tungsten */
        if (sizeof(dev->white_balance_temperature_val) >= data->length) {
            memcpy(&dev->white_balance_temperature_val, data->data, data->length);
            //api_set_white_balance(*val / 51);
        }
        break;
    case UVC_PU_GAIN_CONTROL:
        if (sizeof(dev->gain_val) >= data->length)
            memcpy(&dev->gain_val, data->data, data->length);
        if (uvc_video_cif_set_gain_cb)
            uvc_video_cif_set_gain_cb(dev->gain_val);
        break;
    case UVC_PU_HUE_AUTO_CONTROL:
        if (sizeof(dev->hue_auto_val) >= data->length)
            memcpy(&dev->hue_auto_val, data->data, data->length);
        break;
    case UVC_PU_POWER_LINE_FREQUENCY_CONTROL:
        if (sizeof(dev->power_line_frequency_val) >= data->length) {
            memcpy(&dev->power_line_frequency_val, data->data, data->length);
            //video_record_set_power_line_frequency(*val);
        }
        break;
    default:
        break;
    }
}

static int uvc_xu_ctrl_cs1(struct uvc_device *dev,
        struct uvc_request_data *data)
{
    unsigned char *ctrl = dev->xu_query.ctrl1;
    unsigned int ctrl_size = sizeof(dev->xu_query.ctrl1);
    unsigned int command = 0;

    if (ctrl_size < data->length)
        return -1;

    memset(ctrl, 0, ctrl_size);
    memcpy(ctrl, data->data, data->length);
    memcpy(&command, ctrl, sizeof(command));
    printf("extension ctrl cs1 set cur data: 0x%02x\n", ctrl[0]);

    if (command & 0xFFFFFF == 0xFFFFF8) {
        if (uvc_video_set_iq_mode_cb)
            uvc_video_set_iq_mode_cb(3);
        if (uvc_video_set_focus_pos_cb)
            uvc_video_set_focus_pos_cb(ctrl[3]);
        return 0;
    }

    switch (command) {
    case 0xFFFFFFFF:
        system("reboot loader");
        break;

    case 0xFFFFFFFE:
        system("reboot");
        break;

    case 0xFFFFFFFC:
        /* set function to mass storag */
        break;

    case 0xFFFFFFFB:
        if (uvc_write_eeprom_cb)
            uvc_write_eeprom_cb();
        break;

    case 0xFFFFFFFA:
        if (uvc_video_set_iq_mode_cb)
            uvc_video_set_iq_mode_cb(1);
        break;

    case 0xFFFFFFF9:
        if (uvc_video_set_iq_mode_cb)
            uvc_video_set_iq_mode_cb(3);
        if (uvc_video_set_focus_pos_cb)
            uvc_video_set_focus_pos_cb(56);
        break;

    case 0xFFFFFFF7:
        if (uvc_video_set_iq_mode_cb)
            uvc_video_set_iq_mode_cb(0);
        break;

    case 0xFFFFFFF5:
        if (uvc_video_set_image_effect_cb) {
            uvc_video_set_image_effect_cb(1);
            uvc_set_user_image_effect(1, uvc_video_id_get(get_uvc_rgb_cnt()));
        }
        break;

    case 0xFFFFFFF4:
        if (uvc_video_set_image_effect_cb) {
            uvc_video_set_image_effect_cb(0);
            uvc_set_user_image_effect(0, uvc_video_id_get(get_uvc_rgb_cnt()));
        }
        break;

    case 0xFFFFFFF3:
        if (uvc_video_stream_control_cb) {
            if (uvc_video_id_get(get_uvc_rgb_cnt()) == dev->video_id)
                uvc_video_stream_control_cb(3, 1);
            else {
                pthread_mutex_lock(&start_mutex);
                if (g_cif_cnt < 2)
                    g_cif_cnt++;
                pthread_mutex_unlock(&start_mutex);
                uvc_video_stream_control_cb(4, 1);
            }
        }
        if (uvc_camera_control_cb)
            uvc_camera_control_cb(1);
        break;

    case 0xFFFFFFF2:
        if (uvc_video_stream_control_cb) {
            if (uvc_video_id_get(get_uvc_rgb_cnt()) == dev->video_id)
                uvc_video_stream_control_cb(3, 0);
            else {
                pthread_mutex_lock(&start_mutex);
                if (g_cif_cnt > 0)
                    g_cif_cnt--;
                pthread_mutex_unlock(&start_mutex);
                if (g_cif_cnt == 0)
                    uvc_video_stream_control_cb(4, 0);
            }
        }
        if (uvc_camera_control_cb)
            uvc_camera_control_cb(0);
        uvc_set_uvc_stream(dev->video_id, false);
        dev->bulk_timeout = 0;
        break;

    case 0xFFFFFFF1:
        uvc_pthread_signal();
        break;

    case 0xFFFFFFF0:
        uvc_pthread_signal();
        break;

    case 0xFFFFFFEF:
        dev->continuous = true;
        if (uvc_video_set_time_cb)
            uvc_video_set_time_cb(0, -1);
        if (uvc_set_frame_output_cb)
            uvc_set_frame_output_cb(-1);
        break;

    case 0xFFFFFFEE:
        if (uvc_video_set_time_cb)
            uvc_video_set_time_cb(0, -2);
        if (uvc_set_frame_output_cb)
            uvc_set_frame_output_cb(-2);
        break;

    case 0xFFFFFFED:
        if (uvc_video_set_time_cb)
            uvc_video_set_time_cb(0, -3);
        if (uvc_set_frame_output_cb)
            uvc_set_frame_output_cb(-3);
        break;

    case 0xFFFFFFEC:
        if (uvc_video_set_time_cb)
            uvc_video_set_time_cb(0, -6);
        if (uvc_set_frame_output_cb)
            uvc_set_frame_output_cb(-6);
        break;

    case 0xFFFFFFEB:
        /* set function to adb*/
        break;

    case 0xFFFFFFEA:
        uvc_set_user_dcrop_state(false, uvc_video_id_get(get_uvc_rgb_cnt()));
        break;

    case 0xFFFFFFE9:
        uvc_set_user_dcrop_state(true, uvc_video_id_get(get_uvc_rgb_cnt()));
        break;

    case 0xFFFFFFE8:
        if (uvc_set_one_frame_liveness_cb)
            uvc_set_one_frame_liveness_cb();
        break;

    case 0xFFFFFFE7:
        if (uvc_set_continuous_liveness_cb)
            uvc_set_continuous_liveness_cb(1);
        break;

    case 0xFFFFFFE6:
        if (uvc_set_continuous_liveness_cb)
            uvc_set_continuous_liveness_cb(0);
        break;

    default:
        printf("unsupport xu ctrl cs1 command: 0x%08x!\n", command);
        break;
    }

    return 0;
}

static int uvc_xu_ctrl_cs2(struct uvc_device *dev,
        struct uvc_request_data *data)
{
    unsigned char *ctrl = dev->xu_query.ctrl2;
    unsigned int ctrl_size = sizeof(dev->xu_query.ctrl2);

    if (ctrl_size >= data->length) {
        memcpy(ctrl, data->data, data->length);
        memcpy(&dev->xu_query.length, &ctrl[1], 2);
        memcpy(&dev->xu_query.checksum, &ctrl[3], 2);
        dev->xu_query.index = 0;
        if (ctrl[0] != EXT_QUERY_CMD)
            dev->xu_query.result = 0;
        memset(dev->xu_query.data, 0, sizeof(dev->xu_query.data));

        switch (ctrl[0]) {
            case 0xc5:
                if (uvc_video_get_flt_cb)
                    uvc_video_get_flt_cb(ctrl[5], ctrl[6]);
                break;
            default:
                break;
        }
        if (ctrl[0] != EXT_QUERY_CMD && uvc_iq_get_data_cb) {
            uvc_iq_get_data_cb(ctrl, dev->xu_query.data, dev->xu_query.length);
            dev->xu_query.get_checksum = 0;
            for (int i = 0; i < dev->xu_query.length; i++)
                dev->xu_query.get_checksum += dev->xu_query.data[i];
        }
    }

    return 0;
}

static int uvc_xu_ctrl_cs3(struct uvc_device *dev,
        struct uvc_request_data *data)
{
    if (dev->xu_query.index + data->length < MAX_UVC_REQUEST_DATA_LENGTH) {
        memcpy(dev->xu_query.data + dev->xu_query.index, data->data, data->length);
        dev->xu_query.index += data->length;
    } else {
        printf("ex data received error occur!\n");
        dev->xu_query.result = -1;
    }
    if (uvc_iq_set_data_cb && dev->xu_query.index == dev->xu_query.length) {
        if (dev->xu_query.length <= 60) {
            uvc_iq_set_data_cb(dev->xu_query.data, dev->xu_query.length);
            dev->xu_query.result = 1;
        } else {
            unsigned short sum = 0;
            for (int i = 0; i < dev->xu_query.length; i++)
                sum += dev->xu_query.data[i];
            if (sum == dev->xu_query.checksum) {
                uvc_iq_set_data_cb(dev->xu_query.data, dev->xu_query.length);
                dev->xu_query.result = 1;
            } else {
                printf("ex data checksum error!\n");
                dev->xu_query.result = -1;
            }
        }
    }

    return 0;
}

static int uvc_xu_ctrl_cs4(struct uvc_device *dev,
        struct uvc_request_data *data)
{
    unsigned char *ctrl = dev->xu_query.ctrl4;
    unsigned int ctrl_size = sizeof(dev->xu_query.ctrl4);
    unsigned int command = 0;
    unsigned int *value;

    if (ctrl_size >= data->length) {
        memset(ctrl, 0, ctrl_size);
        memcpy(ctrl, data->data, data->length);
        memcpy(&command, ctrl, sizeof(command));
        printf("extension ctrl cs4 set cur data: 0x%02x\n", ctrl[0]);

        switch (command) {
        case 0x00000000:
            uvc_get_release_version(ctrl, ctrl_size);
            break;

        case 0x00000001:
            uvc_get_sn_code(ctrl, ctrl_size);
            break;

        case 0x00000002:
            uvc_get_calib_pos(ctrl, ctrl_size);
            break;

        case 0x00000003:
            printf("get pcba sn\n");
            memset(ctrl, 0, ctrl_size);
            //vendor_storage_read(ctrl_size, ctrl, VENDOR_SN_ID);
            break;

        case 0x00000004:
            printf("get pcba result\n");
            memset(ctrl, 0, ctrl_size);
            //vendor_storage_read(ctrl_size, ctrl, VENDOR_PCBA_RESULT_ID);
            break;

        case 0x00000005:
            printf("get test result\n");
            memset(ctrl, 0, ctrl_size);
            //vendor_storage_read(ctrl_size, ctrl, VENDOR_TEST_RESULT_ID);
            break;

        case 0x00000006:
            uvc_get_model_name(ctrl, ctrl_size);
            break;

        case 0x00000007:
            printf("set image state: %d\n", ctrl[4]);
            uvc_set_user_mirror_state(ctrl[4], dev->video_id);
            if (ctrl[4] == 2 && uvc_ov_set_hflip_cb)
                uvc_ov_set_hflip_cb(1);
            if (ctrl[4] == 3 && uvc_ov_set_vflip_cb)
                uvc_ov_set_vflip_cb(1);
            break;

        case 0x00000008:
            printf("get support list\n");
            uvc_get_support_list(ctrl, ctrl_size);
            break;

        case 0x00000009:
            uvc_get_current_output(ctrl, ctrl_size, dev);
            break;

        case 0x0000000A:
            printf("uvc set pro time\n");
            value = (unsigned int*)(&ctrl[4]);
            if (uvc_set_pro_time_cb)
                uvc_set_pro_time_cb(value[0]);
            break;

        case 0x0000000B:
            printf("uvc set denoise\n");
            value = (unsigned int*)(&ctrl[4]);
            if (uvc_set_denoise_cb)
                uvc_set_denoise_cb(value[0], value[1]);
            break;

        case 0x0000000C:
            printf("uvc set pro current\n");
            value = (unsigned int*)(&ctrl[4]);
            if (uvc_set_pro_current_cb)
                uvc_set_pro_current_cb(value[0]);
            break;

        default:
            printf("unsupport xu ctrl cs4 command: 0x%08x!\n", command);
            break;
        }
    }

    return 0;
}

static void uvc_xu_ctrl(struct uvc_device *dev, uint8_t cs,
        struct uvc_request_data *data)
{
    switch (cs) {
    case 1:
        uvc_xu_ctrl_cs1(dev, data);
        break;

    case 2:
        uvc_xu_ctrl_cs2(dev, data);
        break;

    case 3:
        uvc_xu_ctrl_cs3(dev, data);
        break;

    case 4:
        uvc_xu_ctrl_cs4(dev, data);
        break;

    default:
        break;
    }
}

static int
uvc_events_process_control_data(struct uvc_device *dev,
                                uint8_t cs, uint8_t entity_id,
                                struct uvc_request_data *data)
{
    unsigned int *val = (unsigned int *)data->data;

    if (!dev->sat.is_file)
        printf(" data = %d, length = %d  , current_cs = %d\n", *val , data->length, dev->cs);

    switch (entity_id) {
        /* Processing unit 'UVC_VC_PROCESSING_UNIT'. */
    case 2:
        uvc_pu_ctrl(dev, cs, data);
        break;

    case 6:
        uvc_xu_ctrl(dev, cs, data);
        break;

    default:
        break;
    }
    if (!dev->sat.is_file)
        printf("Control Request data phase (cs %02x  data %d entity %02x)\n", cs, *val, entity_id);
    return 0;
}

static void *
uvc_video_process_thread(void *arg)
{
    struct uvc_device *udev = (struct uvc_device *)arg;
    fd_set fdsu;
    struct timeval tv;
    int ret;

    while (uvc_get_user_run_state(udev->video_id) && udev->vp_flag) {
        if (udev->sat.is_file) {
            pthread_mutex_lock(&udev->sat_mutex);
            if (udev->sat.is_file) {
                printf("wait file transfer ...\n");
                pthread_cond_wait(&udev->sat_cond, &udev->sat_mutex);
            }
            pthread_mutex_unlock(&udev->sat_mutex);
        }

        FD_ZERO(&fdsu);

        /* We want data events on UVC interface.. */
        FD_SET(udev->uvc_fd, &fdsu);

        fd_set dfds = fdsu;
        /* Timeout. */
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        ret = select(udev->uvc_fd + 1, NULL, &dfds, NULL, &tv);
        udev->data_select_count++;

        if (-1 == ret) {
            printf("select error %d, %s\n",
                   errno, strerror (errno));
            if (EINTR == errno)
                continue;

            break;
        }

        if (0 == ret) {
            if (udev->bulk) {
                printf("%d: bulk select timeout\n", udev->video_id);
                if (uvc_get_uvc_stream(udev->video_id)) {
                    if (udev->bulk_timeout++ == 50) {
                        udev->bulk_timeout = 0;
                        uvc_pthread_signal();
                        break;
                    }
                } else {
                    udev->bulk_timeout = 0;
                }
                continue;
            }
            printf("select timeout\n");
            break;
        }

        if (FD_ISSET(udev->uvc_fd, &dfds)) {
            udev->data_count++;
            if (uvc_video_process(udev))
                uvc_pthread_signal();
        }
    }
    pthread_exit(NULL);
}

static int
uvc_video_process_thread_init(struct uvc_device *udev)
{
    udev->vp_flag = true;
    if (pthread_create(&udev->vp, NULL, uvc_video_process_thread, udev))
        return -1;
    return 0;
}

static void
uvc_video_process_thread_exit(struct uvc_device *udev)
{
    if (!udev->vp_flag)
        return;
    udev->vp_flag = false;
    uvc_video_id_cond_signal(udev->video_id);
    printf("join uvc_video_process_thread ...\n");
    pthread_join(udev->vp, NULL);
    printf("join uvc_video_process_thread ok.\n");
}

static int
uvc_events_process_data(struct uvc_device *dev, struct uvc_request_data *data)
{
    const struct uvc_streaming_control *ctrl =
        (const struct uvc_streaming_control *)&data->data;
    struct uvc_streaming_control *target;
    int ret = 0;

    switch (dev->control) {
    case UVC_VS_PROBE_CONTROL:
        printf("%d: setting probe control, length = %d\n", dev->video_id, data->length);
        target = &dev->probe;
        break;

    case UVC_VS_COMMIT_CONTROL:
        printf("%d: setting commit control, length = %d\n", dev->video_id, data->length);
        target = &dev->commit;
        break;

    default:
        if (!dev->sat.is_file) {
            printf("setting unknown control, length = %d\n", data->length);
            printf("cs: %u, entity_id: %u\n", dev->cs, dev->entity_id);
        }
        ret = uvc_events_process_control_data(dev,
                                              dev->cs,
                                              dev->entity_id, data);
        if (ret < 0)
            goto err;

        return 0;
    }

    uvc_fill_streaming_control(dev, target, ctrl->bFormatIndex,
                               ctrl->bFrameIndex, ctrl->dwFrameInterval);

    if (dev->control == UVC_VS_COMMIT_CONTROL) {
        const struct uvc_function_config_format *format;
        const struct uvc_function_config_frame *frame;
        struct v4l2_pix_format pixfmt;

        if (uvc_video_get_uvc_process(dev->video_id))
            return 0;

        format = &dev->fc->streaming.formats[target->bFormatIndex-1];
        frame = &format->frames[target->bFrameIndex-1];

        dev->fcc = format->fcc;
        dev->width = frame->width;
        dev->height = frame->height;

        /*
         * Try to set the default format at the V4L2 video capture
         * device as requested by the user.
         */
        memset(&pixfmt, 0, sizeof pixfmt);
        pixfmt.width = frame->width;
        pixfmt.height = frame->height;
        pixfmt.pixelformat = format->fcc;
        pixfmt.field = V4L2_FIELD_ANY;

        switch (format->fcc) {
        case V4L2_PIX_FMT_YUYV:
            pixfmt.sizeimage = (pixfmt.width * pixfmt.height * 2);
            break;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_H264:
            pixfmt.sizeimage = pixfmt.width * pixfmt.height * 3 / 2;
            break;
        }

        uvc_set_user_resolution(pixfmt.width, pixfmt.height, dev->video_id);
        uvc_set_user_fcc(pixfmt.pixelformat, dev->video_id);
        if (uvc_buffer_init(dev->video_id))
            goto err;

        /*
         * As per the new commit command received from the UVC host
         * change the current format selection at both UVC and V4L2
         * sides.
         */
        ret = uvc_video_set_format(dev);
        if (ret < 0)
            goto err;

        if (dev->bulk) {
            ret = uvc_handle_streamon_event(dev);
            if (ret < 0)
                goto err;
            if (uvc_video_process_thread_init(dev))
                abort();
            uvc_video_open(dev, pixfmt.width, pixfmt.height,
                           10000000 / target->dwFrameInterval);

//            set_uvc_control_start(dev->video_id, pixfmt.width, pixfmt.height,
//                                  10000000 / target->dwFrameInterval, dev->continuous);
        }
    }

    return 0;

err:
    return ret;
}

static void
uvc_events_process(struct uvc_device *dev)
{
    struct v4l2_event v4l2_event;
    struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;
    struct uvc_request_data resp;
    int ret = 0;

    ret = ioctl(dev->uvc_fd, VIDIOC_DQEVENT, &v4l2_event);
    if (ret < 0) {
        printf("VIDIOC_DQEVENT failed: %s (%d)\n", strerror(errno),
               errno);
        return;
    }

    memset(&resp, 0, sizeof resp);
    resp.length = -EL2HLT;

    switch (v4l2_event.type) {
    case UVC_EVENT_CONNECT:
        return;

    case UVC_EVENT_DISCONNECT:
        dev->uvc_shutdown_requested = 1;
        printf("UVC: Possible USB shutdown requested from "
               "Host, seen via UVC_EVENT_DISCONNECT\n");
        uvc_pthread_signal();
        return;

    case UVC_EVENT_SETUP:
        uvc_events_process_setup(dev, &uvc_event->req, &resp);
        break;

    case UVC_EVENT_DATA:
        ret = uvc_events_process_data(dev, &uvc_event->data);
        if (ret < 0)
            break;

        return;

    case UVC_EVENT_STREAMON:
        if (dev->bulk)
            return;
        uvc_handle_streamon_event(dev);
        if (uvc_video_process_thread_init(dev))
            abort();
        return;

    case UVC_EVENT_STREAMOFF:
        if (dev->bulk)
            return;
        uvc_video_process_thread_exit(dev);

        /* ... and now UVC streaming.. */
        if (dev->is_streaming) {
            uvc_video_stream(dev, 0);
            uvc_uninit_device(dev);
            uvc_video_reqbufs(dev, 0);
            dev->is_streaming = 0;
            dev->first_buffer_queued = 0;
        }

        uvc_buffer_deinit(dev->video_id);

        return;
    case UVC_EVENT_RESUME:
        return;

    case UVC_EVENT_SUSPEND:
        return;
    }

    ret = ioctl(dev->uvc_fd, UVCIOC_SEND_RESPONSE, &resp);
    if (ret < 0) {
        printf("UVCIOC_S_EVENT failed: %s (%d)\n", strerror(errno),
               errno);
        return;
    }
}

static void
uvc_events_init(struct uvc_device *dev)
{
    struct v4l2_event_subscription sub;

    printf("%s\n", __func__);
    uvc_fill_streaming_control(dev, &dev->probe, 1, 1, 0);
    uvc_fill_streaming_control(dev, &dev->commit, 1, 1, 0);

    if (dev->bulk)
        /* FIXME Crude hack, must be negotiated with the driver. */
        dev->probe.dwMaxPayloadTransferSize =
            dev->commit.dwMaxPayloadTransferSize = UVC_MAX_PAYLOAD_SIZE;

    memset(&sub, 0, sizeof sub);
    sub.type = UVC_EVENT_SETUP;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_DATA;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMON;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMOFF;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_RESUME;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_SUSPEND;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_CONNECT;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_DISCONNECT;
    ioctl(dev->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
}

void *
uvc_state_check_thread(void *arg)
{
    struct uvc_device *udev = (struct uvc_device *)arg;
    unsigned long long int qbuf_count = 0;
    unsigned long long int dqbuf_count = 0;
    unsigned long long int data_count = 0;
    struct timeval now;
    struct timespec timeout;

    while (uvc_get_user_run_state(udev->video_id) && udev->check_flag) {
        if (qbuf_count == udev->qbuf_count
            || dqbuf_count == udev->dqbuf_count
            || data_count == udev->data_count) {
            printf("%d: is_streaming = %d, qbuf_count = %llu, dqbuf_count = %llu,"
                    " data_count = %llu, data_select_count = %llu\n",
                    udev->video_id, udev->is_streaming, udev->qbuf_count, udev->dqbuf_count,
                    udev->data_count, udev->data_select_count);
        } else {
            qbuf_count = udev->qbuf_count;
            dqbuf_count = udev->dqbuf_count;
            data_count = udev->data_count;
        }
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec + 60;
        timeout.tv_nsec = now.tv_usec * 1000;
        pthread_mutex_lock(&udev->check_mutex);
        if (udev->check_flag)
            pthread_cond_timedwait(&udev->check_cond, &udev->check_mutex, &timeout);
        pthread_mutex_unlock(&udev->check_mutex);
    }
    pthread_exit(NULL);
}

int
uvc_gadget_main(struct uvc_function_config *fc)
{
    struct uvc_device *udev = NULL;
    struct timeval tv;
    char uvc_devname[32] = {0};
    fd_set fdsu;
    int ret;
    int bulk_mode = 1;
    int nbufs = 2;
    /* USB speed related params */
    int mult = 0;
    int burst = 0;
    enum usb_device_speed speed = USB_SPEED_HIGH;  /* High-Speed */
    enum io_method uvc_io_method = IO_METHOD_MMAP;
    pthread_t t_check = 0;
    int id = fc->video;

    snprintf(uvc_devname, sizeof(uvc_devname), "/dev/video%d", id);

    /* Open the UVC device. */
    ret = uvc_open(&udev, uvc_devname);
    if (udev == NULL || ret < 0)
        return 1;

    udev->fc = fc;
    udev->uvc_devname = uvc_devname;
    udev->video_id = id;
    udev->video_src = fc->video_src;

    /* Set parameters as passed by user. */
    udev->width = 1280;
    udev->height = 720;
    udev->fcc = V4L2_PIX_FMT_MJPEG;


    uvc_set_user_fcc(udev->fcc, udev->video_id);
    udev->io = uvc_io_method;
    udev->bulk = bulk_mode;
    udev->nbufs = nbufs;
    udev->mult = mult;
    udev->burst = burst;
    udev->speed = speed;

    udev->control = 0;

    /* UVC standalone setup. */
    udev->run_standalone = 1;

    switch (speed) {
    case USB_SPEED_FULL:
        /* Full Speed. */
        if (bulk_mode)
            udev->maxpkt = 64;
        else
            udev->maxpkt = 1023;
        break;

    case USB_SPEED_HIGH:
        /* High Speed. */
        if (bulk_mode)
            udev->maxpkt = 512;
        else
            udev->maxpkt = 1024;
        break;

    case USB_SPEED_SUPER:
    default:
        /* Super Speed. */
        if (bulk_mode)
            udev->maxpkt = 1024;
        else
            udev->maxpkt = 1024;
        break;
    }

    /* Init UVC events. */
    uvc_events_init(udev);

    uvc_set_user_run_state(true, udev->video_id);

    pthread_mutex_init(&udev->check_mutex, NULL);
    pthread_cond_init(&udev->check_cond, NULL);
    udev->check_flag = true;
    if (pthread_create(&t_check, NULL, uvc_state_check_thread, udev)) {
        printf("%s create thread failed\n", __func__);
        abort();
    }

    udev->fcc = 0;
    udev->width = 0;
    udev->height = 0;

    while (uvc_get_user_run_state(udev->video_id)) {
        FD_ZERO(&fdsu);

        /* We want both setup and data events on UVC interface.. */
        FD_SET(udev->uvc_fd, &fdsu);

        fd_set efds = fdsu;

        /* Timeout. */
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ret = select(udev->uvc_fd + 1, NULL, NULL, &efds, &tv);
        if (-1 == ret) {
            printf("select error %d, %s\n",
                   errno, strerror (errno));
            if (EINTR == errno)
                continue;

            break;
        }

        if (0 == ret) {
            if (udev->bulk)
                continue;
            printf("select timeout\n");
            break;
        }

        if (FD_ISSET(udev->uvc_fd, &efds)) {
            uvc_events_process(udev);
        }
    }

    if (t_check) {
        pthread_mutex_lock(&udev->check_mutex);
        udev->check_flag = false;
        pthread_cond_signal(&udev->check_cond);
        pthread_mutex_unlock(&udev->check_mutex);
        printf("%d: join uvc check thread\n", udev->video_id);
        pthread_join(t_check, NULL);
        printf("%d: join uvc check thread ok\n", udev->video_id);
    }
    pthread_mutex_destroy(&udev->check_mutex);
    pthread_cond_destroy(&udev->check_cond);

    if (udev->bulk)
        uvc_video_process_thread_exit(udev);

    if (udev->is_streaming) {
        /* ... and now UVC streaming.. */
        uvc_video_stream(udev, 0);
        uvc_uninit_device(udev);
        uvc_video_reqbufs(udev, 0);
        udev->is_streaming = 0;
    }

    uvc_close(udev);

    uvc_buffer_deinit(id);

    if (uvc_camera_control_cb)
        uvc_camera_control_cb(0);

    return 0;
}

static void uvc_init_data(void)
{
    cif_enable = false;
    cif_vga_enable = false;
    g_cif_cnt = 0;
    cif_depth_ir_start = false;

    uvc_depth_cnt = -1;
    uvc_rgb_cnt = -1;
    uvc_ir_cnt = -1;
}

static void get_uvc_index_to_name(unsigned int index, char *name)
{
    if (strstr(name, "depth") || strstr(name, "DEPTH")) {
        uvc_depth_cnt = index;
    } else if (strstr(name, "rgb") || strstr(name, "RGB")) {
        uvc_rgb_cnt = index;
    } else if (strstr(name, "ir") || strstr(name, "IR")) {
        uvc_ir_cnt = index;
    }
}

static void uvc_ctrl_close_device(void)
{
    if (uvc_video_close_cb)
        uvc_video_close_cb(ctrl_device);
    if (uvc_video_deinit_cb)
        uvc_video_deinit_cb(true);
}

static void uvc_pthread_wait(void)
{
    pthread_mutex_lock(&run_mutex);
    if (run_flag)
        pthread_cond_wait(&run_cond, &run_mutex);
    pthread_mutex_unlock(&run_mutex);
}

static void *uvc_pthread_run(void *arg)
{
    struct uvc_function_config *fc[CAM_MAX_NUM];
    unsigned int i;

    while (run_flag) {
        uvc_init_data();
        for (i = 0; i < CAM_MAX_NUM; i++) {
            fc[i] = configfs_parse_uvc_function(i);
            if (fc[i]) {
                get_uvc_index_to_name(fc[i]->index, fc[i]->dev_name);
                uvc_video_id_add(fc[i]);
            }
        }

        /* The first UVC was not found to indicate that there are no uvc devices */
        if (fc[0] == NULL) {
            printf("Not found UVC device.\n");
            sleep(3);
            continue;
        }
        uvc_pthread_wait();

        for (i = 0; i < CAM_MAX_NUM; i++) {
            if (fc[i])
                configfs_free_uvc_function(fc[i]);
        }
        uvc_ctrl_close_device();
        uvc_video_id_exit_all();
    }
    pthread_exit(NULL);
}

int uvc_pthread_create(void)
{
    run_flag = true;
    if (pthread_create(&run_id, NULL, uvc_pthread_run, NULL)) {
        printf("%s: pthread_create failed!\n", __func__);
        return -1;
    }

    /* Wait for the usb configuration to complete */
    sleep(1);
    return 0;
}

void uvc_pthread_exit(void)
{
    run_flag = false;
    uvc_pthread_signal();
    pthread_join(run_id, NULL);
}
