# =========================================================
# SwordWM Makefile
# =========================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11 \
          -Iinclude \
          $(shell pkg-config --cflags x11 xkbcommon 2>/dev/null || echo "-I/usr/include/X11")
LDFLAGS = -lX11 -lm
PREFIX  = /usr/local

SRCS = src/main.c \
       src/core/x11.c \
       src/core/client.c \
       src/core/workspace.c \
       src/layout/layout.c \
       src/input/input.c

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

debug: CFLAGS += -g -O0 -DDEBUG -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(BIN)

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/$(BIN)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

# ── Xephyr test target ─────────────────────────────────────
# Run a nested X server for safe development testing.
# Usage: make test-xephyr
test-xephyr: $(BIN)
	@which Xephyr >/dev/null 2>&1 || (echo "Install Xephyr: sudo pacman -S xorg-server-xephyr" && exit 1)
	@echo "Starting Xephyr on :1 — open a terminal there and test"
	Xephyr :1 -screen 1280x720 &
	sleep 1
	DISPLAY=:1 ./$(BIN) &
	DISPLAY=:1 xterm &

.PHONY: all debug clean install uninstall test-xephyr
