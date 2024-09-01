#!/bin/sh
export LD_LIBRARY_PATH=/mnt/app/root/lib-target:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib:/mnt/app/pkg/lib:/mnt/app/pkg/usr/lib:/mnt/app/navigation:/mnt/app/navigation/lib:$LD_LIBRARY_PATH
export PATH=/bin:/proc/boot:/sbin:/usr/bin:/usr/sbin:/mnt/app/armle/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/bin:/mnt/app/armle/usr/sbin:/fs/sda0/Toolbox/scripts:/mnt/app/media/gracenote/bin:/mnt/app/pkg/bin:/mnt/app/pkg/sbin:/mnt/app/pkg/usr/bin:/mnt/app/pkg/usr/sbin:/mnt/app/root:/eso/bin/apps/
export IPL_CONFIG_DIR=/etc/eso/production

if [ -f /fs/sda0/opengl-render-qnx ]; then
    chmod 0777 /fs/sda0/opengl-render-qnx
    /fs/sda0/opengl-render-qnx &
else
    echo "File /fs/sda0/opengl-render-qnx does not exist."
fi

/navigation/navStartup $*
