#!/bin/sh
set -x
module="ring_buffer"
device="ring_buffer"
insmod ./$module.ko $* || exit 1
rm -f /dev/${device}[0]
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
mknod /dev/${device}0 c $major 0
chmod a+rw /dev/${device}0