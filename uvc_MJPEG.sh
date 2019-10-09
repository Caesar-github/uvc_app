#!/bin/sh
#if [ $# -ne 2 ];then
#    echo "Usage: uvc_MJPEG.sh width height"
#    echo "e.g. uvc_MJPEG.sh 640 480"
#    exit 0
#fi
/etc/init.d/S10udev stop

umount /sys/kernel/config
mount -t configfs none /sys/kernel/config
mkdir -p /sys/kernel/config/usb_gadget/rockchip
mkdir -p /sys/kernel/config/usb_gadget/rockchip/strings/0x409
mkdir -p /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409

echo 0x2207 > /sys/kernel/config/usb_gadget/rockchip/idVendor
echo 0x0310 > /sys/kernel/config/usb_gadget/rockchip/bcdDevice
echo 0x0200 > /sys/kernel/config/usb_gadget/rockchip/bcdUSB

echo "2019" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/serialnumber
echo "rockchip" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/manufacturer
echo "UVC" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6
#cat /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming_maxpacket
echo 1 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming_bulk

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/class/fs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/class/ss/h

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m
w=640
h=480
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p
echo $w > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/wWidth
echo $h > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/wHeight
echo 666666 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/dwDefaultFrameInterval
echo $((w*h*80)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/dwMinBitRate
echo $((w*h*160)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/dwMaxBitRate
echo $((w*h*2)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/dwMaxVideoFrameBufferSize
cat <<EOF > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/480p/dwFrameInterval
666666
1000000
2000000
EOF

w=1280
h=720
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p
echo $w > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/wWidth
echo $h > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/wHeight
echo 666666 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/dwDefaultFrameInterval
echo $((w*h*80)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/dwMinBitRate
echo $((w*h*160)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/dwMaxBitRate
echo $((w*h*2)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/dwMaxVideoFrameBufferSize
cat <<EOF > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/720p/dwFrameInterval
666666
1000000
2000000
EOF

w=1920
h=1080
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p
echo $w > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/wWidth
echo $h > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/wHeight
echo 666666 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/dwDefaultFrameInterval
echo $((w*h*80)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/dwMinBitRate
echo $((w*h*160)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/dwMaxBitRate
echo $((w*h*2)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/dwMaxVideoFrameBufferSize
cat <<EOF > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1080p/dwFrameInterval
666666
1000000
2000000
EOF

w=2560
h=1440
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p
echo $w > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/wWidth
echo $h > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/wHeight
echo 666666 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwDefaultFrameInterval
echo $((w*h*80)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMinBitRate
echo $((w*h*160)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMaxBitRate
echo $((w*h*2)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMaxVideoFrameBufferSize
cat <<EOF > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwFrameInterval
666666
1000000
2000000
EOF

w=2592
h=1944
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p
echo $w > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/wWidth
echo $h > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/wHeight
echo 666666 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwDefaultFrameInterval
echo $((w*h*80)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMinBitRate
echo $((w*h*160)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMaxBitRate
echo $((w*h*2)) > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwMaxVideoFrameBufferSize
cat <<EOF > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m/1440p/dwFrameInterval
666666
1000000
2000000
EOF

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h
#ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/uncompressed/u /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h/u
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h/m
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/fs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/hs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/ss/h

echo 0x1 > /sys/kernel/config/usb_gadget/rockchip/os_desc/b_vendor_code
echo "MSFT100" > /sys/kernel/config/usb_gadget/rockchip/os_desc/qw_sign
echo 500 > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/MaxPower
ln -s /sys/kernel/config/usb_gadget/rockchip/configs/b.1 /sys/kernel/config/usb_gadget/rockchip/os_desc/b.1

echo 0x0005 > /sys/kernel/config/usb_gadget/rockchip/idProduct
echo "uvc" > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration
rm -f /sys/kernel/config/usb_gadget/rockchip/configs/b.1/ffs.adb
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6 /sys/kernel/config/usb_gadget/rockchip/configs/b.1/f1

UDC=`ls /sys/class/udc/| awk '{print $1}'`
echo $UDC > /sys/kernel/config/usb_gadget/rockchip/UDC
