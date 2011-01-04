#!/sbin/runscript
# Copyright 2004 Gentoo Foundation
# Distributed under the terms of the GNU General Public License, v2 or later

depend() {
        need net
}

start() {
	ebegin "Starting tvheadend"
	start-stop-daemon --start \
			--quiet \
			--background \
			--make-pidfile \
			--pidfile /var/run/tvheadend.pid \
			--exec /usr/bin/tvheadend \
			--chuid tvheadend --
	eend $?
}

stop() {
	ebegin "Stopping tvheadend"
	start-stop-daemon --stop \
			--quiet \
			--pidfile /var/run/tvheadend.pid
	eend $?
}
