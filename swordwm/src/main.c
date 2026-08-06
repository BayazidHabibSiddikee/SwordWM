/* =========================================================
 * main.c — SwordWM entry point
 * ========================================================= */
#define _POSIX_C_SOURCE 200809L
#include "swordwm.h"
#include "config_parser.h"

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
    int pid;
    if (fscanf(f, "%d", &pid) == 1) {
        kill(pid, SIGHUP);
        fprintf(stderr, "swordwm: sent SIGHUP to %d\n", pid);
    }
    fclose(f);
    return 0;
}

/* Write our PID so --reload can find us */
static void write_pid(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    /* Ensure dir exists */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/swordwm", home);
    mkdir(dir, 0755);

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
        if (strcmp(argv[i], "-v") == 0)       { printf("swordwm 0.1.0\n"); return 0; }
        if (strcmp(argv[i], "-h") == 0)       { usage(); return 0; }
        fprintf(stderr, "swordwm: unknown option: %s\n", argv[i]);
        usage(); return 1;
    }

    /* Load runtime config first (before X connect so defaults are ready) */
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

    /* Signal handlers */
    signal(SIGHUP,  handle_sighup);
    signal(SIGCHLD, handle_sigchld);

    fprintf(stderr, "swordwm: running — Mod+Shift+E to quit\n");

    /* Event loop — also checks g_reload flag */
    XEvent ev;
    while (wm->running) {
        /* Check SIGHUP reload flag */
        if (g_reload) {
            g_reload = 0;
            config_reload();
        }

        /* Non-blocking event check so we can poll g_reload */
        if (!XPending(wm->dpy)) {
            struct timespec ts = { 0, 10000000 }; /* 10ms */
            nanosleep(&ts, NULL);
            continue;
        }

        XNextEvent(wm->dpy, &ev);
        /* dispatch via event_loop's handler table */
        extern void dispatch_event(XEvent *);
        dispatch_event(&ev);
    }

    remove_pid();
    x11_cleanup();
    free(wm);
    fprintf(stderr, "swordwm: goodbye\n");
    return 0;
}
