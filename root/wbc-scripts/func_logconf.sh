#!/bin/bash
source /root/wbc-scripts/env.sh
if [ -e "$OPENWRT_LOGIN_FILE" ]; then
#	dos2unix -n $OPENWRT_LOGIN_FILE /tmp/settings.sh
    OK=`bash -n /tmp/settings.sh`
    if [ "$?" == "0" ]; then
		source /tmp/settings.sh
    else
		exit
    fi
else
    exit
fi
