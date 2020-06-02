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

#ifndef __UVC_ENCODE_H__
#define __UVC_ENCODE_H__

extern "C" {
#include "vpu.h"
}
#include "uvc_video.h"
#include "video_common.h"

#define UVC_STAMP 65536
#define ENABLE_UVC_H264 0

#define SHA_LEN          32
#define SIGN_LEN         64
#define MAX_DATA_SIZE    (1024 * 10)

#pragma pack(1)
struct uvc_ext_data {
    int16_t timestamp_s;
    uint64_t timestamp;
    int32_t adc;
    uint32_t rgb_frm_id;
    uint32_t ir_frm_id;
    uint32_t dp_frm_id;
    uint8_t rgb_sha[SHA_LEN];
    uint8_t ir_sha[SHA_LEN];
    uint8_t dp_sha[SHA_LEN];
    uint8_t sign[SIGN_LEN];
    uint32_t frame_size;
    uint32_t temp_fps;
    uint8_t is_gray;
    uint8_t is_real;
    uint64_t sync_depth;
    uint64_t sync_rgb;
    uint64_t sync_ir;
    int32_t face_left;
    int32_t face_top;
    int32_t face_right;
    int32_t face_bottom;
    uint8_t reserved[20];

    uint32_t version;
    uint16_t data_type;
    uint16_t data_size;
    int8_t data[MAX_DATA_SIZE];
};
#pragma pack()

struct uvc_encode {
    struct vpu_encode encode;
    struct video_drm encode_dst_buff;
    int rga_fd;
    struct video_drm uvc_out;
    struct video_drm uvc_mid;
    struct video_drm uvc_pre;
    void* out_virt;
    int out_fd;
    int video_id;
    void *user_data;
    size_t user_size;
    size_t frame_size_off;
};

int uvc_encode_init(struct uvc_encode *e);
void uvc_encode_exit(struct uvc_encode *e);
int uvc_pre_process(struct uvc_encode* e, int in_width, int in_height,
                    void* in_virt, int in_fd, int in_fmt);
bool uvc_encode_process(struct uvc_encode *e);
bool uvc_write_process(struct uvc_encode *e);

#endif
