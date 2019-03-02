#include <stdio.h>
#include "uvc-gadget.h"
#include "rkcam_control.h"

int main(void)
{
    check_video_id();
    add_uvc_video();
    while (1) {
        uvc_control();
        read_rkcam();
    }
    uvc_video_id_exit_all();
    return 0;
}
