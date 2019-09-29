#!/bin/sh
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh
source /root/wbc-scripts/func_applyconf.sh

if [[ "$OPENWRT_VIDEO_FORWARD_PORT" == "null" ]] || [[ "$OPENWRT_VIDEO_FORWARD_PORT" == "" ]]; then
	echo $$ > $1
	exit
fi
socat -u EXEC:"raspivid -w $OPENWRT_VIDEO_WIDTH -h $OPENWRT_VIDEO_HEIGHT -fps $OPENWRT_VIDEO_FPS -b $OPENWRT_VIDEO_BITRATE -g $OPENWRT_VIDEO_KEYFRAMERATE -t 0 $OPENWRT_VIDEO_EXTRAPARAMS -o -" UDP:$OPENWRT_IP:$OPENWRT_VIDEO_FORWARD_PORT,sourceport=30000 >/dev/null 2>&1 & 
echo $! > $1



