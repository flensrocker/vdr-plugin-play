# Copyright 1999-2012 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI="4"

inherit flag-o-matic toolchain-funcs vdr-plugin eutils

if [ ${PV} == "9999" ] ; then
		inherit git-2
		EGIT_REPO_URI="git://projects.vdr-developer.org/vdr-plugin-play.git"
		KEYWORDS=""
else
		SRC_URI="mirror://vdr-developerorg/1005/${P}.tgz"
		KEYWORDS="~x86 ~amd64"
fi

DESCRIPTION="A mediaplayer plugin for VDR."
HOMEPAGE="http://projects.vdr-developer.org/projects/show/plg-play"

LICENSE="AGPL-3"
SLOT="0"
IUSE=""

RDEPEND=">=media-video/vdr-1.7
		>=x11-libs/libxcb-1.8
		x11-libs/xcb-util
		x11-libs/xcb-util-image
		x11-libs/xcb-util-keysyms
		x11-libs/xcb-util-wm
		|| ( media-video/mplayer media-video/mplayer2 )"
DEPEND="${RDEPEND}
		x11-proto/xproto
		sys-devel/gettext
		dev-util/pkgconfig
		sys-devel/make"

src_prepare() {
		vdr-plugin_src_prepare
}

src_compile() {
		local myconf

		myconf="-DHAVE_PTHREAD_NAME"
		#use jpeg && myconf="${myconf} -DUSE_JPEG"

		emake all CC="$(tc-getCC)" CFLAGS="${CFLAGS}" \
			LDFLAGS="${LDFLAGS}" CONFIG="${myconf}" LIBDIR="." || die
}

src_install() {
		vdr-plugin_src_install

		dodoc ChangeLog README.txt
}
