/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * drm buffer handle
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */

#ifdef __cplusplus
extern "C"
{
#endif

struct video_drm {
    int fd;
    int handle_fd;
    void *buffer;
    size_t size;
    size_t offset;
    size_t pitch;
    unsigned handle;
};

int video_drm_alloc(struct video_drm *bo, int width, int height, int num, int den);
int video_drm_free(struct video_drm *bo);

#ifdef __cplusplus
}
#endif
