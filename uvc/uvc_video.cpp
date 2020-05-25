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

#include "uvc_video.h"
#include "uvc_gadget.h"
#include "yuv.h"
#include "uvc_api.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/prctl.h>
#include <errno.h>
#include <stdint.h>

#include <list>

struct uvc_buffer {
    void* buffer;
    size_t size;
    size_t total_size;
    int width;
    int height;
    int video_id;
    pthread_mutex_t lock;
};

struct video_uvc {
    pthread_t id;
    bool run;
    int video_id;
    struct uvc_buffer b;
};

void (*uvc_video_restart_isp_cb)() = NULL;
void (*uvc_video_restart_cif_cb)() = NULL;

static std::list<struct uvc_video*> lst_v;
static pthread_mutex_t mtx_v = PTHREAD_MUTEX_INITIALIZER;

static void* uvc_gadget_pthread(void* arg)
{
    struct uvc_function_config *fc = (struct uvc_function_config *)arg;

    prctl(PR_SET_NAME, "uvc_gadget_pthread", 0, 0, 0);

    uvc_gadget_main(fc);
    uvc_set_user_run_state(true, fc->video);
    pthread_exit(NULL);
}

static int uvc_gadget_pthread_create(struct uvc_function_config *fc)
{
    pthread_t *pid = NULL;

    uvc_memset_uvc_user(fc->video);
    if ((pid = uvc_video_get_uvc_pid(fc->video))) {
        if (pthread_create(pid, NULL, uvc_gadget_pthread, fc)) {
            printf("create uvc_gadget_pthread fail!\n");
            return -1;
        }
    }
    return 0;
}

static int _uvc_video_id_check(int id)
{
    int ret = 0;

    if (!lst_v.empty()) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                ret = -1;
                break;
            }
        }
    }

    return ret;
}

int uvc_video_id_check(int id)
{
    int ret = 0;

    pthread_mutex_lock(&mtx_v);
    ret = _uvc_video_id_check(id);
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

int uvc_video_id_add(struct uvc_function_config *fc)
{
    int ret = 0;
    int id = fc->video;

    printf("add uvc video id: %d\n", id);

    pthread_mutex_lock(&mtx_v);
    if (!_uvc_video_id_check(id)) {
        struct uvc_video* v = (struct uvc_video*)calloc(1, sizeof(struct uvc_video));
        if (v) {
            v->id = id;
            v->seq = lst_v.size();
            lst_v.push_back(v);
            pthread_mutex_unlock(&mtx_v);
            uvc_gadget_pthread_create(fc);
            pthread_mutex_lock(&mtx_v);
            pthread_mutex_init(&v->buffer_mutex, NULL);
            pthread_mutex_init(&v->user_mutex, NULL);
            pthread_mutex_init(&v->cond_mutex, NULL);
            pthread_cond_init(&v->cond, NULL);
            ret = 0;
        } else {
            printf("%s: %d: memory alloc fail.\n", __func__, __LINE__);
            ret = -1;
        }
    } else {
        printf("%s: %d: %d already exist.\n", __func__, __LINE__, id);
        ret = -1;
    }
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

void uvc_video_id_remove(int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                pthread_mutex_destroy(&l->buffer_mutex);
                pthread_mutex_destroy(&l->user_mutex);
                pthread_mutex_destroy(&l->cond_mutex);
                pthread_cond_destroy(&l->cond);
                free(l);
                lst_v.erase(i);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static void _uvc_video_id_cond_signal(int id)
{
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                pthread_mutex_lock(&l->cond_mutex);
                pthread_mutex_lock(&l->uvc->b.lock);
                l->uvc->b.buffer = NULL;
                pthread_mutex_unlock(&l->uvc->b.lock);
                pthread_cond_signal(&l->cond);
                pthread_mutex_unlock(&l->cond_mutex);
                break;
            }
        }
    }
}
void uvc_video_id_cond_signal(int id)
{
    pthread_mutex_lock(&mtx_v);
    _uvc_video_id_cond_signal(id);
    pthread_mutex_unlock(&mtx_v);
}

int uvc_video_id_get(unsigned int seq)
{
    int ret = -1;

    pthread_mutex_lock(&mtx_v);
    if (!lst_v.empty()) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (l->seq == seq) {
                ret = l->id;
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

static void uvc_gadget_pthread_exit(int id);

int uvc_video_id_exit(int id)
{
    if (uvc_video_id_check(id)) {
        uvc_gadget_pthread_exit(id);
        uvc_video_id_remove(id);
        return 0;
    }

    return -1;
}

static int _uvc_video_id_exit_all()
{
    int ret = -1;

    pthread_mutex_lock(&mtx_v);
    if (!lst_v.empty()) {
        struct uvc_video* l = lst_v.front();
        pthread_mutex_unlock(&mtx_v);
        uvc_video_id_exit(l->id);
        pthread_mutex_lock(&mtx_v);
        ret = 0;
    }
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

void uvc_video_id_exit_all()
{
    while (!_uvc_video_id_exit_all())
        continue;
}

static void _uvc_video_set_uvc_process(struct uvc_video* v, bool state)
{
    v->uvc_process = state;
}

void uvc_video_set_uvc_process(int id, bool state)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_video_set_uvc_process(l, state);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_video_get_uvc_process(struct uvc_video* v)
{
    return v->uvc_process;
}

bool uvc_video_get_uvc_process(int id)
{
    bool state = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                state = _uvc_video_get_uvc_process(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return state;
}

pthread_t* uvc_video_get_uvc_pid(int id)
{
    pthread_t *tid = NULL;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                tid = &l->uvc_pid;
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return tid;
}

void uvc_video_join_uvc_pid(int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                if (l->uvc_pid) {
                    pthread_mutex_unlock(&mtx_v);
                    pthread_join(l->uvc_pid, NULL);
                    l->uvc_pid = 0;
                    pthread_mutex_lock(&mtx_v);
                }
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static void uvc_gadget_pthread_exit(int id)
{
    while (!uvc_get_user_run_state(id))
        pthread_yield();
    uvc_set_user_run_state(false, id);
    uvc_video_join_uvc_pid(id);
}

static void _uvc_get_user_resolution(struct uvc_video *v, int* width, int* height);

static int _uvc_buffer_init(struct uvc_video *v)
{
    int i = 0;
    int ret = 0;
    struct uvc_buffer* buffer = NULL;
    int width, height;

    _uvc_get_user_resolution(v, &width, &height);

    pthread_mutex_lock(&v->buffer_mutex);

    v->uvc = (struct video_uvc*)calloc(1, sizeof(struct video_uvc));
    if (!v->uvc) {
        ret = -1;
        goto exit;
    }
    v->uvc->id = 0;
    v->uvc->video_id = v->id;
    v->uvc->run = 1;
    pthread_mutex_init(&v->uvc->b.lock, NULL);
    _uvc_video_set_uvc_process(v, true);

exit:
    pthread_mutex_unlock(&v->buffer_mutex);
    return ret;
}

int uvc_buffer_init(int id)
{
    int ret = -1;
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                ret = _uvc_buffer_init(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

static void _uvc_buffer_deinit(struct uvc_video *v)
{
    pthread_mutex_lock(&v->buffer_mutex);
    if (v->uvc) {
        v->uvc->run = 0;
        _uvc_video_set_uvc_process(v, false);
        pthread_mutex_destroy(&v->uvc->b.lock);
        free(v->uvc);
        v->uvc = NULL;
    }
    pthread_mutex_unlock(&v->buffer_mutex);
}

void uvc_buffer_deinit(int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_buffer_deinit(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_buffer_write(struct uvc_video *v,
                              void* extra_data,
                              size_t extra_size,
                              void* data,
                              size_t size,
                              unsigned int fcc,
                              size_t frame_size_off)
{
    bool ret = false;

    if (v->uvc && data) {
        pthread_mutex_lock(&v->uvc->b.lock);
        if (v->uvc->b.buffer) {
            ret = true;
            if (v->uvc->b.total_size >= extra_size + size) {
                switch (fcc) {
                case V4L2_PIX_FMT_YUYV:
#ifdef YUYV_AS_RAW
                    memcpy(v->uvc->b.buffer, data, size);
#else
                    NV12_to_YUYV(v->uvc->b.width, v->uvc->b.height, data, v->uvc->b.buffer);
#endif
                    break;
                case V4L2_PIX_FMT_MJPEG:
                    if (extra_data && 2 + extra_size < 65535 && v->uvc->b.total_size >= (4 + extra_size + size)) {
                        size_t index = 4;// FF D8 FF E0
                        size_t len = *((unsigned char*)data + index);
                        len = len * 256 + *((unsigned char*)data + index + 1);
                        index = index + len;
                        memcpy(v->uvc->b.buffer, data, index);
                        memset((char*)v->uvc->b.buffer + index, 0xFF, 1);
                        memset((char*)v->uvc->b.buffer + index + 1, 0xE2, 1);
                        memset((char*)v->uvc->b.buffer + index + 2, (2 + extra_size) / 256, 1);
                        memset((char*)v->uvc->b.buffer + index + 3, (2 + extra_size) % 256, 1);
                        memcpy((char*)v->uvc->b.buffer + index + 4, extra_data, extra_size);
                        extra_size += 4;
                        memcpy((char*)v->uvc->b.buffer + index + extra_size,
                                (char*)data + index, size - index);
                        uint32_t frame_size = extra_size + size;
                        memcpy((char*)v->uvc->b.buffer + index + 4 + frame_size_off, &frame_size, 4);
                    } else {
                        memcpy(v->uvc->b.buffer, data, size);
                    }
                    break;
                case V4L2_PIX_FMT_H264:
                    if (extra_data && extra_size > 0)
                        memcpy(v->uvc->b.buffer, extra_data, extra_size);
                    if (extra_size >= 0)
                        memcpy((char*)v->uvc->b.buffer + extra_size, data, size);
                    break;
                }
                v->uvc->b.size = extra_size + size;
            }
        }
        pthread_mutex_unlock(&v->uvc->b.lock);
        if (v->uvc->b.size)
            _uvc_video_id_cond_signal(v->id);
    }

    return ret;
}

bool uvc_buffer_write(void* extra_data,
                      size_t extra_size,
                      void* data,
                      size_t size,
                      unsigned int fcc,
                      int id,
                      size_t frame_size_off)
{
    bool ret = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                ret = _uvc_buffer_write(l, extra_data, extra_size, data, size, fcc, frame_size_off);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return ret;
}

static void _uvc_set_user_resolution(struct uvc_video *v, int width, int height)
{
    pthread_mutex_lock(&v->user_mutex);
    if (v->uvc_user.fcc == V4L2_PIX_FMT_YUYV)
        v->uvc_user.width = width;
    else
        v->uvc_user.width = width - width % 16;
    v->uvc_user.height = height;
    printf("uvc_user.width = %u, uvc_user.height = %u\n", v->uvc_user.width,
           v->uvc_user.height);
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_set_user_resolution(int width, int height, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_resolution(l, width, height);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static void _uvc_get_user_resolution(struct uvc_video *v, int* width, int* height)
{
    pthread_mutex_lock(&v->user_mutex);
    *width = v->uvc_user.width;
    *height = v->uvc_user.height;
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_get_user_resolution(int* width, int* height, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_get_user_resolution(l , width, height);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_get_user_run_state(struct uvc_video *v)
{
    bool ret;

    pthread_mutex_lock(&v->user_mutex);
    ret = v->uvc_user.run;
    pthread_mutex_unlock(&v->user_mutex);

    return ret;
}

bool uvc_get_user_run_state(int id)
{
    bool state = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                state = _uvc_get_user_run_state(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return state;
}

static void _uvc_set_user_run_state(struct uvc_video *v, bool state)
{
    pthread_mutex_lock(&v->user_mutex);
    v->uvc_user.run = state;
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_set_user_run_state(bool state, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_run_state(l, state);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static int _uvc_get_user_mirror_state(struct uvc_video *v)
{
    int ret;

    pthread_mutex_lock(&v->user_mutex);
    ret = v->uvc_user.mirror;
    pthread_mutex_unlock(&v->user_mutex);

    return ret;
}

int uvc_get_user_mirror_state(int id)
{
    int state = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                state = _uvc_get_user_mirror_state(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return state;
}

static void _uvc_set_user_mirror_state(struct uvc_video *v, int state)
{
    pthread_mutex_lock(&v->user_mutex);
    v->uvc_user.mirror = state;
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_set_user_mirror_state(int state, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_mirror_state(l, state);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static int _uvc_get_user_image_effect(struct uvc_video *v)
{
    bool ret;

    pthread_mutex_lock(&v->user_mutex);
    ret = v->uvc_user.image_effect;
    pthread_mutex_unlock(&v->user_mutex);

    return ret;
}

int uvc_get_user_image_effect(int id)
{
    int effect = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                effect = _uvc_get_user_image_effect(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return effect;
}

static void _uvc_set_user_image_effect(struct uvc_video *v, int effect)
{
    pthread_mutex_lock(&v->user_mutex);
    v->uvc_user.image_effect = effect;
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_set_user_image_effect(int effect, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_image_effect(l, effect);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_get_user_dcrop_state(struct uvc_video *v)
{
    bool ret;

    pthread_mutex_lock(&v->user_mutex);
    ret = v->uvc_user.dcrop;
    pthread_mutex_unlock(&v->user_mutex);

    return ret;
}

bool uvc_get_user_dcrop_state(int id)
{
    bool state = false;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                state = _uvc_get_user_dcrop_state(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return state;
}

static void _uvc_set_user_dcrop_state(struct uvc_video *v, bool state)
{
    pthread_mutex_lock(&v->user_mutex);
    v->uvc_user.dcrop = state;
    pthread_mutex_unlock(&v->user_mutex);
}

void uvc_set_user_dcrop_state(bool state, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_dcrop_state(l, state);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static void _uvc_set_user_fcc(struct uvc_video *v, unsigned int fcc)
{
    v->uvc_user.fcc = fcc;
}

void uvc_set_user_fcc(unsigned int fcc, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_user_fcc(l, fcc);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static unsigned int _uvc_get_user_fcc(struct uvc_video *v)
{
    return v->uvc_user.fcc;
}

unsigned int uvc_get_user_fcc(int id)
{
    unsigned int fcc = 0;

    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                fcc = _uvc_get_user_fcc(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);

    return fcc;
}

static void _uvc_memset_uvc_user(struct uvc_video *v)
{
    memset(&v->uvc_user, 0, sizeof(struct uvc_user));
    v->uvc_user.dcrop = true;
}

void uvc_memset_uvc_user(int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_memset_uvc_user(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static void _uvc_set_video_type(struct uvc_video *v, bool is_isp)
{
    v->is_isp = is_isp;
}

void uvc_set_video_type(bool is_isp, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_video_type(l, is_isp);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_buffer_check(struct uvc_video* v, struct uvc_buffer* buffer)
{
    int width = 0, height = 0;

    _uvc_get_user_resolution(v, &width, &height);
    if (buffer->width == width && buffer->height == height)
        return true;
    else
        return false;
}

static bool _uvc_get_uvc_stream(struct uvc_video *v)
{
    return v->stream_start;
}

bool uvc_get_uvc_stream(int id)
{
    bool ret = false;
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                ret = _uvc_get_uvc_stream(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
    return ret;
}

static void _uvc_set_uvc_stream(struct uvc_video *v, bool start)
{
    v->stream_start = start;
    if (!start)
        v->stream_timeout = 0;
}

void uvc_set_uvc_stream(int id, bool start)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                _uvc_set_uvc_stream(l, start);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}

static bool _uvc_stream_timeout(struct uvc_video *v)
{
    v->stream_timeout++;
    if (!v->stream_start) {
        v->stream_timeout = 0;
        return false;
    } else {
        printf("%d: uvc stream timeout!\n", v->id);
        if (v->stream_timeout >= 120)
            return true;
        else
            return false;
    }
}

bool uvc_stream_timeout(int id)
{
    bool ret = false;
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                ret = _uvc_stream_timeout(l);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
    return ret;
}

static void _uvc_user_fill_buffer(struct uvc_video *v, struct uvc_device *dev, struct v4l2_buffer *buf)
{
    struct timeval now;
    struct timespec timeout;

    v->uvc->b.width = dev->width;
    v->uvc->b.height = dev->height;
    v->uvc->b.total_size = buf->length;
    v->uvc->b.video_id = dev->video_id;
    v->uvc->b.size = 0;
    pthread_mutex_lock(&v->uvc->b.lock);
    v->uvc->b.buffer = dev->mem[buf->index].start;
    pthread_mutex_unlock(&v->uvc->b.lock);

    v->stream_timeout = 0;
    pthread_mutex_lock(&v->cond_mutex);
    while (dev->vp_flag) {
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec + 1;
        timeout.tv_nsec = now.tv_usec * 1000;
        if (pthread_cond_timedwait(&v->cond, &v->cond_mutex, &timeout) != ETIMEDOUT) {
            break;
        } else {
            if (!_uvc_stream_timeout(v))
                continue;
            pthread_mutex_unlock(&v->cond_mutex);
            if (v->is_isp) {
                if (uvc_video_restart_isp_cb)
                    uvc_video_restart_isp_cb();
            } else {
                if (uvc_video_restart_cif_cb)
                    uvc_video_restart_cif_cb();
            }
            pthread_mutex_lock(&v->cond_mutex);
        }
    }
    pthread_mutex_unlock(&v->cond_mutex);

    buf->bytesused = v->uvc->b.size;
}

void uvc_user_fill_buffer(struct uvc_device *dev, struct v4l2_buffer *buf, int id)
{
    pthread_mutex_lock(&mtx_v);
    if (_uvc_video_id_check(id)) {
        for (std::list<struct uvc_video*>::iterator i = lst_v.begin(); i != lst_v.end(); ++i) {
            struct uvc_video* l = *i;
            if (id == l->id) {
                pthread_mutex_unlock(&mtx_v);
                _uvc_user_fill_buffer(l, dev, buf);
                pthread_mutex_lock(&mtx_v);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mtx_v);
}
