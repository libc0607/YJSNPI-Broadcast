#!/bin/bash
# Note: This script should be called when startup.
# You should add 'bash /path/to/this/script.sh' to /etc/rc.local.

CAM=`/usr/bin/vcgencmd get_camera | nice grep -c detected=1`
rm /etc/monit/conf.d/wbc-air.conf 2>/dev/null
rm /etc/monit/conf.d/wbc-ground.conf 2>/dev/null

if vcgencmd get_throttled | nice grep -q -v "0x0"; then
	TEMP=`cat /sys/class/thermal/thermal_zone0/temp`
	TEMP_C=$(($TEMP/1000))
	if [ "$TEMP_C" -lt 75 ]; then # it must be under-voltage
		echo "  ---------------------------------------------------------------------------------------------------" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "  | ERROR: Under-Voltage detected on the TX Pi. Your Pi is not supplied with stable 5 Volts.        |" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "  | Either your power-supply or wiring is not sufficent, check the wiring instructions in the Wiki. |" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "  | Set Video Bitrate lower in LuCI (~1000kbit) to reduce current consumption!                      |" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "  | When you have fixed wiring/power-supply, delete this file and make sure it doesn't re-appear!   |" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "  ---------------------------------------------------------------------------------------------------" >> /boot/UNDERVOLTAGE-ERROR!!!.txt
		echo "1" > /tmp/undervolt
	else 
		echo "0" > /tmp/undervolt
	fi
else
	echo "0" > /tmp/undervolt
fi

if [ "$CAM" == "0" ]; then
	echo "0" > /tmp/cam
	echo "Camera not found."
	ln -s /root/wbc-scripts/wbc-ground-monit.conf /etc/monit/conf.d/wbc-ground.conf
	/root/wifibroadcast/sharedmem_init_rx
else
	touch /tmp/TX
	echo "1" > /tmp/cam
	echo "Camera found."
	ln -s /root/wbc-scripts/wbc-air-monit.conf /etc/monit/conf.d/wbc-air.conf
fi
dos2unix -n /boot/opconfig.txt /tmp/settings.sh
monit reload
monit restart all
echo Done.
#end
