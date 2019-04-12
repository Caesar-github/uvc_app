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
#include "uvc_video.h"
#include <stdio.h>
#include <stdlib.h>

int uvc_encode_init(struct uvc_encode *e, int width, int height)
{
    printf("%s: width = %d, height = %d\n", __func__, width, height);
    memset(e, 0, sizeof(*e));
    e->video_id = -1;
    e->width = -1;
    e->height = -1;
    e->width = width;
    e->height = height;
    e->src_virt = NULL;
    mpi_enc_cmd_config_mjpg(&e->mpi_cmd, width, height);
    if (mpi_enc_test_init(&e->mpi_cmd, &e->mpi_data) != MPP_OK)
        return -1;
    e->src_virt = mpp_buffer_get_ptr(e->mpi_data->frm_buf);
    e->src_size = mpp_buffer_get_size(e->mpi_data->frm_buf);

    return 0;
}

void uvc_encode_exit(struct uvc_encode *e)
{
    mpi_enc_test_deinit(&e->mpi_data);
    e->video_id = -1;
    e->width = -1;
    e->height = -1;
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
    void* virt = e->src_virt;
    void* hnd = NULL;

    if (!uvc_get_user_run_state(e->video_id) || !uvc_buffer_write_enable(e->video_id))
        return false;

    uvc_get_user_resolution(&width, &height, e->video_id);
    fcc = uvc_get_user_fcc(e->video_id);
    switch (fcc) {
    case V4L2_PIX_FMT_YUYV:
        size = width * height * 2;
        uvc_buffer_write(0, extra_data, extra_size, virt, size, fcc, e->video_id);
        break;
    case V4L2_PIX_FMT_MJPEG:
        if (mpi_enc_test_run(&e->mpi_data) == MPP_OK) {
            uvc_buffer_write(0, extra_data, extra_size,
                             e->mpi_data->enc_data, e->mpi_data->enc_len, fcc, e->video_id);
        }
        break;
    case V4L2_PIX_FMT_H264: 
        break;
    default:
        printf("%s: not support fcc: %u\n", __func__, fcc);
        break;
    }

    return true;
}
