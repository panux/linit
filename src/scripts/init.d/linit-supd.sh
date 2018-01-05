#!/etc/rc.common

start() {
    linit-supd &
    SUPD_PID=$!
    # unfortunately, it cannot supervise itself
    (wait $SUPD_PID && linitctl state linit-supd stopped || linitctl state linit-supd failed) &
    echo -n $SUPD_PID > /tmp/linit-supd.pid
}

stop() {
    kill $(cat /tmp/linit-supd.pid)
}
