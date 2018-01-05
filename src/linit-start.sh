#!/bin/sh

echo $1 $LINITSOCK
/etc/init.d/$1 start && linitctl state $1 running || linitctl state $1 failed
