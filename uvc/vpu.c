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

#include "vpu.h"

int vpu_nv12_encode_jpeg_init_ext(struct vpu_encode* encode,
                                  int width,
                                  int height,
                                  int quant) {
  MPP_RET ret = MPP_OK;
  MppEncPrepCfg prep_cfg;
  MppEncCodecCfg codec_cfg;
  MppCodingType type = MPP_VIDEO_CodingMJPEG;
  MppFrameFormat fmt = MPP_FMT_YUV420SP;
  memset(encode, 0, sizeof(*encode));
  encode->jpeg_enc_out.fd = -1;
  encode->jpeg_enc_out.handle_fd = -1;
  encode->width = width;
  encode->height = height;
  encode->enc_out_data = NULL;
  encode->enc_out_length = 0;

  ret = mpp_create(&encode->mpp_ctx, &encode->mpi);
  if (MPP_OK != ret) {
    printf("mpp_create failed\n");
    return -1;
  }

  MppCtx mpp_ctx = encode->mpp_ctx;
  ret = mpp_init(mpp_ctx, MPP_CTX_ENC, type);
  if (MPP_OK != ret) {
    printf("mpp_init failed\n");
    return -1;
  }

  memset(&prep_cfg, 0, sizeof(prep_cfg));
  prep_cfg.change =
      MPP_ENC_PREP_CFG_CHANGE_INPUT | MPP_ENC_PREP_CFG_CHANGE_FORMAT;
  prep_cfg.width = width;
  prep_cfg.height = height;
  prep_cfg.hor_stride = width;
  prep_cfg.ver_stride = height;
  prep_cfg.format = fmt;

  MppApi* mpi = encode->mpi;
  ret = mpi->control(mpp_ctx, MPP_ENC_SET_PREP_CFG, &prep_cfg);
  if (MPP_OK != ret) {
    printf("mpi control enc set cfg failed\n");
    return -1;
  }

  memset(&codec_cfg, 0, sizeof(codec_cfg));
  codec_cfg.coding = type;
  codec_cfg.jpeg.change = MPP_ENC_JPEG_CFG_CHANGE_QP;
  codec_cfg.jpeg.quant = quant; /* range 0 - 10, worst -> best */

  ret = mpi->control(mpp_ctx, MPP_ENC_SET_CODEC_CFG, &codec_cfg);
  if (ret) {
    printf("mpi control enc set codec cfg failed ret %d\n", ret);
    return -1;
  }

  if (video_drm_alloc(&encode->jpeg_enc_out, width, height, 2, 1)) {
    printf("%s:video ion alloc fail!\n", __func__);
    return -1;
  }

  return 0;
}

int vpu_nv12_encode_jpeg_init(struct vpu_encode* encode,
                              int width,
                              int height) {
  return vpu_nv12_encode_jpeg_init_ext(encode, width, height, 10);
}

int vpu_nv12_encode_jpeg_doing(struct vpu_encode* encode,
                               void* srcbuf,
                               int src_fd,
                               size_t src_size) {
if (encode->packet)
     mpp_packet_deinit(&encode->packet);

  MPP_RET ret = MPP_OK;
  RK_U32 width = encode->width;
  RK_U32 height = encode->height;
  MppCtx mpp_ctx = encode->mpp_ctx;
  MppApi* mpi = encode->mpi;
  MppTask task = NULL;
  MppFrame frame = NULL;
  MppBuffer pic_buf = NULL;
  MppBuffer str_buf = NULL;
  MppBufferGroup memGroup = NULL;

  encode->enc_out_data = NULL;
  encode->enc_out_length = 0;

  ret = mpp_frame_init(&frame);
  if (MPP_OK != ret) {
    printf("mpp_frame_init failed\n");
    goto ENCODE_OUT;
  }

  mpp_frame_set_width(frame, width);
  mpp_frame_set_height(frame, height);
  mpp_frame_set_hor_stride(frame, width);
  mpp_frame_set_ver_stride(frame, height);
  mpp_frame_set_eos(frame, 1);

  if (src_fd > 0) {
    MppBufferInfo inputCommit;

    memset(&inputCommit, 0, sizeof(inputCommit));
    inputCommit.type = MPP_BUFFER_TYPE_DRM;
    inputCommit.size = src_size;
    inputCommit.fd = src_fd;

    ret = mpp_buffer_import(&pic_buf, &inputCommit);
    if (ret) {
      printf("import input picture buffer failed\n");
      goto ENCODE_OUT;
    }
  } else {
    if (NULL == srcbuf) {
      ret = MPP_ERR_NULL_PTR;
      goto ENCODE_OUT;
    }

    ret = mpp_buffer_get(memGroup, &pic_buf, src_size);
    if (ret) {
      printf("allocate input picture buffer failed\n");
      goto ENCODE_OUT;
    }
    memcpy((RK_U8*)mpp_buffer_get_ptr(pic_buf), srcbuf, src_size);
  }

  if (encode->jpeg_enc_out.handle_fd > 0) {
    MppBufferInfo outputCommit;

    memset(&outputCommit, 0, sizeof(outputCommit));
    outputCommit.type = MPP_BUFFER_TYPE_DRM;
    outputCommit.fd = encode->jpeg_enc_out.handle_fd;
    outputCommit.size = encode->jpeg_enc_out.size;
    outputCommit.ptr = (void*)encode->jpeg_enc_out.buffer;

    ret = mpp_buffer_import(&str_buf, &outputCommit);
    if (ret) {
      printf("import output stream buffer failed\n");
      goto ENCODE_OUT;
    }
  } else {
    ret = mpp_buffer_get(memGroup, &str_buf, width * height);
    if (ret) {
      printf("allocate output stream buffer failed\n");
      goto ENCODE_OUT;
    }
  }

  mpp_frame_set_buffer(frame, pic_buf);

//  if (encode->packet)
//    mpp_packet_deinit(&encode->packet);

  mpp_packet_init_with_buffer(&encode->packet, str_buf);

  ret = mpi->poll(mpp_ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
  if (ret) {
    printf("mpp task input poll failed ret %d\n", ret);
    goto ENCODE_OUT;
  }
  ret = mpi->dequeue(mpp_ctx, MPP_PORT_INPUT, &task);
  if (ret || NULL == task) {
    printf("mpp task input dequeue failed ret %d task %p\n", ret, task);
    goto ENCODE_OUT;
  }
  mpp_task_meta_set_frame(task, KEY_INPUT_FRAME, frame);
  mpp_task_meta_set_packet(task, KEY_OUTPUT_PACKET, encode->packet);

  ret = mpi->enqueue(mpp_ctx, MPP_PORT_INPUT, task);
  if (ret) {
    printf("mpp task input enqueue failed\n");
    goto ENCODE_OUT;
  }

  ret = mpi->poll(mpp_ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
  if (ret) {
    printf("mpp task output poll failed ret %d\n", ret);
    goto ENCODE_OUT;
  }

  ret = mpi->dequeue(mpp_ctx, MPP_PORT_OUTPUT, &task);
  if (ret || NULL == task) {
    printf("mpp task output dequeue failed ret %d task %p\n", ret, task);
    goto ENCODE_OUT;
  }

  if (task) {
    MppFrame packet_out = NULL;

    mpp_task_meta_get_packet(task, KEY_OUTPUT_PACKET, &packet_out);

    assert(packet_out == encode->packet);

    ret = mpi->enqueue(mpp_ctx, MPP_PORT_OUTPUT, task);
    if (ret) {
      printf("mpp task output enqueue failed\n");
      goto ENCODE_OUT;
    }
    task = NULL;
  }

  if ((encode->packet != NULL)) {
    RK_U8* data = (RK_U8*)mpp_packet_get_data(encode->packet);
    size_t length = mpp_packet_get_length(encode->packet);

    encode->enc_out_data = data;
    encode->enc_out_length = length;
  }

  ret = mpi->reset(mpp_ctx);
  if (MPP_OK != ret) {
    printf("mpi->reset failed\n");
    goto ENCODE_OUT;
  }

ENCODE_OUT:
  if (pic_buf) {
    mpp_buffer_put(pic_buf);
    pic_buf = NULL;
  }

  if (str_buf) {
    mpp_buffer_put(str_buf);
    str_buf = NULL;
  }

  if (frame)
    mpp_frame_deinit(&frame);

  if (encode->enc_out_length > src_size) {
    printf("MJPEG encode out length overflow!\n");
    ret = -1;
  }
  return ret;
}

void vpu_nv12_encode_jpeg_done(struct vpu_encode* encode) {
  if (encode) {
    if (encode->packet)
      mpp_packet_deinit(&encode->packet);

    if (encode->mpp_ctx) {
      mpp_destroy(encode->mpp_ctx);
      encode->mpp_ctx = NULL;
    }

    video_drm_free(&encode->jpeg_enc_out);
  }
}
