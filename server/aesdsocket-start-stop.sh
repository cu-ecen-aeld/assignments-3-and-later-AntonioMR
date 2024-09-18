#!/bin/sh

case "$1" in
    start)
        echo "Starting aesdsocket server"
        start-stop-daemon -S --name aesdsocket --startas aesdsocket -- -d
        ;;
    stop)
        echo "Stopping easdsocket server"
        start-stop-daemon -K --name aesdsocket --signal TERM
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0