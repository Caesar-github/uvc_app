/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * drm buffer handle
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include "drm.h"

#define DRM_DEVICE "/dev/dri/card0"

static int drm_open(void)
{
    int fd;
    fd = open(DRM_DEVICE, O_RDWR);
    if (fd < 0) {
        printf("open %s failed!\n", DRM_DEVICE);
        return -1;
    }
    return fd;
}

static void drm_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

static int drm_ioctl(int fd, int req, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, req, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

static int drm_alloc(int fd, size_t len, size_t align, unsigned int *handle, unsigned int flags)
{
    int ret;
    struct drm_mode_create_dumb dmcb;

    memset(&dmcb, 0, sizeof(struct drm_mode_create_dumb));
    dmcb.bpp = 8;
    dmcb.width = (len + align - 1) & (~(align - 1));
    dmcb.height = 1;
    dmcb.flags = flags;

    if (handle == NULL)
        return -EINVAL;

    ret = drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dmcb);
    if (ret < 0)
        return ret;

    *handle = dmcb.handle;

    return ret;
}

static int drm_free(int fd, unsigned int handle)
{
    struct drm_mode_destroy_dumb data = {
        .handle = handle,
    };
    return drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &data);
}

static void *drm_map_buffer(int fd, unsigned int handle, size_t len)
{
    struct drm_mode_map_dumb dmmd;
    void *buf = NULL;
    int ret;

    memset(&dmmd, 0, sizeof(dmmd));
    dmmd.handle = handle;

    ret = drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &dmmd);
    if (ret) {
        printf("map_dumb failed: %s\n", strerror(ret));
        return NULL;
    }

    buf = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, dmmd.offset);
    if (buf == MAP_FAILED) {
        printf("mmap failed: %s\n", strerror(errno));
        return NULL;
    }

    return buf;
}

static void drm_unmap_buffer(void *buf, size_t len)
{
    if (buf)
        munmap(buf, len);
}

static int drm_handle_to_fd(int fd, unsigned int handle, int *map_fd, unsigned int flags)
{
    int ret;
    struct drm_prime_handle dph;

    memset(&dph, 0, sizeof(struct drm_prime_handle));
    dph.handle = handle;
    dph.fd = -1;
    dph.flags = flags;

    if (map_fd == NULL)
        return -EINVAL;

    ret = drm_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dph);
    if (ret < 0)
        return ret;

    *map_fd = dph.fd;

    if (*map_fd < 0) {
        printf("map ioctl returned negative fd\n");
        return -EINVAL;
    }

    return ret;
}

int video_drm_alloc(struct video_drm *bo, int width, int height,
                    int num, int den)
{
    int ret;

    if (width == 0 || height == 0) {
      printf("uvc width height error %d x %d\n", width, height);
      return -1;
    }

    bo->fd = drm_open();
    if (bo->fd < 0)
        return -1;

    bo->size = width * height * num / den;
    ret = drm_alloc(bo->fd, bo->size, 16, &bo->handle, 0);
    if (ret)
        return -1;

    ret = drm_handle_to_fd(bo->fd, bo->handle, &bo->handle_fd, 0);
    if (ret)
        return -1;

    bo->buffer = (char*)drm_map_buffer(bo->fd, bo->handle, bo->size);
    if (!bo->buffer) {
        printf("drm map buffer fail.\n");
        return -1;
    }

    return 0;
}

int video_drm_free(struct video_drm *bo)
{
    drm_unmap_buffer(bo->buffer, bo->size);
    drm_free(bo->fd, bo->handle);
    drm_close(bo->fd);

    return 0;
}
