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
#include "uvc_control.h"
#include "uvc_video.h"

#define SYS_UVC_NAME "ff300000.usb"

struct uvc_ctrl {
    int out;
    int in;
    bool start;
    bool stop; //unused
    int width;
    int height;
    int fps;
};

static struct uvc_ctrl uvc_ctrl[2] = {
    {-1, -1, false, false, -1, -1, -1},
    {-1, -1, false, false, -1, -1, -1},
};
static bool uvc_stop = false;

void check_video_id(void)
{
    FILE *fp = NULL;
    char buf[1024];
    bool exist = false;

    while (!exist) {
        fp = popen("cat /sys/class/video4linux/video*/name", "r");
        if (fp) {
            while (fgets(buf, sizeof(buf), fp)) {
                if (!strncmp(buf, SYS_UVC_NAME, strlen(SYS_UVC_NAME))) {
                    exist = true;
                    break;
                }
            }
            pclose(fp);
        } else {
            printf("/sys/class/video4linux/video*/name isn't exist.\n");
            abort();
        }
    }
    fp = popen("cat /sys/class/video4linux/video*/name", "r");
    if (fp) {
        int id = 0;
        while (fgets(buf, sizeof(buf), fp)) {
            if (!strncmp(buf, "rkisp11_selfpath", strlen("rkisp11_selfpath")))
                uvc_ctrl[ISP_SEQ].in = id;
            else if (!strncmp(buf, "CIF", strlen("CIF")))
                uvc_ctrl[CIF_SEQ].in = id;
            else if (!strncmp(buf, SYS_UVC_NAME, strlen(SYS_UVC_NAME))) {
                if (uvc_ctrl[0].out == -1)
                    uvc_ctrl[0].out = id;
                else if (uvc_ctrl[1].out == -1)
                    uvc_ctrl[1].out = id;
                else
                    printf("unexpect uvc video!\n");
            }
            id++;
        }
        pclose(fp);
    }
}

static inline void uvc_control_driver(struct uvc_ctrl *ctrl, int seq)
{
    int width = (seq == 0 ? ctrl->width * 2 : ctrl->width);
    if (ctrl->start) {
        //video_record_addvideo(ctrl->in, width, ctrl->height, ctrl->fps);
        ctrl->start = false;
    }
}

void uvc_control(void)
{
    if (uvc_stop) {
        uvc_video_id_exit_all();
        //video_record_deinit(true);
        uvc_stop = false;
        add_uvc_video();
    }
    uvc_control_driver(&uvc_ctrl[CIF_SEQ], CIF_SEQ);
    uvc_control_driver(&uvc_ctrl[ISP_SEQ], ISP_SEQ);
}

void set_uvc_control_start(int video_id, int width, int height, int fps)
{
    if (uvc_video_id_get(CIF_SEQ) == video_id) {
        uvc_ctrl[CIF_SEQ].width = width;
        uvc_ctrl[CIF_SEQ].height = height;
        uvc_ctrl[CIF_SEQ].fps = fps;
        uvc_ctrl[CIF_SEQ].start = true;
    } else if (uvc_video_id_get(ISP_SEQ) == video_id) {
        uvc_ctrl[ISP_SEQ].width = width;
        uvc_ctrl[ISP_SEQ].height = height;
        uvc_ctrl[ISP_SEQ].fps = fps;
        uvc_ctrl[ISP_SEQ].start = true;
    } else
        printf("unexpect uvc!");
}

void set_uvc_control_stop(void)
{
    uvc_stop = true;
}

void add_uvc_video()
{
    if (uvc_ctrl[0].out != -1)
        uvc_video_id_add(uvc_ctrl[0].out);
    if (uvc_ctrl[1].out != -1)
        uvc_video_id_add(uvc_ctrl[1].out);
}

