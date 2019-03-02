/*
 * V4L2 video capture example
 * AUTHOT : Leo Wen
 * DATA : 2019-02-15
 */
#ifndef __RKCAM_CAPTURE_H__
#define __RKCAM_CAPTURE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h> /* getopt_long() */
#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <dlfcn.h>

#include <linux/videodev2.h>
#include "rkisp_control_loop.h"
#include "mediactl.h"
#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
    void *start;
    size_t length;
};

struct RKisp_media_ctl
{
    /* media controller */
    struct media_device *controller;
    struct media_entity *isp_subdev;
    struct media_entity *isp_params_dev;
    struct media_entity *isp_stats_dev;
    struct media_entity *sensor_subdev;
};

int rkcam_init(int node, int width, int height, struct buffer **buffers, void **rkisp_engine);
int rkcam_deinit(int fd, struct buffer **buffers, void **rkisp_engine);
int read_frame(int fd, struct buffer *buffers, void* desbuff);

#ifdef __cplusplus
}
#endif

#endif
