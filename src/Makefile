OBJS=status-area-applet-battery.o
SOURCES=status-area-applet-battery.c
LIB=status-area-applet-battery.so
PKG_FLAGS=$(shell pkg-config hildon-1 hildon-notify profile libhildondesktop-1 dbus-1 hal --libs --cflags)
CCFLAGS=-shared -Wall
CC=gcc

all:$(LIB)

$(LIB):$(OBJS)
	$(CC) $(CCFLAGS) $(PKG_FLAGS) $(OBJS) -o $(LIB)

.c.o:
	$(CC) $(CCFLAGS) $(PKG_FLAGS) -c $< -o $@

.PHONE: clean all

clean:
	rm -f $(OBJS) $(LIB)
