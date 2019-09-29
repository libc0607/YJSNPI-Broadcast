#!/bin/bash

source /root/wbc-scripts/env.sh

# Apply OpenWrt config
if [ -e "$OPENWRT_CONFIG_FILE" ]; then
	
    OK=`bash -n $OPENWRT_CONFIG_FILE`
    if [ "$?" == "0" ]; then
#		tmessage "OpenWrt config file $OPENWRT_CONFIG_FILE found"
		source $OPENWRT_CONFIG_FILE
    else
#		tmessage "ERROR: OpenWrt config file $OPENWRT_CONFIG_FILE contains syntax error(s)"
		exit
    fi
else
#   tmessage "ERROR: OpenWrt config file not found "
    exit
fi
