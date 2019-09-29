#!/bin/bash
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh

while true; do
	wget -t 1 -T 1 http://$OPENWRT_IP/$OPENWRT_CONF_PATH -O /var/run/wbc/config.ini >/dev/null 2>&1
	if [ ! -f "$OPENWRT_CONFIG_FILE" ]; then
		/bin/bash /root/wbc-scripts/wbc-air-genconf.sh
	fi
		sleep 3
done


