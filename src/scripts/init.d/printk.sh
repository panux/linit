#!/etc/rc.common

start() {
    echo 0 > /proc/sys/kernel/printk
}
