

# VNC2MIB2VC - opengl-render-qnx

https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/54d5f16b-2c2a-484a-a8b8-4e4e5c39675a

![map](https://github.com/OneB1t/VcMOSTRenderMqb/assets/320479/52123bb5-55de-4fb4-871c-195b8fcebdc3)
PRE-REQUISITIES: MIB2 with SSH access, Virtual cockpit, VNC server on phone/notebook (tested with Xiaomi 12 + droidVNC-NG)







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
1) upload compiled file (opengl-render-qnx/arm/o-le-v7/opengl-render-qnx) to MIB2 unit via SCP (10.173.189.1) or use SD-card (/fs/sda0 on unit itself)
2) MIB2 SSH: chmod +x /fs/sda0/opengl-render-qnx
3) MIB2 SSH: use dmdt to prepare new context and switch this context to VC run those 2 commands (later this is not going to be needed)
   - export LD_LIBRARY_PATH=/eso/lib:/armle/lib:/root/lib-target:/armle/lib/dll IPL_CONFIG_DIR=/etc/eso/production
   - /eso/bin/apps/dmdt dc 99 3
   - /eso/bin/apps/dmdt sc 4 99 
5) PHONE: connect mobile phone to MIB2 wifi (WARNING it is expected that phone will get 10.173.189.62 this IP is hardcoded for now)
6) PHONE: run droidVNC-NG (https://github.com/bk138/droidVNC-NG) and set following parameters:
   - port 5900
   - no password
   - scaling (30-50%) this depends on your phone resolution -> lower value means faster streaming
7) MIB2 SSH: run ./fs/sda0/opengl-render-qnx
8) VC: switch to navigation map and it should now show phone image

When you want to get rid if this just restart your MIB2 device and everything is back to stock.




# VcMOSTRenderMqb
Way to write custom data to Virtual cockpit for MQB platform
Supported screens (as of now probably most screens which are able to receive MOST map will be supported):

continental VDO

> 3G0920798 - passat B8 / arteon
> 
> 3G0920791 - passat B8 / arteon
> 
> 3G0920794 - passat B8 GTE
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

