#!/bin/sh

# if [[ "$OPENWRT_RSSI_FORWARD_PORT" == "null" ]]; then
	# echo "RSSI RX: Disabled."
	# sleep 365d
# fi

/root/wifibroadcast/rssi_forward_in /var/run/wbc/config.ini
