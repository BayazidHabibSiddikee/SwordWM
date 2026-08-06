# =========================================================
# SwordWM Makefile
# =========================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11 \
          -Iinclude \
          $(shell pkg-config --cflags x11 xft fontconfig)
LDFLAGS = $(shell pkg-config --libs x11 xft fontconfig) -lm

# Verify required pkg-config packages exist at make time
_ := $(shell pkg-config --exists x11 xft fontconfig || \
     { echo "ERROR: Missing pkg-config packages. Install: libx11-dev libxft-dev libfontconfig1-dev"; exit 1; })

PREFIX  = /usr/local
OBJDIR  = .obj

SRCS = src/main.c \
       src/core/x11.c \
       src/core/client.c \
       src/core/workspace.c \
       src/layout/layout.c \
       src/input/input.c \
       src/decorate/decorate.c \
       src/ewmh/ewmh.c \
       src/config/config.c

# Object files go into .obj/ mirroring src/ structure
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)
BIN  = swordwm

# ── Targets ───────────────────────────────────────────────

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built: $(BIN)"

# Compile + generate dependency files
$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Include auto-generated dependency files (silently on first build)
-include $(DEPS)

debug: CFLAGS  += -g -O0 -DDEBUG -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(BIN)

clean:
	rm -rf $(OBJDIR) $(BIN)

# distclean: also removes editor/IDE artifacts
distclean: clean
	rm -f tags TAGS cscope.out
	find . -name "*.orig" -o -name "*.rej" | xargs rm -f 2>/dev/null || true

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 config/swordwm.conf \
	        $(DESTDIR)/etc/xdg/swordwm/config
	@echo "Installed: $(DESTDIR)$(PREFIX)/bin/$(BIN)"
	@echo "Default config: $(DESTDIR)/etc/xdg/swordwm/config"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)/etc/xdg/swordwm/config

# ── Xephyr test ────────────────────────────────────────────
# make test-xephyr   — nested X at :1 with swordwm + terminal
# Install Xephyr:  sudo pacman -S xorg-server-xephyr
#               OR sudo apt install xserver-xephyr
TERMINAL_TEST ?= $(shell which ghostty alacritty kitty xterm 2>/dev/null | head -1)

test-xephyr: $(BIN)
	@which Xephyr >/dev/null 2>&1 || \
	  { echo "Install Xephyr: sudo pacman -S xorg-server-xephyr  OR  sudo apt install xserver-xephyr"; exit 1; }
	@pkill -x Xephyr 2>/dev/null || true
	Xephyr :1 -screen 1280x720 -br &
	sleep 1.2
	DISPLAY=:1 xsetroot -solid "\#1a1b26" 2>/dev/null || true
	DISPLAY=:1 ./$(BIN) &
	sleep 0.8
	DISPLAY=:1 LIBGL_ALWAYS_SOFTWARE=1 GDK_BACKEND=x11 GDK_DISABLE=gles-api,vulkan $(TERMINAL_TEST) &

.PHONY: all debug clean distclean install uninstall test-xephyr
