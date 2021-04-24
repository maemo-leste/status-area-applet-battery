hildondesktoplib_LTLIBRARIES = status-area-applet-battery.la

status_area_applet_battery_la_SOURCES = status-area-applet-battery.c \
										batmon.c

status_area_applet_battery_la_CFLAGS = \
	$(LIBHILDON_CFLAGS) $(LIBCANBERRA_CFLAGS) $(LIBHILDONDESKTOP_CFLAGS) \
	$(PROFILE_CFLAGS) $(DBUS_CFLAGS) $(GCONF_CFLAGS) $(UPOWER_GLIB_CFLAGS)

status_area_applet_battery_la_LIBADD = \
	$(LIBHILDON_LIBS) $(LIBCANBERRA_LIBS) $(LIBHILDONDESKTOP_LIBS) \
	$(PROFILE_LIBS) $(DBUS_LIBS) $(GCONF_LIBS) $(UPOWER_GLIB_LIBS)

status_area_applet_battery_la_LDFLAGS = -Wl,--as-needed -module -avoid-version

gconfschemasdir = $(sysconfdir)/gconf/schemas
dist_gconfschemas_DATA = status-area-applet-battery.schemas

hildonstatusmenudesktopentry_DATA = status-area-applet-battery.desktop
EXTRA_DIST = $(hildonstatusmenudesktopentry_DATA)

install-data-hook:
	@(cd $(DESTDIR)$(hildondesktoplibdir) && $(RM) \
		$(hildondesktoplib_LTLIBRARIES))
