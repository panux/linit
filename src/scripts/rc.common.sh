#!/bin/sh

set -e

export this=$(basename "$1")

# Start and stop
start() {
    return 0
}
stop() {
    return 0
}
# Restart
restart() {
    stop
    start
}
# Reload - does restart unless overwritten
reload() {
    restart
}
# Boot and shutdown actions (default same as start and stop)
boot() {
    start
}
shutdown() {
    stop
}

# Getting runlevel - default to 3
runlevel() {
    echo 3
}

# Disabling and enabling services
disable() {
    rm -f /etc/rc.d/$this
}
enable() {
    ln -s /etc/init.d/$this /etc/rc.d/$this
}

help() {
    cat /etc/init.help
}

dep() {
    linitctl start $@
}

depends() {
    return 0
}

dependencies() {
    depends
}

. "$1"

cmd=help
if [ "$2" == start ]; then
    dependencies
done
for i in stop restart reload boot shutdown disable enable depscan runlevel help fail; do
    if [ $i == "$2" ]; then
        cmd=$2
    fi
done
$cmd
