#!/etc/rc.common

depends() {
    dep pre welcome linit-supd
}

start() {
    linit-sup --name sleep -- /bin/sleep 10
}
