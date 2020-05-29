/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * UVC API
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef _UVC_API_H_
#define _UVC_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "uvc_gadget.h"
#include "uvc_video.h"

#define CAM_DEPTH_ID	0
#define CAM_RGB_ID	1
#define CAM_IR_ID	2

#define UVC_CB_VIDEO_OPEN		0
#define UVC_CB_VIDEO_CLOSE		1
#define UVC_CB_VIDEO_SET_TIME		2
#define UVC_CB_VIDEO_CIF_SET_GAIN	3
#define UVC_CB_VIDEO_GET_FLT		4
#define UVC_CB_VIDEO_ADD		5
#define UVC_CB_VIDEO_DELETE		6
#define UVC_CB_VIDEO_DEINIT		7
#define UVC_CB_VIDEO_SET_FRAME_TYPE	8
#define UVC_CB_VIDEO_SET_IQ_MODE	9
#define UVC_CB_VIDEO_SET_FOCUS_POS	10
#define UVC_CB_VIDEO_SET_IMAGE_EFFECT	11
#define UVC_CB_VIDEO_STREAM_CTRL	12
#define UVC_CB_VIDEO_RESTART_ISP	13
#define UVC_CB_VIDEO_RESTART_CIF	14
#define UVC_CB_IQ_GET_DATA              15
#define UVC_CB_IQ_SET_DATA              16
#define UVC_CB_HUE_SET			17
#define UVC_CB_HUE_GET			18
#define UVC_CB_SAT_DATA			19

/* Callback facilities */
struct uvc_callback {
    void (*video_open)(void *dev);
    void (*video_close)(void *dev);
    void (*video_set_time)(unsigned short t_ms, int frame_cnt);
    void (*video_cif_set_gain)(int gain);
    void (*video_get_flt)(uint8_t flt_g_scenemode, uint8_t flt_g_level);
    int (*video_addvideo)(AddVideoParam_t *param);
    int (*video_deletevideo)(int deviceid);
    void (*video_deinit)(bool black);
    /* void (*video_set_frame_type)(enum disp_frame_type type); */
    void (*video_set_iq_mode)(int mode);
    void (*video_set_focus_pos)(int position);
    void (*video_set_image_effect)(int effect);
    void (*video_stream_control)(int type, int flag);
    void (*video_restart_isp)(void);
    void (*video_restart_cif)(void);
    void (*iq_get_data)(unsigned char *ctrl, unsigned char *data, int len);
    void (*iq_set_data)(unsigned char *data, int len);
    void (*hue_set)(short hue, void *dev);
    short (*hue_get)(void *dev);
    void (*sat_data)(void *buffer, unsigned short size, void *dev);
};

extern void (*uvc_video_open_cb)(void *);
extern void (*uvc_video_close_cb)(void *);
extern void (*uvc_video_set_time_cb)(unsigned short, int);
extern void (*uvc_video_cif_set_gain_cb)(int);
extern void (*uvc_video_get_flt_cb)(uint8_t, uint8_t);
extern int (*uvc_video_addvideo_cb)(AddVideoParam_t *);
extern int (*uvc_video_deletevideo_cb)(int);
extern void (*uvc_video_deinit_cb)(bool);
/*extern void (*uvc_video_set_frame_type_cb)(enum disp_frame_type);*/
extern void (*uvc_video_set_iq_mode_cb)(int);
extern void (*uvc_video_set_focus_pos_cb)(int);
extern void (*uvc_video_set_image_effect_cb)(int);
extern void (*uvc_video_stream_control_cb)(int, int);
extern void (*uvc_video_restart_isp_cb)();
extern void (*uvc_video_restart_cif_cb)();
extern void (*uvc_iq_get_data_cb)(unsigned char *, unsigned char *, int);
extern void (*uvc_iq_set_data_cb)(unsigned char *, int);
extern void (*uvc_hue_set_cb)(short, void *);
extern short (*uvc_hue_get_cb)(void *);
extern void (*uvc_sat_data_cb)(void *, unsigned short, void *);
extern void (*uvc_set_one_frame_liveness_cb)(void);
extern void (*uvc_set_continuous_liveness_cb)(int);

extern void *hue_set_device;
extern void *hue_get_device; 
extern void *sat_device; 
extern void *ctrl_device;

/* callback setting function */
static inline void uvc_set_callback(int type, struct uvc_callback cb,
        void *param)
{
    switch (type) {
    case UVC_CB_VIDEO_OPEN:
        uvc_video_open_cb = cb.video_open;
        ctrl_device = param;
        break;

    case UVC_CB_VIDEO_CLOSE:
        uvc_video_close_cb = cb.video_close;
        ctrl_device = param;
        break;

    case UVC_CB_VIDEO_SET_TIME:
        uvc_video_set_time_cb = cb.video_set_time;
        break;

    case UVC_CB_VIDEO_CIF_SET_GAIN:
        uvc_video_cif_set_gain_cb = cb.video_cif_set_gain;
        break;

    case UVC_CB_VIDEO_GET_FLT:
        uvc_video_get_flt_cb = cb.video_get_flt;
        break;

    case UVC_CB_VIDEO_ADD:
        uvc_video_addvideo_cb = cb.video_addvideo;
        break;

    case UVC_CB_VIDEO_DELETE:
        uvc_video_deletevideo_cb = cb.video_deletevideo;
        break;

    case UVC_CB_VIDEO_DEINIT:
        uvc_video_deinit_cb = cb.video_deinit;
        break;

    case UVC_CB_VIDEO_SET_FRAME_TYPE:
        /* uvc_video_set_frame_type_cb = cb.video_set_frame_type;*/
        break;

    case UVC_CB_VIDEO_SET_IQ_MODE:
        uvc_video_set_iq_mode_cb = cb.video_set_iq_mode;
        break;

    case UVC_CB_VIDEO_SET_FOCUS_POS:
        uvc_video_set_focus_pos_cb = cb.video_set_focus_pos;
        break;

    case UVC_CB_VIDEO_SET_IMAGE_EFFECT:
        uvc_video_set_image_effect_cb = cb.video_set_image_effect;
        break;

    case UVC_CB_VIDEO_STREAM_CTRL:
        uvc_video_stream_control_cb = cb.video_stream_control;
        break;

    case UVC_CB_VIDEO_RESTART_ISP:
        uvc_video_restart_isp_cb = cb.video_restart_isp;
        break;

    case UVC_CB_VIDEO_RESTART_CIF:
        uvc_video_restart_cif_cb = cb.video_restart_cif;
        break;

    case UVC_CB_IQ_GET_DATA:
        uvc_iq_get_data_cb = cb.iq_get_data;
        break;

    case UVC_CB_IQ_SET_DATA:
        uvc_iq_set_data_cb = cb.iq_set_data;
        break;

    case UVC_CB_HUE_SET:
        uvc_hue_set_cb = cb.hue_set;
        hue_set_device = param;
        break;

    case UVC_CB_HUE_GET:
        uvc_hue_get_cb = cb.hue_get;
        hue_get_device = param;
        break;

    case UVC_CB_SAT_DATA:
        uvc_sat_data_cb = cb.sat_data;
        sat_device = param;
        break;
    }
}

static inline int get_uvc_depth_cnt(void)
{
#ifdef CAM_DEPTH_ID
    return CAM_DEPTH_ID;
#else
    return -1;
#endif
}

static inline int get_uvc_ir_cnt(void)
{
#ifdef CAM_RGB_ID
    return CAM_RGB_ID;
#else
    return -1;
#endif
}

static inline int get_uvc_rgb_cnt(void)
{
#ifdef CAM_IR_ID
    return CAM_IR_ID;
#else
    return -1;
#endif
}

bool get_cif_vga_enable(void);
bool get_cif_ir_depth_enable(void);

int uvc_pthread_create(void);
void uvc_pthread_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* _UVC_API_H_ */
