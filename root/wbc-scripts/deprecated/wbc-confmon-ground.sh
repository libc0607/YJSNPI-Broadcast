#!/bin/bash
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh

if [ -f $OPENWRT_CONFIGURED_FLAG_FILE ]; then
	while true; do
		FLAG=`cat $OPENWRT_CONFIGURED_FLAG_FILE`
		if [[ "$FLAG" == "1" ]]; then
			echo "0" > $OPENWRT_CONFIGURED_FLAG_FILE
			supervisorctl restart wbc-videorx &
			supervisorctl restart wbc-rssirx &
			supervisorctl restart wbc-telemetryrx &
		fi
		sleep 10
	done
else
	sleep 3
fi
