LIBS=hildon-1 libcanberra profile libhildondesktop-1 dbus-1 gconf-2.0 upower-glib
CFLAGS=-c `pkg-config --cflags $(LIBS)`
LDFLAGS=`pkg-config --libs $(LIBS)`
CFLAGS+=-Wall -Werror -O2 -fpic
LDFLAGS+=-shared

all: status-area-applet-battery.so

status-area-applet-battery.so: status-area-applet-battery.o batmon.o
	$(CC) $(LDFLAGS) $^ -o $@

status-area-applet-battery.o: status-area-applet-battery.c batmon.h
	$(CC) $(CFLAGS) $< -o $@

batmon.o: batmon.c batmon.h
	$(CC) $(CFLAGS) $< -o $@

install:
	install -d "$(DESTDIR)/usr/lib/hildon-desktop/"
	install -d "$(DESTDIR)/usr/share/applications/hildon-status-menu/"
	install -d "$(DESTDIR)/usr/share/gconf/schemas/"
	install -m 644 status-area-applet-battery.so "$(DESTDIR)/usr/lib/hildon-desktop/"
	install -m 644 status-area-applet-battery.desktop "$(DESTDIR)/usr/share/applications/hildon-status-menu/"
	install -m 644 status-area-applet-battery.schemas "$(DESTDIR)/usr/share/gconf/schemas/"

clean:
	$(RM) status-area-applet-battery.{so,o}

.PHONY: all install clean
