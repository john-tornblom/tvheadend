# Copyright 1999-2009 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

inherit eutils toolchain-funcs

DESCRIPTION="Combined DVB reciever, Digital Video Recorder and Showtime/XBMC streaming server for Linux"
HOMEPAGE="http://lonelycoder.com/hts"
SRC_URI="http://www.lonelycoder.com/debian/dists/hts/main/source/${PN}_${PV}.tar.gz"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~x86 ~amd64"
IUSE="avahi cwc dvb v4l mmx sse2 xmltv"

RDEPEND="avahi? ( net-dns/avahi )
	xmltv? ( media-tv/xmltv )"

DEPEND="${RDEPEND}
	dev-util/pkgconfig
	v4l? ( sys-kernel/linux-headers )
	dvb? ( media-tv/linuxtv-dvb-headers )"

src_compile() {
	econf $(use_enable nls) \
		$(use_enable avahi) \
		$(use_enable cwc) \
		$(use_enable dvb) \
		$(use_enable mmx) \
		$(use_enable sse2) \
		$(use_enable v4l) \
		--cc="$(tc-getCC)" \
		--release \
		|| die "configure failed"
	emake || die "emake failed"
}

src_install () {
	make DESTDIR="${D}" install || die

	newinitd "${FILESDIR}/tvheadend.init.d" tvheadend
}

pkg_postinst () {
	enewgroup tvheadend
	enewuser tvheadend -1 -1 /var/tvheadend tvheadend

	mkdir -p "/var/lib/tvheadend/.hts/tvheadend"   
	if [ ! -e /var/lib/tvheadend/.hts/tvheadend/superuser ]; then
		echo "{ \"username\": \"admin\", \"password\": \"admin\" }" > /var/lib/tvheadend/.hts/tvheadend/superuser
	fi
	chown -R tvheadend:tvheadend /var/lib/tvheadend/.hts
	chmod -R 600 /var/lib/tvheadend/.hts
	einfo
	einfo "Start tvheadend by calling on /etc/init.d/tvheadend start"
	einfo "The webif will the be accessable on http port 9981"
	einfo ""
	einfo "Username: admin"
	einfo "Password: admin"
	einfo ""
}
