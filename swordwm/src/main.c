/* =========================================================
 * main.c — SwordWM entry point
 * ========================================================= */
#include "swordwm.h"

/* Global WM state — accessible everywhere via extern */
WMState *wm = NULL;

static void usage(void) {
    fprintf(stderr,
        "swordwm - a hybrid tiling/floating X11 window manager\n\n"
        "Usage: swordwm [OPTIONS]\n\n"
        "Options:\n"
        "  -v, --version   Print version and exit\n"
        "  -h, --help      Show this help\n\n"
        "Start from ~/.xinitrc or a display manager session file.\n"
        "See man swordwm(1) for full documentation.\n");
}

int main(int argc, char *argv[]) {
    /* ── Argument parsing ──────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("swordwm 0.1.0\n");
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
        fprintf(stderr, "swordwm: unknown option: %s\n", argv[i]);
        usage();
        return 1;
    }

    /* ── Connect to X, set up workspaces, grab keys ──────── */
    if (x11_connect() != 0) {
        fprintf(stderr, "swordwm: failed to connect to X display\n");
        return 1;
    }

    /* ── Manage any windows already on screen ─────────────── */
    manage_existing_windows();

    /* ── Run the event loop (blocks until quit) ───────────── */
    fprintf(stderr, "swordwm: running (press Mod+Shift+E to quit)\n");
    event_loop();

    /* ── Clean up ──────────────────────────────────────────── */
    x11_cleanup();
    free(wm);
    fprintf(stderr, "swordwm: goodbye\n");
    return 0;
}
