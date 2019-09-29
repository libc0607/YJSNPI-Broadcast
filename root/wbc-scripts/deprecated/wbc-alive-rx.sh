#!/bin/bash
source /root/wbc-scripts/env.sh
source /root/wbc-scripts/func_logconf.sh
source /root/wbc-scripts/func_applyconf.sh

# function to check if packets coming in, if not, re-start wbc-videorx to clear frozen display
while true; do
	OPENWRT_AUTH_TOKEN=$(cat $OPENWRT_TOKEN_FILE)
	echo "Token: $OPENWRT_AUTH_TOKEN"
	ALIVE_MSG=`curl -s -w " httpcode:%{http_code}\n"  -b sysauth=$OPENWRT_AUTH_TOKEN http://$OPENWRT_IP/cgi-bin/luci/admin/wbc/check_alive`
	ALIVE_CODE=`echo $ALIVE_MSG |grep httpcode|cut -d ':' -f 3`
	ALIVE_STATUS=`echo $ALIVE_MSG |grep alive|cut -d ' ' -f 1|jq '.alive'|cut -d '"' -f 2`
	echo "Got response: $ALIVE_MSG"
	echo "HTTP Code $ALIVE_CODE, check_alive status $ALIVE_STATUS"
	if [[ "$ALIVE_CODE" == "200" ]]; then
		if [[ "$ALIVE_STATUS" == "0" ]]; then
			echo "No new packets, restarting hello_video and sleeping for 3s ..."
			supervisorctl restart wbc-videorx 
		else
			echo "Received packets, do nothing ..."
			echo 1 > $CHECK_ALIVE_FILE
		fi
	else
		echo "Get /cgi-bin/luci/admin/wbc/check_alive failed, maybe the token is expired? "
	fi
	sleep 3
done
