#!/sbin/runscript
# Copyright 2004 Gentoo Foundation
# Distributed under the terms of the GNU General Public License, v2 or later

depend() {
        use network logger
}

start() {
	ebegin "Starting tvheadend"

	start-stop-daemon --start \
			--quiet \
			--background \
			--make-pidfile \
			--pidfile /var/run/tvheadend.pid \
			--exec /usr/bin/tvheadend \
			--env HOME=/var/lib/tvheadend \
			--chuid tvheadend \
			-- -u tvheadend -g tvheadend -c /var/lib/tvheadend/.hts/tvheadend
	eend $?
}

stop() {
	ebegin "Stopping tvheadend"
	start-stop-daemon --stop \
			--quiet \
			--pidfile /var/run/tvheadend.pid
	eend $?
}
