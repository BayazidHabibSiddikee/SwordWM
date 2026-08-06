CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 $(shell pkg-config --cflags cairo x11)
LDFLAGS = $(shell pkg-config --libs cairo x11) -lm
PREFIX  = /usr/local

SRCS = src/main.c src/display_x11.c src/renderer.c \
       src/renderers/gradient.c
OBJS = $(SRCS:.c=.o)
BIN  = swordwm-wallpaper

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

.PHONY: all clean install uninstall
