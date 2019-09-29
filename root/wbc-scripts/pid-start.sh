#!/bin/sh
# A tool of running exec in monit
# $1 - pid file path
# $2 - shell command
# e.g. 
# 	./pid-start.sh /tmp/socat.pid "socat - -"
$2 >/dev/null 2>&1 &
echo $! > $1
 