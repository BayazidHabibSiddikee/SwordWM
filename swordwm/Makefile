# =========================================================
# SwordWM Makefile
# =========================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11 \
          -Iinclude \
          $(shell pkg-config --cflags x11 xft fontconfig 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs x11 xft fontconfig 2>/dev/null) -lm

PREFIX  = /usr/local

SRCS = src/main.c \
       src/core/x11.c \
       src/core/client.c \
       src/core/workspace.c \
       src/layout/layout.c \
       src/input/input.c \
       src/decorate/decorate.c \
       src/ewmh/ewmh.c \
       src/config/config.c

OBJS = $(SRCS:.c=.o)
BIN  = swordwm

# ── Targets ───────────────────────────────────────────────

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built: $(BIN)"

%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS  += -g -O0 -DDEBUG -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(BIN)

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/$(BIN)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

# ── Xephyr test ────────────────────────────────────────────
# make test-xephyr   — opens nested X at :1, launches WM + terminal
# Requires: Xephyr  (sudo pacman -S xorg-server-xephyr)
#           alacritty or ghostty or xterm + xfonts-base
TERMINAL_TEST ?= $(shell which ghostty alacritty kitty xterm 2>/dev/null | head -1)

test-xephyr: $(BIN)
	@which Xephyr >/dev/null 2>&1 || \
	  (echo "Install Xephyr: sudo pacman -S xorg-server-xephyr  OR  sudo apt install xserver-xephyr" && exit 1)
	@pkill -x Xephyr 2>/dev/null || true
	Xephyr :1 -screen 1280x720 -br &
	sleep 1.2
	DISPLAY=:1 xsetroot -solid "\#1a1b26" 2>/dev/null || true
	DISPLAY=:1 ./$(BIN) &
	sleep 0.8
	DISPLAY=:1 $(TERMINAL_TEST) &

.PHONY: all debug clean install uninstall test-xephyr
