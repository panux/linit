#!/bin/sh

echo $1 $LINITSOCK
/etc/init.d/$1 stop
linitctl state $1 stopped
