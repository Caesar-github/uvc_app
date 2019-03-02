/*
 * V4L2 video capture example
 * AUTHOT : Leo Wen
 * DATA : 2019-02-15
 */
#include "rkcam_capture.h"

static int init_mmap(int fd, struct buffer **buffers, int buf_type)
{
    struct v4l2_requestbuffers req;
    struct buffer *buf_tmp;
    CLEAR(req);

    req.count = 4;
    req.type = buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == ioctl(fd, VIDIOC_REQBUFS, &req)) {
        printf("%s does not support ""memory mapping\n", __func__);
        return -1;
    }

    if (req.count < 2) {
        printf( "Insufficient buffer memory on %s\n",
                __func__);
        return -1;
    }

    buf_tmp = (struct buffer*)calloc(req.count, sizeof(*buf_tmp));

    if (NULL == buf_tmp) {
        printf("Out of memory\n");
        return -1;
    }
    *buffers = buf_tmp;

    for (int n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        CLEAR(buf);
        CLEAR(planes);

        buf.type = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
            buf.m.planes = planes;
            buf.length = 1;
        }

        if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            printf( "buffer QUERYBUF err %s\n", __func__);
            return -1;
        }
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
            buf_tmp[n_buffers].length = buf.m.planes[0].length;
            buf_tmp[n_buffers].start =
                mmap(NULL /* start anywhere */,
                        buf.m.planes[0].length,
                        PROT_READ | PROT_WRITE /* required */,
                        MAP_SHARED /* recommended */,
                        fd, buf.m.planes[0].m.mem_offset);

        } else {
            buf_tmp[n_buffers].length = buf.length;
            buf_tmp[n_buffers].start =
                mmap(NULL /* start anywhere */,
                        buf.length,
                        PROT_READ | PROT_WRITE /* required */,
                        MAP_SHARED /* recommended */,
                        fd, buf.m.offset);
        }
        if (MAP_FAILED == buf_tmp[n_buffers].start){
            printf( "buffer MAP_FAILED %s\n", __func__);
            return -1;
        }
    }
    return 0;
}

static int init_device(int fd, int width, int height, struct buffer **buffers)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        printf("%s: VIDIOC_QUERYCAP is no V4L2 device\n", __func__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        printf("%s is not a video capture device, capabilities: %x\n",
                __func__, cap.capabilities);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        printf("%s does not support streaming i/o\n",
                __func__);
        return -1;
    }
    CLEAR(fmt);
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == ioctl(fd, VIDIOC_S_FMT, &fmt)) {
        printf("%s VIDIOC_S_FMT err\n",
                __func__);
        return -1;
    }

    return init_mmap(fd, buffers, fmt.type);
}

#define MAX_MEDIA_INDEX 64
static struct media_device* __rkisp_get_media_dev_by_vnode(const char* vnode)
{
    char sys_path[64];
    struct media_device *device = NULL;
    uint32_t nents, j, i = 0;
    FILE *fp;
    int ret;

    while (i < MAX_MEDIA_INDEX) {
        snprintf (sys_path, 64, "/dev/media%d", i++);
        fp = fopen (sys_path, "r");
        if (!fp)
            continue;
        fclose (fp);

        device = media_device_new (sys_path);

        /* Enumerate entities, pads and links. */
        media_device_enumerate (device);

        nents = media_get_entities_count (device);
        for (j = 0; j < nents; ++j) {
            struct media_entity *entity = media_get_entity (device, j);
            const char *devname = media_entity_get_devname (entity);
            if (NULL != devname) {
                if (!strcmp (devname, vnode)) {
                    goto out;
                }
            }
        }
        media_device_unref (device);
    }

out:
    return device;
}

static void deinit_device(struct buffer **buffers, void **rkisp_engine)
{
    unsigned int i;
    unsigned int n_buffers = 4;
    struct buffer *buf_tmp = *buffers;

    for (i = 0; i < n_buffers; ++i)
        if (-1 == munmap(buf_tmp[i].start, buf_tmp[i].length))
            printf("%s: munmap is err\n", __func__);
    free(buf_tmp);

    if (*rkisp_engine)
        rkisp_cl_deinit (*rkisp_engine);
}

static int get_type(int fd)
{
    enum v4l2_buf_type type;
    struct v4l2_capability cap;

    if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        printf("%s is no V4L2 device\n",  __func__);
        return -1;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; 

    return type;
}

static int start_capturing(int fd, char* dev_name, void **rkisp_engine)
{
    unsigned int i;
    char iq_file[255] = "/etc/cam_iq.xml";
    int fp = -1;
    struct RKisp_media_ctl rkisp;
    struct v4l2_capability capability;
    enum v4l2_buf_type buf_type;
    int n_buffers = 4;

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        printf ("Video device %s query capability not supported.", dev_name);
        return -1;
    }

    buf_type = get_type(fd);

    if (strstr((char*)(capability.driver), "rkisp")) {
        printf ("Using ISP media node\n");
        rkisp_cl_init (rkisp_engine, iq_file, NULL);

        struct rkisp_cl_prepare_params_s params={0};
        int nents;

        rkisp.controller =
            __rkisp_get_media_dev_by_vnode (dev_name);
        if (!rkisp.controller) {
            printf(
                    "Can't find controller, maybe use a wrong video-node or wrong permission to media node");
            return -1;		
        }
        rkisp.isp_subdev =
            media_get_entity_by_name (rkisp.controller, "rkisp1-isp-subdev",
                    strlen("rkisp1-isp-subdev"));
        rkisp.isp_params_dev =
            media_get_entity_by_name (rkisp.controller, "rkisp1-input-params",
                    strlen("rkisp1-input-params"));
        rkisp.isp_stats_dev =
            media_get_entity_by_name (rkisp.controller, "rkisp1-statistics",
                    strlen("rkisp1-statistics"));
        /* assume the last enity is sensor_subdev */
        nents = media_get_entities_count (rkisp.controller);
        rkisp.sensor_subdev = media_get_entity (rkisp.controller, nents - 1);

        params.isp_sd_node_path = media_entity_get_devname (rkisp.isp_subdev);
        params.isp_vd_params_path = media_entity_get_devname (rkisp.isp_params_dev);
        params.isp_vd_stats_path = media_entity_get_devname (rkisp.isp_stats_dev);
        params.sensor_sd_node_path = media_entity_get_devname (rkisp.sensor_subdev);
        rkisp_cl_prepare (*rkisp_engine, &params);

        media_device_unref (rkisp.controller);

        rkisp_cl_start (*rkisp_engine);

        if (*rkisp_engine == NULL) {
            printf ("%s: rkisp_init engine failed\n", __func__);
        } else {
            printf ("%s: rkisp_init engine succeed\n", __func__);
        }
    } else if (strstr((char*)(capability.driver), "cif")) {
        printf ("%s: Using CIF media node\n", __func__);
    } else if (strstr((char*)(capability.driver), "uvc")) {
        printf ("%s: Using UVC media node\n", __func__);
    } else {
        printf ("%s: Using unknow media node\n", __func__);
    }

    for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
            struct v4l2_plane planes[1];
            buf.m.planes = planes;
            buf.length = 1;
        }
        if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
            printf("%s: VIDIOC_QBUF err\n", __func__);
            return -1;
        }
    }

    if (-1 == ioctl(fd, VIDIOC_STREAMON, &buf_type)) {
        printf("%s: VIDIOC_STREAMON\n", __func__);
        return -1;
    }
    return 0;
}

static void stop_capturing(int fd, void **rkisp_engine)
{
    enum v4l2_buf_type type;
    struct v4l2_capability cap;

    if (*rkisp_engine)
        rkisp_cl_stop(*rkisp_engine);

    type = get_type(fd);

    if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &type))
        printf("%s: VIDIOC_STREAMOFF\n", __func__);
}

int rkcam_init(int node, int width, int height, struct buffer **buffers, void **rkisp_engine)
{
    char dev_name[255];
    int fd;

    sprintf(dev_name, "/dev/video%d", node);

    fd = open(dev_name, O_RDWR /* required */ /*| O_NONBLOCK*/, 0);
    if (-1 == fd) {
        printf(" %s :open '%s' err\n", __func__, dev_name);
        goto open_err;
    }

    if (-1 == init_device(fd, width, height, buffers)) {
        printf(" %s :open '%s' err\n", __func__, dev_name);
        goto init_err;
    }

    start_capturing(fd, dev_name, rkisp_engine);

    return fd;
init_err:
    close(fd);
open_err:
    return -1;	
}

int read_frame(int fd, struct buffer *buffers, void* desbuff)
{
    struct v4l2_buffer buf;
    int size;

    CLEAR(buf);
    buf.type = get_type(fd);
    buf.memory = V4L2_MEMORY_MMAP;

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf.type) {
        struct v4l2_plane planes[1];
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (-1 == ioctl(fd, VIDIOC_DQBUF, &buf)) {
        printf("%s：VIDIOC_DQBUF ERR\n", __func__);
        return -1;	
    }
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf.type) {
        size = buf.m.planes[0].bytesused;
        //printf("multi-planes bytesused 0x%p %d\n", buffers[buf.index].start, size);
        memcpy(desbuff, buffers[buf.index].start, size);
    } else {
        size = buf.length;
        printf("bytesused %d\n", size);
        memcpy(desbuff, buffers[buf.index].start, size);
    }

    if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)){
        printf("%s：VIDIOC_QBUF ERR\n", __func__);
        return -1;
    }

    return size;
}

int rkcam_deinit(int fd, struct buffer **buffers, void **rkisp_engine)
{
    stop_capturing(fd, rkisp_engine);
    deinit_device(buffers, rkisp_engine);
    close(fd);
    return 0;
}
