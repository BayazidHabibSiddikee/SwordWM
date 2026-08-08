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

/* Get config directory path (XDG_CONFIG_HOME aware) */
static const char *get_config_dir(void) {
    static char dir[512];
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    
    if (xdg_config && xdg_config[0]) {
        snprintf(dir, sizeof(dir), "%s/swordwm", xdg_config);
    } else if (home) {
        snprintf(dir, sizeof(dir), "%s/.config/swordwm", home);
    } else {
        snprintf(dir, sizeof(dir), "/root/.config/swordwm");
    }
    return dir;
}

/* Send SIGHUP to a running swordwm instance */
static int do_reload(void) {
    char pid_path[512];
    snprintf(pid_path, sizeof(pid_path), "%s/swordwm.pid", get_config_dir());
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
    const char *dir = get_config_dir();
    mkdir(dir, 0700);  /* 0700 — only owner can read PID dir */

    char path[512];
    snprintf(path, sizeof(path), "%s/swordwm.pid", dir);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }
}

static void remove_pid(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/swordwm.pid", get_config_dir());
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
     * Event loop — uses pselect() to atomically check for signals and X events.
     * This eliminates the race condition where SIGHUP could be missed.
     * pselect() will be interrupted by SIGHUP/SIGCHLD (EINTR) which
     * is exactly what we want — we check g_reload then re-block.
     */
    int xfd = ConnectionNumber(wm->dpy);
    XEvent ev;
    sigset_t empty_mask;
    sigemptyset(&empty_mask);

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

        /* Step physics animations if any client is still bouncing */
        if (wm->physics_world && physics_anim_any_active(wm->physics_world))
            physics_anim_step_all(wm->physics_world);

        /* Block until X has data or a signal interrupts us.
         * Use 16ms timeout (~60fps) when animations are active,
         * 1s otherwise (zero CPU cost when idle). */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        struct timespec timeout;
        if (wm->physics_world && physics_anim_any_active(wm->physics_world)) {
            timeout.tv_sec  = 0;
            timeout.tv_nsec = 16000000L; /* 16ms = ~60fps */
        } else {
            timeout.tv_sec  = 1;
            timeout.tv_nsec = 0;
        }
        pselect(xfd + 1, &rfds, NULL, NULL, &timeout, &empty_mask);
        /* Signal or timeout — loop back and check g_reload */
    }

    remove_pid();
    x11_cleanup();
    free(wm);
    fprintf(stderr, "swordwm: goodbye\n");
    return 0;
}
