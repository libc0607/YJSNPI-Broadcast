#!/bin/bash
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh
OPENWRT_VIDEO_FORWARD_PORT=35000	
OPENWRT_VIDEO_FPS=48
OPENWRT_VIDEO_KEYFRAMERATE=5
OPENWRT_VIDEO_WIDTH=800
OPENWRT_VIDEO_HEIGHT=480
OPENWRT_VIDEO_BITRATE=1024000


OPENWRT_VIDEO_FORWARD_PORT=`cat /var/run/wbc/config.ini|grep video_forward_port|cut -d '=' -f 2`
OPENWRT_VIDEO_FPS=`cat /var/run/wbc/config.ini|grep video_fps|cut -d '=' -f 2`
OPENWRT_VIDEO_KEYFRAMERATE=`cat /var/run/wbc/config.ini|grep video_keyframerate|cut -d '=' -f 2`
OPENWRT_VIDEO_WIDTH=`cat /var/run/wbc/config.ini|grep video_size |cut -d '=' -f 2|cut -d 'x' -f 1`
OPENWRT_VIDEO_HEIGHT=`cat /var/run/wbc/config.ini|grep video_size|cut -d '=' -f 2|cut -d 'x' -f 2`
OPENWRT_VIDEO_BITRATE=`cat /var/run/wbc/config.ini|grep video_bitrate|cut -d '=' -f 2`
OPENWRT_VIDEO_EXTRAPARAMS=`cat /var/run/wbc/config.ini|grep video_extraparams|cut -d '=' -f 2`

# save them to file
echo "#!/bin/bash" > $OPENWRT_CONFIG_FILE
echo "# Auto generated at $(date)" >> $OPENWRT_CONFIG_FILE # make sure the hash of this file change
echo "OPENWRT_VIDEO_FORWARD_PORT=$OPENWRT_VIDEO_FORWARD_PORT" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_FPS=$OPENWRT_VIDEO_FPS" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_KEYFRAMERATE=$OPENWRT_VIDEO_KEYFRAMERATE" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_WIDTH=$OPENWRT_VIDEO_WIDTH" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_HEIGHT=$OPENWRT_VIDEO_HEIGHT" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_BITRATE=$OPENWRT_VIDEO_BITRATE" >> $OPENWRT_CONFIG_FILE
echo "OPENWRT_VIDEO_EXTRAPARAMS=$OPENWRT_VIDEO_EXTRAPARAMS" >> $OPENWRT_CONFIG_FILE
