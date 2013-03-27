all: status-area-applet-battery.so

install:
	install -d "$(DESTDIR)/usr/lib/hildon-desktop/"
	install -d "$(DESTDIR)/usr/share/applications/hildon-status-menu/"
	install -d "$(DESTDIR)/usr/share/gconf/schemas/"
	install -m 644 status-area-applet-battery.so "$(DESTDIR)/usr/lib/hildon-desktop/"
	install -m 644 status-area-applet-battery.desktop "$(DESTDIR)/usr/share/applications/hildon-status-menu/"
	install -m 644 status-area-applet-battery.schemas "$(DESTDIR)/usr/share/gconf/schemas/"

clean:
	$(RM) status-area-applet-battery.so

status-area-applet-battery.so: status-area-applet-battery.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs hildon-1 libcanberra profile libhildondesktop-1 dbus-1 hal) -W -Wall -O2 -shared $^ -o $@

.PHONY: all install clean
