#!/etc/rc.common

depends() {
    dep pre welcome linit-supd
}

start() {
    linit-sup --name login -- /bin/sh -c 'sleep 2; while true; do clear; getty 115200 /dev/console; done'
}

stop() {
    linit-sup-stop login
}
