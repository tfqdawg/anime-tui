CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LIBS := $(shell pkg-config --cflags --libs ncursesw sqlite3)

animetui: animetui.c
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f animetui

.PHONY: clean
