#define _POSIX_C_SOURCE 199309L
#include "ww.h"
#include "display.h"
#include "renderer.h"
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

static const char *state_file(void) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/.config/swordwm-wallpaper/state", getenv("HOME"));
    return path;
}

static const char *pid_file(void) {
    static char path[256];
    snprintf(path, sizeof(path), "%s/.config/swordwm-wallpaper/pid", getenv("HOME"));
    return path;
}

static void save_state(const char *mode) {
    FILE *f = fopen(state_file(), "w");
    if (f) { fprintf(f, "%s\n", mode); fclose(f); }
}

static void save_pid(void) {
    FILE *f = fopen(pid_file(), "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }
}

static void remove_pid(void) {
    unlink(pid_file());
}

static void usage(void) {
    fprintf(stderr,
        "swordwm-wallpaper - animated desktop background\n\n"
        "Usage: swordwm-wallpaper [OPTIONS]\n\n"
        "Options:\n"
        "  --mode MODE    Set mode: gradient, particles, wave, matrix, graph\n"
        "  --next         Cycle to next mode\n"
        "  --stop         Stop running instance\n"
        "  --list         List available modes\n"
        "  --help         Show this help\n");
}

static void list_modes(void) {
    printf("Modes: gradient, particles, wave, matrix, graph\n");
}

static void stop_running(void) {
    FILE *f = fopen(pid_file(), "r");
    if (f) {
        int pid;
        if (fscanf(f, "%d", &pid) == 1)
            kill(pid, SIGTERM);
        fclose(f);
    }
    unlink(pid_file());
}

static const char *next_mode(const char *cur) {
    const char *modes[] = {"gradient", "particles", "wave", "matrix", "graph"};
    int n = 5;
    for (int i = 0; i < n; i++) {
        if (strcmp(modes[i], cur) == 0)
            return modes[(i + 1) % n];
    }
    return "gradient";
}

static const char *read_state(void) {
    static char buf[64] = "gradient";
    FILE *f = fopen(state_file(), "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\n")] = 0;
        }
        fclose(f);
    }
    return buf;
}

int main(int argc, char *argv[]) {
    const char *mode = NULL;
    int do_next = 0, do_stop = 0, do_list = 0;

    static struct option long_opts[] = {
        {"mode",  required_argument, NULL, 'm'},
        {"next",  no_argument,       NULL, 'n'},
        {"stop",  no_argument,       NULL, 's'},
        {"list",  no_argument,       NULL, 'l'},
        {"help",  no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "m:nlsh", long_opts, NULL)) != -1) {
        switch (c) {
            case 'm': mode = optarg; break;
            case 'n': do_next = 1; break;
            case 's': do_stop = 1; break;
            case 'l': do_list = 1; break;
            case 'h': usage(); return 0;
            default:  usage(); return 1;
        }
    }

    if (do_list)  { list_modes();  return 0; }
    if (do_stop)  { stop_running(); return 0; }

    if (do_next)
        mode = next_mode(read_state());
    else if (!mode)
        mode = read_state();

    save_state(mode);

    WWDisplay d;
    if (display_init(&d) != 0)
        return 1;

    display_create_window(&d);
    display_set_desktop_hints(&d);
    display_create_surface(&d);

    const WWRenderer *ren = renderer_find(mode);
    ren->init(d.sw, d.sh);
    printf("[swordwm-wallpaper] Mode: %s (%dx%d)\n", mode, d.sw, d.sh);

    save_pid();

    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    while (d.running) {
        while (XPending(d.dpy)) {
            XEvent ev;
            XNextEvent(d.dpy, &ev);
            switch (ev.type) {
                case Expose:
                    break;
                case ConfigureNotify:
                    d.sw = ev.xconfigure.width;
                    d.sh = ev.xconfigure.height;
                    display_create_surface(&d);
                    break;
                case KeyPress:
                    if (XLookupKeysym(&ev.xkey, 0) == XK_q)
                        d.running = 0;
                    break;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double t = (ts_now.tv_sec - ts_start.tv_sec) +
                   (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;

        ren->render(d.cr, d.sw, d.sh, t);
        cairo_surface_flush(d.surface);
        XFlush(d.dpy);

        struct timespec ts_sleep;
        ts_sleep.tv_sec  = 0;
        ts_sleep.tv_nsec = 16000000; // ~60fps
        nanosleep(&ts_sleep, NULL);
    }

    ren->destroy();
    remove_pid();
    display_destroy(&d);
    printf("[swordwm-wallpaper] Stopped.\n");
    return 0;
}
