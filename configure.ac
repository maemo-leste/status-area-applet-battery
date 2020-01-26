AC_INIT([Status area battery applet],
        [1.5.0],
        [merlijn@wizzup.org], [status-area-applet-battery])

#[patsubst(esyscmd([dpkg-parsechangelog | sed -n '/^Version: \(.*\)$/ {s//\1/;p}']),],
AM_INIT_AUTOMAKE
LT_INIT([shared disable-static])
AC_CONFIG_FILES([Makefile])

AC_PROG_CC
AC_PROG_LIBTOOL

PKG_CHECK_MODULES(LIBHILDON, hildon-1)
AC_SUBST(LIBHILDON_CFLAGS)
AC_SUBST(LIBHILDON_LIBS)

PKG_CHECK_MODULES(LIBCANBERRA, libcanberra)
AC_SUBST(LIBCANBERRA_CFLAGS)
AC_SUBST(LIBCANBERRA_LIBS)

PKG_CHECK_MODULES(LIBHILDONDESKTOP, libhildondesktop-1)
AC_SUBST(LIBHILDONDESKTOP_CFLAGS)
AC_SUBST(LIBHILDONDESKTOP_LIBS)

PKG_CHECK_MODULES(PROFILE, profile)
AC_SUBST(PROFILE_CFLAGS)
AC_SUBST(PROFILE_LIBS)

PKG_CHECK_MODULES(DBUS, dbus-1)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(GCONF, gconf-2.0)
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(UPOWER_GLIB, upower-glib)
AC_SUBST(UPOWER_GLIB_CFLAGS)
AC_SUBST(UPOWER_GLIB_LIBS)

AC_CONFIG_FILES(status-area-applet-battery.schemas)

hildonstatusmenudesktopentrydir="`$PKG_CONFIG --variable=hildonstatusmenudesktopentrydir libhildondesktop-1`"
AC_SUBST(hildonstatusmenudesktopentrydir)

hildondesktoplibdir="`$PKG_CONFIG --variable=hildondesktoplibdir osso-af-settings`"
AC_SUBST(hildondesktoplibdir)

AC_OUTPUT
