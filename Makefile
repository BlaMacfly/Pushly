# Pushly — build Linux
CXX      ?= g++
PREFIX   ?= /usr
CXXFLAGS += -O2 -Wall -std=c++17 $(shell pkg-config --cflags gtk+-3.0 gstreamer-1.0 x11 xtst)
LDLIBS   += $(shell pkg-config --libs gtk+-3.0 gstreamer-1.0 x11 xtst)

all: pushly

pushly: main_linux.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

install: pushly
	install -Dm755 pushly $(DESTDIR)$(PREFIX)/bin/pushly
	install -Dm644 start.mp3 $(DESTDIR)$(PREFIX)/share/pushly/start.mp3
	install -Dm644 stop.mp3 $(DESTDIR)$(PREFIX)/share/pushly/stop.mp3
	install -Dm644 PushlyLogo.png $(DESTDIR)$(PREFIX)/share/pushly/PushlyLogo.png
	install -Dm644 PushlyLogo.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/pushly.png
	install -Dm644 pushly.desktop $(DESTDIR)$(PREFIX)/share/applications/pushly.desktop

clean:
	rm -f pushly

.PHONY: all install clean
