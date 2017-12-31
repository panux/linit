#!/etc/rc.common

start() {
    echo 0 > /proc/sys/kernel/printk
}

enable() {
    /etc/init.d/pre enable
    ln -s /etc/init.d/$this /etc/rc.d/pre/$this
}

disable() {
    rm /etc/rc.d/pre/$this
}
