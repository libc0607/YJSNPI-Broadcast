#!/bin/sh
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh
source /root/wbc-scripts/func_applyconf.sh

OSD_CONFIG_FILE_MD5_PATH=/var/run/wbc/osdconfig_md5
OSD_CONFIG_FILE_MD5_NEW=`md5sum /boot/osdconfig.txt|cut -d ' ' -f 1`
OSD_CONFIG_FILE_MD5_OLD=`cat $OSD_CONFIG_FILE_MD5_PATH`



/tmp/osd $OPENWRT_TELE_FORWARD_PORT > /dev/null

echo "OSD Stopped."
