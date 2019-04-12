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

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "uvc_control.h"
#include "uvc_encode.h"
#include "uvc_video.h"

#define SYS_ISP_NAME "isp"
#define SYS_CIF_NAME "cif"

struct uvc_ctrl {
    int id;
    int width;
    int height;
    int fps;
};

static struct uvc_ctrl uvc_ctrl[2];
struct uvc_encode uvc_enc;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static bool is_uvc_video(void *buf)
{
    if (strstr(buf, "usb") || strstr(buf, "gadget"))
        return true;
    else
        return false;
}

void check_video_id(void)
{
    FILE *fp = NULL;
    char buf[1024];
    bool exist = false;
    bool isp_exist = false;
    bool cif_exist = false;
    int i = 5;

    memset(&uvc_ctrl, 0, sizeof(uvc_ctrl));
    while (!exist && i--) {
        printf("%s\n", __func__);
        fp = popen("cat /sys/class/video4linux/video*/name", "r");
        if (fp) {
            while (fgets(buf, sizeof(buf), fp)) {
                if (is_uvc_video(buf)) {
                    exist = true;
                    break;
                }
            }
            pclose(fp);
        } else {
            printf("/sys/class/video4linux/video*/name isn't exist.\n");
            abort();
        }
        usleep(100000);
    }
    fp = popen("cat /sys/class/video4linux/video*/name", "r");
    if (fp) {
        int id = 0;
        while (fgets(buf, sizeof(buf), fp)) {
            if (is_uvc_video(buf)) {
                if (!uvc_ctrl[0].id)
                    uvc_ctrl[0].id = id;
                else if (!uvc_ctrl[1].id)
                    uvc_ctrl[1].id = id;
                else
                    printf("unexpect uvc video!\n");
            }
            id++;
        }
        pclose(fp);
    }
}

void add_uvc_video()
{
    if (uvc_ctrl[0].id)
        uvc_video_id_add(uvc_ctrl[0].id);
    if (uvc_ctrl[1].id)
        uvc_video_id_add(uvc_ctrl[1].id);
}

void uvc_control_init(int width, int height)
{
    pthread_mutex_lock(&lock);
    memset(&uvc_enc, 0, sizeof(uvc_enc));
    if (uvc_encode_init(&uvc_enc, width, height)) {
        printf("%s fail!\n", __func__);
        abort();
    }
    pthread_mutex_unlock(&lock);
}

void uvc_control_exit()
{
    pthread_mutex_lock(&lock);
    uvc_encode_exit(&uvc_enc);
    memset(&uvc_enc, 0, sizeof(uvc_enc));
    pthread_mutex_unlock(&lock);
}

void uvc_read_camera_buffer(void *cam_buffer, size_t cam_size)
{
    void *buffer = NULL;
    size_t size;

    pthread_mutex_lock(&lock);
    buffer = uvc_enc.src_virt;
    size = uvc_enc.src_size;
    if (buffer && cam_size <= size) {
        memcpy(buffer, cam_buffer, cam_size);
        uvc_enc.video_id = uvc_video_id_get(0);
        uvc_encode_process(&uvc_enc);
    } else if (size) {
        printf("%s: size = %d, cam_size = %d\n", __func__, size, cam_size);
    }
    pthread_mutex_unlock(&lock);
}
