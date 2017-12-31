#!/etc/rc.common

depends() {
    dep $(ls /etc/rc.d)
}

start() {
    echo booted
}
