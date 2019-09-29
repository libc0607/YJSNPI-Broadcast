#!/bin/bash
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh

# pid 
echo $$ > $1

# Default values
OPENWRT_AUTH_TOKEN=233	
OPENWRT_VIDEO_FORWARD_PORT=35000	# Both Air and Ground
OPENWRT_TELE_FORWARD_PORT=35001		# Ground only
OPENWRT_RSSI_FORWARD_PORT=35002		# Ground only
OPENWRT_VIDEO_FPS=48
OPENWRT_VIDEO_KEYFRAMERATE=5
OPENWRT_VIDEO_WIDTH=800
OPENWRT_VIDEO_HEIGHT=480
OPENWRT_VIDEO_BITRATE=1024000

while true; do

#	echo "===================================================================================="
#	echo "Try to connect OpenWrt at $OPENWRT_IP, Username $OPENWRT_USERNAME, Password $OPENWRT_PASSWORD, Token $OPENWRT_AUTH_TOKEN"
	TOKEN_ALIVE=`curl -s -i -X POST -b sysauth=$OPENWRT_AUTH_TOKEN -d '{"method":"uptime"}' http://$OPENWRT_IP/cgi-bin/luci/rpc/sys 2>/dev/null|grep HTTP/1.1|cut -d ' ' -f 2 `
#	echo "token_alive: $TOKEN_ALIVE"
	if [[ "$TOKEN_ALIVE" == "200" ]]; then
#		echo "Token seems valid now, do nothing..."
	else
#		echo "OpenWrt auth token expired or error, refreshing..."
		OPENWRT_AUTH_TOKEN=`curl -s -i -X POST -d '{"method":"login","params":["'$OPENWRT_USERNAME'","'$OPENWRT_PASSWORD'"]}' http://$OPENWRT_IP/cgi-bin/luci/rpc/auth|tail -n 1|jq '.result'|cut -d '"' -f 2`
		echo "$OPENWRT_AUTH_TOKEN" > $OPENWRT_TOKEN_FILE
#		echo "Got new token $OPENWRT_AUTH_TOKEN, save to $OPENWRT_TOKEN_FILE"
	fi
	

	# check if we have new config or wbc on openwrt has been restarted
	OPENWRT_CHECKMSG=`curl -s -X POST -b sysauth=$OPENWRT_AUTH_TOKEN http://$OPENWRT_IP/cgi-bin/luci/admin/wbc/check_config |tail -n 1`
	OPENWRT_RESTART_TS=`echo $OPENWRT_CHECKMSG|jq '.timestamp'|cut -d '"' -f 2`
	OPENWRT_CONFIG_MD5=`echo $OPENWRT_CHECKMSG|jq '.configmd5'|cut -d '"' -f 2`
	OPENWRT_CONFIG_MD5_OLD=`cat $OPENWRT_CONFIG_MD5_FILE`
	tmessage "New config MD5 is $OPENWRT_CONFIG_MD5, old MD5 is $OPENWRT_CONFIG_MD5_OLD"
	if [[ "$OPENWRT_CONFIG_MD5" == "$OPENWRT_CONFIG_MD5_OLD" ]]; then	
#		echo "Config MD5 Not changed, do nothing..."
	else
		echo "0" > $OPENWRT_CONFIGURED_FLAG_FILE
		echo "$OPENWRT_RESTART_TS" > $OPENWRT_RESTART_TS_FILE
		echo "$OPENWRT_CONFIG_MD5" > $OPENWRT_CONFIG_MD5_FILE
		sync
		CONFIG_JSONMSG_FULL=`curl -i -X POST -b sysauth=$OPENWRT_AUTH_TOKEN http://$OPENWRT_IP/cgi-bin/luci/admin/wbc/get_initconfig |tail -n 1`
		OPENWRT_VIDEO_FORWARD_PORT=`echo $CONFIG_JSONMSG_FULL |jq '.videoport'|cut -d '"' -f 2`
		OPENWRT_TELE_FORWARD_PORT=`echo $CONFIG_JSONMSG_FULL |jq '.teleport'|cut -d '"' -f 2`
		OPENWRT_RSSI_FORWARD_PORT=`echo $CONFIG_JSONMSG_FULL |jq '.rssiport'|cut -d '"' -f 2`
		OPENWRT_VIDEO_FPS=`echo $CONFIG_JSONMSG_FULL |jq '.fps'|cut -d '"' -f 2`
		OPENWRT_VIDEO_KEYFRAMERATE=`echo $CONFIG_JSONMSG_FULL |jq '.keyframerate'|cut -d '"' -f 2`
		OPENWRT_VIDEO_WIDTH=`echo $CONFIG_JSONMSG_FULL |jq '.imgsize'|cut -d '"' -f 2|cut -d 'x' -f 1`
		OPENWRT_VIDEO_HEIGHT=`echo $CONFIG_JSONMSG_FULL |jq '.imgsize'|cut -d '"' -f 2|cut -d 'x' -f 2`
		OPENWRT_VIDEO_BITRATE=`echo $CONFIG_JSONMSG_FULL |jq '.bitrate'|cut -d '"' -f 2`
		
		# save them to file
		echo "#!/bin/bash" > $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_FORWARD_PORT=$OPENWRT_VIDEO_FORWARD_PORT" >> $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_FPS=$OPENWRT_VIDEO_FPS" >> $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_KEYFRAMERATE=$OPENWRT_VIDEO_KEYFRAMERATE" >> $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_WIDTH=$OPENWRT_VIDEO_WIDTH" >> $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_HEIGHT=$OPENWRT_VIDEO_HEIGHT" >> $OPENWRT_CONFIG_FILE
		echo "OPENWRT_VIDEO_BITRATE=$OPENWRT_VIDEO_BITRATE" >> $OPENWRT_CONFIG_FILE

		# Generate .ini 
		cat >> /var/run/wbc/config.ini <<EOF 
\# config.ini 
\# Do not modify - this file is auto generated
[alive_in]	
udp_port=$OPENWRT_VIDEO_ALIVE_PORT
exec=monit restart wbc-hellovideo

[video]
mode=rx	
udp_port=$OPENWRT_VIDEO_FORWARD_PORT
udp_bufsize=524288 	#byte

[rssi_in]
mode=rx
udp_port=$OPENWRT_RSSI_FORWARD_PORT

EOF

	fi
	sleep 3
done


