#!/etc/rc.common

depends() {
    dep printk
}

start() {
    (sleep 2; while true; do clear; getty 115200 /dev/console; done) &
}
