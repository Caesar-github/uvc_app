/*
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL), available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "uvc_encode.h"
#define UVC_MAX_WIDTH 1920
#define UVC_MAX_HEIGHT 1080
#define UVC_BPP       16

extern "C" {
}

static int rga_buffer_init(int width, int height, int bpp, bo_t *rga_buf_bo,
                           int *rga_buf_fd)
{
    int ret = -1;

    ret = c_RkRgaInit();
    if (ret) {
        printf("c_RkRgaInit error : %s\n", strerror(errno));
        return ret;
    }
    ret = c_RkRgaGetAllocBuffer(rga_buf_bo, width, height, bpp);
    if (ret) {
        printf("c_RkRgaGetAllocBuffer error : %s\n", strerror(errno));
        return ret;
    }
    ret = c_RkRgaGetMmap(rga_buf_bo);
    if (ret) {
        printf("c_RkRgaGetMmap error : %s\n", strerror(errno));
        return ret;
    }
    ret = c_RkRgaGetBufferFd(rga_buf_bo, rga_buf_fd);
    if (ret)
        printf("c_RkRgaGetBufferFd error : %s\n", strerror(errno));

    return ret;
}

static int rga_buffer_deinit(bo_t *rga_buf_bo, int rga_buf_fd)
{
    int ret = -1;

    close(rga_buf_fd);
    ret = c_RkRgaUnmap(rga_buf_bo);
    if (ret)
        printf("c_RkRgaUnmap error : %s\n", strerror(errno));

    ret = c_RkRgaFree(rga_buf_bo);

    return ret;
}

int uvc_encode_init(struct uvc_encode *e)
{
    memset(e, 0, sizeof(*e));
    e->video_id = -1;

    if (vpu_nv12_encode_jpeg_init_ext(&e->encode, 640, 360, 7)) {
        printf("%s: %d failed!\n", __func__, __LINE__);
        return -1;
    }

    rga_buffer_init(UVC_MAX_WIDTH, UVC_MAX_HEIGHT, UVC_BPP,
                    &e->rga_buf_bo, &e->rga_buf_fd);

    return 0;
}

void uvc_encode_exit(struct uvc_encode *e)
{
    vpu_nv12_encode_jpeg_done(&e->encode);
    rga_buffer_deinit(&e->rga_buf_bo, e->rga_buf_fd);
}

static int uvc_rga_rotation(unsigned int src_fmt, unsigned char* src_buf,
                            int src_w, int src_h,
                            unsigned int dst_fmt, int dst_fd, int dst_w,
                            int dst_h, int rotation)
{
    int ret;
    rga_info_t src;
    rga_info_t dst;

    if (!src_w || !src_h || !dst_w || !dst_h)
        return -1;

    memset(&src, 0, sizeof(rga_info_t));
    src.fd = -1;
    src.virAddr = src_buf;
    src.mmuFlag = 1;

    memset(&dst, 0, sizeof(rga_info_t));
    dst.fd = dst_fd;
    dst.mmuFlag = 1;

    rga_set_rect(&src.rect, 0, 0, src_w, src_h, src_w, src_h, src_fmt);
    src.rotation = rotation;
    rga_set_rect(&dst.rect, 0, 0, dst_w, dst_h, dst_w, dst_h, dst_fmt);
    ret = c_RkRgaBlit(&src, &dst, NULL);
    if (ret)
        printf("c_RkRgaBlit0 error : %s\n", strerror(errno));

    return ret;
}


int uvc_pre_process(struct uvc_encode* e, int in_width, int in_height,
                    void* in_virt, int in_fd, int in_fmt)
{
    int dst_width, dst_height;
    int out_fmt = in_fmt;
    int ret;

    uvc_set_uvc_stream(e->video_id, true);
    uvc_get_user_resolution(&dst_width, &dst_height, e->video_id);

    if ((in_width == dst_height) && (in_height == dst_width)) {
        uvc_rga_rotation(in_fmt, (unsigned char *)in_virt, in_width, in_height,
            out_fmt, e->rga_buf_fd, dst_width, dst_height, HAL_TRANSFORM_ROT_90);
        e->out_virt = e->rga_buf_bo.ptr;
        e->out_fd = e->rga_buf_fd;
    } else {
        e->out_virt = in_virt;
        e->out_fd = in_fd;
    }

    return ret;
}

bool uvc_encode_process(struct uvc_encode *e)
{
    int ret = 0;
    unsigned int fcc;
    int size;
    void* extra_data = nullptr;
    size_t extra_size = 0;
    int width, height;
    int jpeg_quant;
    void* virt = e->out_virt;
    int fd = e->out_fd;

    if (!uvc_get_user_run_state(e->video_id) || !uvc_video_get_uvc_process(e->video_id))
        return false;

    uvc_get_user_resolution(&width, &height, e->video_id);
    fcc = uvc_get_user_fcc(e->video_id);
    switch (fcc) {
    case V4L2_PIX_FMT_YUYV:
        break;
    case V4L2_PIX_FMT_MJPEG:
        if (e->encode.width != width || e->encode.height != height) {
            vpu_nv12_encode_jpeg_done(&e->encode);
            jpeg_quant = (height >= 1080) ? 7 : 10;
            if (vpu_nv12_encode_jpeg_init_ext(&e->encode, width, height, jpeg_quant)) {
                printf("%s: %d failed!\n", __func__, __LINE__);
                return false;
            }
        }
        size = width * height * 3 / 2;
        ret = vpu_nv12_encode_jpeg_doing(&e->encode, virt, fd, size);
        if (ret)
            return false;

        break;
    case V4L2_PIX_FMT_H264: 
        break;
    default:
        printf("%s: not support fcc: %u\n", __func__, fcc);
        break;
    }

    return true;
}

bool uvc_write_process(struct uvc_encode *e)
{
    bool ret = false;
    unsigned int fcc;
    int size;
    void* extra_data = nullptr;
    size_t extra_size = 0;
    int width, height;
    int jpeg_quant;
    void* virt = e->out_virt;

    if (!uvc_get_user_run_state(e->video_id) || !uvc_video_get_uvc_process(e->video_id))
        return false;

    uvc_get_user_resolution(&width, &height, e->video_id);
    fcc = uvc_get_user_fcc(e->video_id);
    switch (fcc) {
    case V4L2_PIX_FMT_YUYV:
        size = width * height * 2;
        ret = uvc_buffer_write(extra_data, extra_size, virt, size, fcc, e->video_id, 0);
        break;
    case V4L2_PIX_FMT_MJPEG:
        if (e->user_data && e->user_size > 0) {
            extra_data = e->user_data;
            extra_size = e->user_size;
        }
        ret = uvc_buffer_write(extra_data, extra_size, e->encode.enc_out_data,
                         e->encode.enc_out_length, fcc, e->video_id, e->frame_size_off);
        break;
    case V4L2_PIX_FMT_H264:
        break;
    default:
        printf("%s: not support fcc: %u\n", __func__, fcc);
        break;
    }

    return ret;
}

