#!/etc/rc.common

depends() {
    for i in $(ls /etc/rc.d); do
        dep $i
    done
}

start() {
    echo booted
}
