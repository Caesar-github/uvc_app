/*
 * V4L2 video capture example
 * AUTHOT : Leo Wen
 * DATA : 2019-02-15
 */
#ifndef __RKCAM_CONTROL_H__
#define __RKCAM_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

int add_rkcam(int id, int width, int height);
void remove_rkcam();
void read_rkcam();

#ifdef __cplusplus
}
#endif

#endif
