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

#ifndef __UVC_CONTROL_H__
#define __UVC_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_RK_MODULE
#define ISP_SEQ 1
#define ISP_FMT HAL_FRMAE_FMT_NV12
#define CIF_SEQ 0
#define CIF_FMT HAL_FRMAE_FMT_SBGGR10
#else
#define ISP_SEQ 0
#define ISP_FMT HAL_FRMAE_FMT_SBGGR8
#define CIF_SEQ 1
#define CIF_FMT HAL_FRMAE_FMT_NV12
#endif

void add_uvc_video();
void uvc_control(void);
void check_video_id(void);
void set_uvc_control_start(int video_id, int width, int height, int fps);
void set_uvc_control_stop(void);

#ifdef __cplusplus
}
#endif

#endif
