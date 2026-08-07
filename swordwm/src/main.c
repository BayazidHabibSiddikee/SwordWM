/* =========================================================
 * main.c — SwordWM entry point
 * ========================================================= */
#define _POSIX_C_SOURCE 200809L
#include "swordwm.h"
#include "config_parser.h"

/* dispatch_event is defined in x11.c */
void dispatch_event(XEvent *ev);

/* Global WM state */
WMState *wm = NULL;

/* SIGHUP flag — set by signal handler, checked in event loop */
static volatile sig_atomic_t g_reload = 0;

static void handle_sighup(int sig) {
    (void)sig;
    g_reload = 1;
}

/* Reap zombie child processes (autostart, spawn) */
static void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void usage(void) {
    fprintf(stderr,
        "swordwm - hybrid tiling/floating X11 window manager\n\n"
        "Usage: swordwm [OPTIONS]\n\n"
        "Options:\n"
        "  --reload    Send SIGHUP to running swordwm (reloads config)\n"
        "  -v          Print version and exit\n"
        "  -h          Show this help\n");
}

/* Send SIGHUP to a running swordwm instance */
static int do_reload(void) {
    const char *home = getenv("HOME");
    if (!home) return 1;
    char pid_path[256];
    snprintf(pid_path, sizeof(pid_path),
             "%s/.config/swordwm/swordwm.pid", home);
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        fprintf(stderr, "swordwm: no pid file at %s\n", pid_path);
        return 1;
    }
    pid_t pid = 0;
    if (fscanf(f, "%d", &pid) == 1 && pid > 1) {
        kill(pid, SIGHUP);
        fprintf(stderr, "swordwm: sent SIGHUP to %d\n", (int)pid);
    }
    fclose(f);
    return 0;
}

/* Write our PID so --reload can find us */
static void write_pid(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/swordwm", home);
    mkdir(dir, 0700);  /* 0700 — only owner can read PID dir */

    char path[512];
    snprintf(path, sizeof(path), "%s/swordwm.pid", dir);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }
}

static void remove_pid(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path),
             "%s/.config/swordwm/swordwm.pid", home);
    unlink(path);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reload") == 0) return do_reload();
        if (strcmp(argv[i], "-v") == 0) { printf("swordwm 0.1.0\n"); return 0; }
        if (strcmp(argv[i], "-h") == 0) { usage(); return 0; }
        fprintf(stderr, "swordwm: unknown option: %s\n", argv[i]);
        usage();
        return 1;
    }

    /* Load runtime config before X connect so defaults are ready */
    config_load();

    /* Connect to X, set up workspaces, grab keys */
    if (x11_connect() != 0) {
        fprintf(stderr, "swordwm: failed to connect to X display\n");
        return 1;
    }

    /* Manage any pre-existing windows */
    manage_existing_windows();

    /* Run autostart commands */
    config_run_autostart();

    /* Write PID file for --reload */
    write_pid();

    /* Signal handlers (override the ones set by x11_connect) */
    signal(SIGHUP,  handle_sighup);

    /* SIGCHLD: reap zombies. Use SA_RESTART so that select() is NOT
     * interrupted by child exits — without SA_RESTART every spawned
     * process causes a spurious EINTR wakeup that spins the event loop. */
    {
        struct sigaction sa;
        sa.sa_handler = handle_sigchld;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigaction(SIGCHLD, &sa, NULL);
    }

    fprintf(stderr, "swordwm: running — Mod+Shift+E to quit\n");

    /*
     * Event loop — uses select() on the X connection fd so we can
     * respond to signals (g_reload) without busy-waiting.
     * select() will be interrupted by SIGHUP/SIGCHLD (EINTR) which
     * is exactly what we want — we check g_reload then re-block.
     */
    int xfd = ConnectionNumber(wm->dpy);
    XEvent ev;

    while (wm->running) {
        /* Check SIGHUP reload flag before blocking */
        if (g_reload) {
            g_reload = 0;
            config_reload();
        }

        /* Drain any already-queued events without blocking */
        while (XPending(wm->dpy)) {
            XNextEvent(wm->dpy, &ev);
            dispatch_event(&ev);
        }

        if (!wm->running) break;

        /* Block until X has data or a signal interrupts us */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        /* 1-second timeout so g_reload is never stale for long */
        struct timeval tv = { 1, 0 };
        select(xfd + 1, &rfds, NULL, NULL, &tv);
        /* EINTR (signal) or timeout — loop back and check g_reload */
    }

    remove_pid();
    x11_cleanup();
    free(wm);
    fprintf(stderr, "swordwm: goodbye\n");
    return 0;
}
