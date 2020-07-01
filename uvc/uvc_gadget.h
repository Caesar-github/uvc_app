/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * UVC protocol handling
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef _UVC_GADGET_H_
#define _UVC_GADGET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>
#include "uvc_configfs.h"

/* Enable debug prints. */
#undef ENABLE_BUFFER_DEBUG
#undef ENABLE_USB_REQUEST_DEBUG

#define CLEAR(x)    memset (&(x), 0, sizeof (x))
#define __max(a, b)   (((a) > (b)) ? (a) : (b))
#define __min(a, b)   (((a) < (b)) ? (a) : (b))

#define clamp(val, min, max) ({                  \
         typeof(val) __val = (val);              \
         typeof(min) __min = (min);              \
         typeof(max) __max = (max);              \
         (void) (&__val == &__min);              \
         (void) (&__val == &__max);              \
         __val = __val < __min ? __min: __val;   \
         __val > __max ? __max: __val; })

#define pixfmtstr(x)    (x) & 0xff, ((x) >> 8) & 0xff, ((x) >> 16) & 0xff, \
         ((x) >> 24) & 0xff

#define CAM_MAX_NUM 3
#define EXT_QUERY_CMD 0x82
#define SAT_FILE_MAX_SIZE 2048
#define UVC_MAX_PAYLOAD_SIZE (512 * 1024)

/*
 * The UVC webcam gadget kernel driver (g_webcam.ko) supports changing
 * the Brightness attribute of the Processing Unit (PU). by default. If
 * the underlying video capture device supports changing the Brightness
 * attribute of the image being acquired (like the Virtual Video, VIVI
 * driver), then we should route this UVC request to the respective
 * video capture device.
 *
 * Incase, there is no actual video capture device associated with the
 * UVC gadget and we wish to use this application as the final
 * destination of the UVC specific requests then we should return
 * pre-cooked (static) responses to GET_CUR(BRIGHTNESS) and
 * SET_CUR(BRIGHTNESS) commands to keep command verifier test tools like
 * UVC class specific test suite of USBCV, happy.
 *
 * Note that the values taken below are in sync with the VIVI driver and
 * must be changed for your specific video capture device. These values
 * also work well in case there in no actual video capture device.
 */
#define PU_BRIGHTNESS_MIN_VAL       0
#define PU_BRIGHTNESS_MAX_VAL       255
#define PU_BRIGHTNESS_STEP_SIZE     1
#define PU_BRIGHTNESS_DEFAULT_VAL   127

#define PU_CONTRAST_MIN_VAL         0
#define PU_CONTRAST_MAX_VAL         65535
#define PU_CONTRAST_STEP_SIZE       1
#define PU_CONTRAST_DEFAULT_VAL     127

#define PU_HUE_MIN_VAL              -32768
#define PU_HUE_MAX_VAL              32767
#define PU_HUE_STEP_SIZE            1
#define PU_HUE_DEFAULT_VAL          0

#define PU_SATURATION_MIN_VAL       0
#define PU_SATURATION_MAX_VAL       65535
#define PU_SATURATION_STEP_SIZE     1
#define PU_SATURATION_DEFAULT_VAL   0

#define PU_SHARPNESS_MIN_VAL        0
#define PU_SHARPNESS_MAX_VAL        255
#define PU_SHARPNESS_STEP_SIZE      1
#define PU_SHARPNESS_DEFAULT_VAL    127

#define PU_GAMMA_MIN_VAL            0
#define PU_GAMMA_MAX_VAL            255
#define PU_GAMMA_STEP_SIZE          1
#define PU_GAMMA_DEFAULT_VAL        127

#define PU_WHITE_BALANCE_TEMPERATURE_MIN_VAL        0
#define PU_WHITE_BALANCE_TEMPERATURE_MAX_VAL        255
#define PU_WHITE_BALANCE_TEMPERATURE_STEP_SIZE      1
#define PU_WHITE_BALANCE_TEMPERATURE_DEFAULT_VAL    127

#define PU_GAIN_MIN_VAL             0
#define PU_GAIN_MAX_VAL             255
#define PU_GAIN_STEP_SIZE           1
#define PU_GAIN_DEFAULT_VAL         0

#define PU_HUE_AUTO_MIN_VAL         0
#define PU_HUE_AUTO_MAX_VAL         255
#define PU_HUE_AUTO_STEP_SIZE       1
#define PU_HUE_AUTO_DEFAULT_VAL     127

#define UVC_EVENT_FIRST         (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_CONNECT       (V4L2_EVENT_PRIVATE_START + 0)
#define UVC_EVENT_DISCONNECT        (V4L2_EVENT_PRIVATE_START + 1)
#define UVC_EVENT_STREAMON      (V4L2_EVENT_PRIVATE_START + 2)
#define UVC_EVENT_STREAMOFF     (V4L2_EVENT_PRIVATE_START + 3)
#define UVC_EVENT_SETUP         (V4L2_EVENT_PRIVATE_START + 4)
#define UVC_EVENT_DATA          (V4L2_EVENT_PRIVATE_START + 5)
#define UVC_EVENT_SUSPEND       (V4L2_EVENT_PRIVATE_START + 6)
#define UVC_EVENT_RESUME        (V4L2_EVENT_PRIVATE_START + 7)
#define UVC_EVENT_LAST          (V4L2_EVENT_PRIVATE_START + 7)

#define MAX_UVC_REQUEST_DATA_LENGTH	4096

#define PREISP_CALIB_ITEM_NUM           24
#define PREISP_CALIB_MAGIC              "#SLM_CALIB_DATA#"
#define PREISP_DCROP_ITEM_NAME          "calib_data.bin"
#define PREISP_DCROP_CALIB_RATIO        192
#define PREISP_DCROP_CALIB_XOFFSET      196
#define PREISP_DCROP_CALIB_YOFFSET      198

#define UVCIOC_SEND_RESPONSE        _IOW('U', 1, struct uvc_request_data)

struct uvc_request_data {
    __s32 length;
    __u8 data[60];
};

struct uvc_event {
    union {
        enum usb_device_speed speed;
        struct usb_ctrlrequest req;
        struct uvc_request_data data;
    };
};

/* ---------------------------------------------------------------------------
 * Generic stuff
 */
typedef struct {
    int id;
    int width;
    int height;
    int fps;
    int img_effect;
    bool dcrop;
    bool continuous;
} AddVideoParam_t;

/* IO methods supported */
enum io_method {
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

/* Buffer representing one video frame */
struct buffer {
    struct v4l2_buffer buf;
    void *start;
    size_t length;
};

struct xu_ctrl_query {
    unsigned char ctrl1[4];
    unsigned char ctrl2[16]; //byte:1 command, 2-3 data length, 4-5 checksum
    unsigned char ctrl4[60];
    unsigned short get_checksum;
    unsigned char data[MAX_UVC_REQUEST_DATA_LENGTH];
    unsigned short checksum;
    unsigned short length;
    unsigned int index;
    char result;
};

struct sat_s{
    bool is_file;
    unsigned short cmd[4];
    unsigned short cmd_cnt;
    unsigned short file_size;
    unsigned short file_checksum;
    unsigned short data[SAT_FILE_MAX_SIZE];
    unsigned short data_cnt;
    struct timeval t0;
    struct timeval t1;
};

/* Represents a UVC based video output device */
struct uvc_device {
    int video_id;
    int video_src;
    /* uvc device specific */
    int uvc_fd;
    int is_streaming;
    int run_standalone;
    char *uvc_devname;

    struct uvc_function_config *fc;
    /* uvc control request specific */
    struct uvc_streaming_control probe;
    struct uvc_streaming_control commit;
    int control;
    struct uvc_request_data request_error_code;
    unsigned int brightness_val;
    unsigned short contrast_val;
    short hue_val;
    unsigned short saturation_val;
    unsigned int sharpness_val;
    unsigned int gamma_val;
    unsigned int white_balance_temperature_val;
    unsigned int gain_val;
    unsigned int hue_auto_val;
    unsigned char power_line_frequency_val;

    struct xu_ctrl_query xu_query;
    struct sat_s sat;

    pthread_mutex_t sat_mutex;
    pthread_cond_t sat_cond;

    /* uvc buffer specific */
    enum io_method io;
    struct buffer *mem;
    struct buffer *dummy_buf;
    unsigned int nbufs;
    unsigned int fcc;
    unsigned int width;
    unsigned int height;

    unsigned int bulk;
    uint8_t color;

    /* USB speed specific */
    int mult;
    int burst;
    int maxpkt;
    enum usb_device_speed speed;

    /* uvc specific flags */
    int first_buffer_queued;
    int uvc_shutdown_requested;

    /* uvc buffer queue and dequeue counters */
    unsigned long long int qbuf_count;
    unsigned long long int dqbuf_count;

    unsigned long long int data_count;
    unsigned long long int data_select_count;

    bool check_flag;
    pthread_mutex_t check_mutex;
    pthread_cond_t check_cond;

    /* v4l2 device hook */
    struct v4l2_device *vdev;
    uint8_t cs;
    uint8_t entity_id;
    struct v4l2_buffer ubuf;

    /* uvc video process */
    pthread_t vp;
    bool vp_flag;

    bool continuous;
    int bulk_timeout;
};

struct calib_item {
    unsigned char name[48];
    unsigned int  offset;
    unsigned int  size;
    unsigned int  temp;
    unsigned int  crc32;
};

struct calib_head {
    unsigned char magic[16];
    unsigned int  version;
    unsigned int  head_size;
    unsigned int  image_size;
    unsigned int  items_number;
    unsigned char reserved0[32];
    unsigned int  hash_len;
    unsigned char hash[32];
    unsigned char reserved1[28];
    unsigned int  sign_tag;
    unsigned int  sign_len;
    unsigned char rsa_hash[256];
    unsigned char reserved2[120];
    struct calib_item item[PREISP_CALIB_ITEM_NUM];
};

int uvc_gadget_main(struct uvc_function_config *fc);

#ifdef __cplusplus
}
#endif

#endif /* _UVC_GADGET_H_ */

