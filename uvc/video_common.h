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

#ifndef VIDEO_COMMON_H
#define VIDEO_COMMON_H

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#ifdef DEBUG
#include <assert.h>
#else
#undef assert
#define assert(A)
#endif

#ifdef DEBUG
#define PRINTF_FUNC printf("%s, thread: 0x%x\n", __func__, (int)pthread_self())
#define PRINTF_FUNC_OUT \
  printf("---- OUT OF %s, thread: 0x%x ----\n", __func__, (int)pthread_self())
#define PRINTF_FUNC_LINE printf("~~ %s: %d\n", __func__, __LINE__)
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF_FUNC
#define PRINTF_FUNC_OUT
#define PRINTF_FUNC_LINE
#define PRINTF(...)
#endif

#define PRINTF_NO_MEMORY                              \
  printf("no memory! %s : %d\n", __func__, __LINE__); \
  assert(0)

#define THREAD_STACK_SIZE (128 * 1024)

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

typedef struct {
    int width;
    int height;
    int video_bit_rate;
    int stream_frame_rate;
    int video_encoder_level;
    int video_gop_size;
    int video_profile;

    int audio_bit_rate;       // 64000
    int audio_sample_rate;    // 44100
    uint64_t channel_layout;  // MONO
    int nb_channels;
    int input_nb_samples;  // 1024, codec may rewrite nb_samples; aac 1024 is
    // better; for muxing

    int out_nb_samples;                  // for demuxing
} media_info;

typedef enum {
    VIDEO_PACKET,
    AUDIO_PACKET,
    SUBTITLE_PACKET,
    VIDEO_INFO_PACKET,
    PACKET_TYPE_COUNT
} PacketType;

class video_object
{
private:
    volatile int ref_cnt;

public:
    std::function<void()> release_cb;
    video_object() : ref_cnt(1) {}
    virtual ~video_object() {
        // printf("%s: %p\n", __func__, this);
        assert(ref_cnt == 0);
    }
    void ref();
    int unref();  // return 0 means object is deleted
    int refcount();
    void resetRefcount() {
        //__atomic_store_n(&ref_cnt, 0, __ATOMIC_SEQ_CST);
        ref_cnt = 0;
    }
};

class EncodedPacket : public video_object
{
public:
    PacketType type;
    bool is_phy_buf;
    union {
        struct timeval time_val;  // store the time for video_packet
        uint64_t audio_index;     // audio packet index
    };
    struct timeval lbr_time_val;
    EncodedPacket();
    ~EncodedPacket();
    int copy_av_packet();
};

#define UNUSED(x) (void)x

#define CREATE_FUNC(__TYPE__)        \
  static __TYPE__* create() {        \
    __TYPE__* pRet = new __TYPE__(); \
    if (pRet && !pRet->init()) {     \
      return pRet;                   \
    } else {                         \
      if (pRet)                      \
        delete pRet;                 \
      return NULL;                   \
    }                                \
  }

typedef void* (*Runnable)(void* arg);

#define UPALIGNTO(value, align) ((value + align - 1) & (~(align - 1)))

#define UPALIGNTO16(value) UPALIGNTO(value, 16)

inline int64_t difftime(struct timeval& start, struct timeval& end)
{
    return (end.tv_sec - start.tv_sec) * 1000000LL +
           (end.tv_usec - start.tv_usec);
}

int StartThread(pthread_t& tid, void * (*start_routine)(void*), void* arg);
void StopThread(pthread_t& tid);

#define PRSET_THREAD_NAME() prctl(PR_SET_NAME, __func__, 0, 0, 0)

#endif  // VIDEO_COMMON_H
