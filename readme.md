### 操作流程
1. 配置uvc功能：运行uvc_MJPEG.sh
2. 运行: uvc_app 640 480
3. 打开AMCAP即可预览，uvc_app输出四条纯色

### 接口说明
1. mpi_enc_set_format：设置MJPG编码输入源格式，没设置默认为NV12
2. check_video_id：检查是否有UVC设备节点
3. add_uvc_video：初始化UVC设备
4. uvc_read_camera_buffer：读取buffer后用于编码传输
5. uvc_video_id_exit_all：反初始化UVC设备
6. register_callback_for_uvc：注册获取camera数据后的callback