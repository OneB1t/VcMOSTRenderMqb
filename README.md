

# VNC2MIB2VC - opengl-render-qnx

Telegram group:
https://t.me/+MCIqkmX6bjY3NTE0

https://github.com/user-attachments/assets/8121a700-7f8b-454b-bcc3-6d35c4149662

https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/54d5f16b-2c2a-484a-a8b8-4e4e5c39675a


![map](https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/52123bb5-55de-4fb4-871c-195b8fcebdc3)
![map+persistence(![photo_2024-09-07_18-07-58](https://github.com/user-attachments/assets/7b1a06fd-9e64-4403-ac04-388283ec7675)
)]
PRE-REQUISITIES: MIB2 with SSH access, Virtual cockpit, VNC server on phone/notebook (tested with Xiaomi 12 + droidVNC-NG)



How it works?
![diagram](https://github.com/user-attachments/assets/d95dcf20-d4ef-49df-ba9e-bef4dc393d52)

How to compile:
https://github.com/OneB1t/VcMOSTRenderMqb/wiki/How-to-compile-(QNX-version)
https://github.com/OneB1t/VcMOSTRenderMqb/wiki/How-to-compile-(Windows-version)



mount all required parts as R/W see https://github.com/jilleb/mib2-toolbox/wiki/SSH-Login for more info also enable SSH to make installation easier

```
mount -uw /net/mmx/mnt/app
echo "/bin/mount -uw /net/mmx/fs/sda0 2>/dev/null" >> /net/mmx/mnt/app/root/.profile
echo "/bin/mount -uw /net/mmx/fs/sda1 2>/dev/null" >> /net/mmx/mnt/app/root/.profile
echo "/bin/mount -uw /mnt/app" >> /net/mmx/mnt/app/root/.profile
echo "/bin/mount -uw /mnt/system" >> /net/mmx/mnt/app/root/.profile
mount -ur /net/mmx/mnt/app
```


This software is able to show any VNC stream on virtual cockpit. Final compile file is stored inside opengl-render-qnx/arm/o-le-v7/opengl-render-qnx 
How to make it run:
1) upload compiled file (opengl-render-qnx/arm/o-le-v7/opengl-render-qnx - https://github.com/OneB1t/VcMOSTRenderMqb/releases) to MIB2 unit via SCP (10.173.189.1) or use SD-card (/fs/sda0 on unit itself)
2) MIB2 SSH: chmod +x /fs/sda0/opengl-render-qnx
3) PHONE: connect mobile phone to MIB2 wifi (WARNING it is expected that phone will get 10.173.189.62 this IP is hardcoded for now)
4) PHONE: run droidVNC-NG (https://github.com/bk138/droidVNC-NG) and set following parameters:
   - port 5900
   - no password
   - scaling (30-50%) this depends on your phone resolution -> lower value means faster streaming
5) MIB2 SSH: run
 ```
         export IPL_CONFIG_DIR=/etc/eso/production
         ./fs/sda0/opengl-render-qnx
 ```
 in case that your phone received some random non-default IP other than 10.173.189.62 address you can specify the address as argument for opengl-render-qnx binary
```
          ./fs/sda0/opengl-render-qnx 10.10.10.2
 ```
7) VC: switch to navigation map and it should now show phone image

When you want to get rid if this just restart your MIB2 device and everything is back to stock.

For autostartup (this is for advanced users only now please be really careful this can brick unit boot and UART is needed to fix it):
1. MIB2 SSH: mount root filesystem as R/W (if you do not know how to do this please stop here)
2. MIB2 SSH: copy opengl-render-qnx into /navigation/opengl-render-qnx (mv /fs/sda0/opengl-render-qnx /navigation/opengl-render-qnx)
3. MIB2 SSH: and open /etc/boot/startup.sh -> find normal_startup() method inside the file and add following lines to the end of this method
 ```  
    if [ -f /navigation/opengl-render-qnx ]; then
        chmod 0777 /navigation/opengl-render-qnx
        /navigation/opengl-render-qnx &
    else
        echo "File /navigation/opengl-render-qnx does not exist."
    fi
```

In case that custom IP is required please modify like this where 10.10.10.10 is IP of phone/vnc server device:
 ```  
    if [ -f /navigation/opengl-render-qnx ]; then
        chmod 0777 /navigation/opengl-render-qnx
        /navigation/opengl-render-qnx 10.10.10.10 &
    else
        echo "File /navigation/opengl-render-qnx does not exist."
    fi
```

Final method should look like this:
```
normal_startup()
{
    set_environment_variables
    start_early_framework &
    start_early_drivers

    check_filesystems

    start_drivers &
    info "create_ramdisk ..."
    create_sysramdisk 20
    create_ramdisk 10 organizer /organizerdisk
    check_sop
    waitfor_quick /mnt/app/eso $TIMEOUT
    start_network &
    start_framework
    start_hmi &

    waitfor_quick /mnt/app/img_ver.txt $TIMEOUT
    { read IMG_VER1; read IMG_VER2; } < /mnt/app/img_ver.txt
    info "MMX BENCH_Startup IMG_VER $IMG_VER1 $IMG_VER2"
    echo "MMX BENCH_Startup IMG_VER $IMG_VER1 $IMG_VER2"
    pidin info
    start_system_services &
    start_autorunner &

    # New driver from eso replaced i2c-smsc_bridge
    if [ -f /mnt/app/mediaconnectorVerbosity-v3_enabled ]
    then
        /eso/bin/apps/mediaconnector -v3 -map 0x11,0x10 &
    elif [ -f /mnt/app/mediaconnectorVerbosity-v6_enabled ]
    then
        /eso/bin/apps/mediaconnector -v6 -map 0x11,0x10 &
    else
        /eso/bin/apps/mediaconnector -v1 -map 0x11,0x10 &
    fi

    start_system_tools

    start_dvdrom_driver

    #  MOST not for DELPHI
    # DTV
    waitfor_quick /net/rcc/dev/name/local/inic/isoRX1 2
    if [ $? -eq 0 ]; then
        devp-iso-mmx-mib2 -R -S196 -i0 -B3 -P32 -Q24 -m/dev/mlb -MisoRX1 -v5 -p16 &
    fi
    # AVDC
    waitfor_quick /net/rcc/dev/name/local/inic/isoRX2 2
    if [ $? -eq 0 ]; then
        devp-iso-mmx-mib2 -R -S196 -i1 -B3 -P32 -Q24 -m/dev/mlb -MisoRX2 -v5 -p16 &
    fi
    # Passenger Map
    waitfor_quick /net/rcc/dev/name/local/inic/isoTX1 2
    if [ $? -eq 0 ]; then
        devp-iso-mmx-mib2 -T -S188 -i2 -B3 -P64 -Q18 -m/dev/mlb -MisoTX1 -v5 -p16 &
    fi
    # DCIVIDEO: Kombi Map
    waitfor_quick /net/rcc/dev/name/local/inic/isoTX2 2
    if [ $? -eq 0 ]; then
        devp-iso-mmx-mib2 -T -S188 -i3 -B3 -P64 -Q18 -m/dev/mlb -MisoTX2 -v5 -p16 &
    fi

    if [ -f /navigation/opengl-render-qnx ]; then
        chmod 0777 /navigation/opengl-render-qnx
        /navigation/opengl-render-qnx &
    else
        echo "File /navigation/opengl-render-qnx does not exist."
    fi
}
```
4. restart MIB to see if everything works as expected






# VcMOSTRenderMqb
Way to write custom data to Virtual cockpit for MQB platform
Supported screens (as of now probably most screens which are able to receive MOST map will be supported):

continental VDO (navigation support required)
> 
> 3G0920791 - passat B8 / arteon
> 
> 5NA920791 - tiguan
> 
> 5G1920791 - golf mk7
> 
> 5G1920794 - golf mk7 GTE
> 
> 5G1920795 - e-golf mk7
> 
> 5G1920798 - golf mk7
> 
> 3CG920791 - vw atlas

opengl-render-qnx C++ -> much faster then first python implementation can render around 10fps
also installation should be much less painfull as it does not require anything special just compiled binary and some way to run it on MIB2 device


https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/0b193c30-7f72-46c4-8433-422bf9de17b9


OLD WORK:

To make this work you need to install Python3.3 to MIB2.5 first using following package repositories: https://pkgsrc.mibsolution.one then save current version of VCRenderData.py to sd card or upload it via winSCP
To run the code then execute 

> python3.3 /fs/sda0/VCRenderData.py

This is what can be achieved for now
![image](https://raw.githubusercontent.com/OneB1t/VcMOSTRenderMqb/main/render.bmp)

https://github.com/jilleb/mib2-toolbox/assets/320479/d625360f-629a-4b98-9ecd-61b4ec68585a

![image](https://user-images.githubusercontent.com/320479/280536425-d9cbde9c-9c01-4852-8c2f-9d4c5a4a7283.png)

Now font can be scaled and also some new characters/icons appeared
![image](https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/25234d56-b886-448c-ae48-5f27bde40418)


My next goal is to get some usefull data from exlap channel and render them in theory it should be possible to also get data/image from AA/CP and render them onto VC. Any help or ideas are appreciated.

Also to be able to run Android Auto on main unit and data on virtual cluster it is needed to patch navigation in M.I.B. Navignore

