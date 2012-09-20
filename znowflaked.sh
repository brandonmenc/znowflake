#!/bin/sh
#
# znowflaked - this script starts and stops the znowflake daemon
#
# chkconfig:   - 85 15
# description: Znowflake generates unique ID numbers
# processname: znowflaked
# config:      /etc/znowflake/znowflake.conf
# pidfile:     /var/run/znowflaked.pid
 
# Source function library.
. /etc/rc.d/init.d/functions
 
znowflaked="/usr/local/bin/znowflaked"
prog=$(basename $znowflaked)

ZNOWFLAKE_CONF_FILE="/etc/znowflake/znowflake.conf"

lockfile=/var/lock/subsys/znowflaked

start() {
    [ -x $znowflaked ] || exit 5
    echo -n $"Starting $prog: "
    daemon $znowflaked -d -f $ZNOWFLAKE_CONF_FILE
    retval=$?
    echo
    [ $retval -eq 0 ] && touch $lockfile
    return $retval
}
 
stop() {
    echo -n $"Stopping $prog: "
    killproc $prog -QUIT
    retval=$?
    echo
    [ $retval -eq 0 ] && rm -f $lockfile
    return $retval
}
 
restart() {
    configtest || return $?
    stop
    sleep 1
    start
}
 
rh_status() {
    status $prog
}
 
rh_status_q() {
    rh_status >/dev/null 2>&1
}
 
case "$1" in
    start)
        rh_status_q && exit 0
        $1
        ;;
    stop)
        rh_status_q || exit 0
        $1
        ;;
    restart)
        $1
        ;;
    status)
        rh_status
        ;;
    condrestart|try-restart)
        rh_status_q || exit 0
        ;;
    *)
        echo $"Usage: $0 {start|stop|status|restart|condrestart|try-restart}"
        exit 2
esac
