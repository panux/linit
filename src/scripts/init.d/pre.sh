#!/etc/rc.common

#Pre-boot targets

depends() {
    if [ -d /etc/rc.d/pre ]; then
        dep $(ls /etc/rc.d/pre)
    fi
}

enable() {
    if [ ! -d /etc/rc.d/pre ]; then
        mkdir /etc/rc.d/pre
    fi
}

disable() {
    rm -r /etc/rc.d/pre
}
