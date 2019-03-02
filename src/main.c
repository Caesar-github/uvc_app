#include <stdio.h>
#include "uvc-gadget.h"

int main(void)
{
    check_video_id();
    add_uvc_video();
    while (1) {
        uvc_control();
        usleep(100000);
    }
    uvc_video_id_exit_all();
    return 0;
}
