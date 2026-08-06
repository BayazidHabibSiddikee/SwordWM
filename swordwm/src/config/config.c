/* =========================================================
 * src/config/config.c — Runtime config file parser
 *
 * Reads ~/.config/swordwm/config, parses key=value and
 * bind lines, populates the global SwordConfig struct.
 * ========================================================= */

#include "swordwm.h"
#include "config_parser.h"
#include <ctype.h>
#include <sys/stat.h>

/* ── Global config instance ──────────────────────────────── */
SwordConfig cfg;

/* ── Path to config file ─────────────────────────────────── */
static const char *config_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home) home = "/root";
    snprintf(path, sizeof(path), "%s/.config/swordwm/config", home);
    return path;
}

/* ── Apply compile-time defaults ─────────────────────────── */
static void set_defaults(void) {
    strncpy(cfg.terminal,              TERMINAL,                    sizeof(cfg.terminal)-1);
    cfg.mod                  = MOD_KEY;
    cfg.gap_inner            = GAP_INNER;
    cfg.gap_outer            = GAP_OUTER;
    cfg.border_width         = BORDER_WIDTH;
    cfg.title_bar_height     = TITLE_BAR_HEIGHT;
    cfg.focus_follows_mouse  = FOCUS_FOLLOWS_MOUSE;

    strncpy(cfg.color_focused_border,     COLOR_FOCUSED_BORDER,     CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_border,   COLOR_UNFOCUSED_BORDER,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_focused_title_bg,   COLOR_FOCUSED_TITLE_BG,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_focused_title_fg,   COLOR_FOCUSED_TITLE_FG,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_title_bg, COLOR_UNFOCUSED_TITLE_BG, CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_title_fg, COLOR_UNFOCUSED_TITLE_FG, CONF_COLOR_LEN-1);
    strncpy(cfg.color_urgent_border,      COLOR_URGENT_BORDER,      CONF_COLOR_LEN-1);

    cfg.n_binds     = 0;
    cfg.n_autostart = 0;
}

/* ── String helpers ──────────────────────────────────────── */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* ── Parse modifier string → X11 bitmask ────────────────── */
static unsigned int parse_mod(const char *s) {
    if (streq(s, "super") || streq(s, "mod4")) return Mod4Mask;
    if (streq(s, "alt")   || streq(s, "mod1")) return Mod1Mask;
    if (streq(s, "ctrl"))                       return ControlMask;
    if (streq(s, "shift"))                      return ShiftMask;
    return Mod4Mask; /* default */
}

/* ── Parse a bind line into a KeyBinding ─────────────────── */
/*
 * Bind syntax:
 *   bind Mod+Return     = spawn ghostty
 *   bind Mod+Shift+q    = close
 *   bind Mod+j          = focus_next
 *   bind Mod+k          = focus_prev
 *   bind Mod+Shift+e    = quit
 *   bind Mod+space      = rotate_layout
 *   bind Mod+Shift+space = toggle_floating
 *   bind Mod+equal      = gap_inc
 *   bind Mod+minus      = gap_dec
 *   bind Mod+1          = workspace 0
 *   bind Mod+Shift+1    = move_workspace 0
 */
static void parse_bind(const char *keys, const char *action) {
    if (cfg.n_binds >= CONF_MAX_BINDS) return;

    unsigned int mod = 0;
    char keybuf[64];
    strncpy(keybuf, keys, sizeof(keybuf)-1);
    keybuf[sizeof(keybuf)-1] = '\0';

    /* Split on '+' — last token is the key, others are modifiers */
    char *tok;
    char *last = NULL;
    char *p = keybuf;
    /* walk tokens */
    char parts[8][32];
    int nparts = 0;
    tok = strtok(p, "+");
    while (tok && nparts < 8) {
        strncpy(parts[nparts++], tok, 31);
        tok = strtok(NULL, "+");
    }
    if (nparts == 0) return;

    /* Last part is the key symbol name */
    last = parts[nparts - 1];

    /* All but last are modifier names — map "Mod" to cfg.mod */
    for (int i = 0; i < nparts - 1; i++) {
        char *m = parts[i];
        /* lowercase for comparison */
        for (char *c = m; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (streq(m, "mod"))   mod |= cfg.mod;
        else                   mod |= parse_mod(m);
    }

    /* Map key name to KeySym */
    KeySym sym = XStringToKeysym(last);
    if (sym == NoSymbol) {
        fprintf(stderr, "swordwm: config: unknown key '%s'\n", last);
        return;
    }

    /* Map action string to function + arg */
    void (*fn)(const char *) = NULL;
    const char *arg = NULL;
    static char arg_storage[CONF_MAX_BINDS][256];

    char act[128];
    strncpy(act, action, sizeof(act)-1);
    act[sizeof(act)-1] = '\0';
    char *act_arg = strchr(act, ' ');
    if (act_arg) { *act_arg = '\0'; act_arg = trim(act_arg + 1); }

    if (streq(act, "spawn")) {
        fn = action_spawn;
        /* Use act_arg as command; fall back to terminal */
        snprintf(arg_storage[cfg.n_binds], 256, "%s",
                 act_arg ? act_arg : cfg.terminal);
        arg = arg_storage[cfg.n_binds];
    } else if (streq(act, "close"))           { fn = action_close_window; }
    else if (streq(act, "focus_next"))        { fn = action_focus_next; }
    else if (streq(act, "focus_prev"))        { fn = action_focus_prev; }
    else if (streq(act, "toggle_floating"))   { fn = action_toggle_floating; }
    else if (streq(act, "rotate_layout"))     { fn = action_rotate_layout; }
    else if (streq(act, "gap_inc"))           { fn = action_gap_inc; }
    else if (streq(act, "gap_dec"))           { fn = action_gap_dec; }
    else if (streq(act, "quit"))              { fn = action_quit; }
    else if (streq(act, "workspace")) {
        fn = action_switch_workspace;
        if (act_arg) {
            /* Convert "1"-"9" (human) to 0-based index */
            int n = atoi(act_arg);
            if (n >= 1) n--;
            snprintf(arg_storage[cfg.n_binds], 63, "%d", n);
            arg = arg_storage[cfg.n_binds];
        }
    } else if (streq(act, "move_workspace")) {
        fn = action_move_to_workspace;
        if (act_arg) {
            int n = atoi(act_arg);
            if (n >= 1) n--;
            snprintf(arg_storage[cfg.n_binds], 63, "%d", n);
            arg = arg_storage[cfg.n_binds];
        }
    } else {
        fprintf(stderr, "swordwm: config: unknown action '%s'\n", act);
        return;
    }

    KeyBinding *kb = &cfg.binds[cfg.n_binds++];
    kb->modmask = mod;
    kb->keysym  = sym;
    kb->action  = fn;
    kb->arg     = arg;
}

/* ── Parse a single key=value line ──────────────────────── */
static void parse_line(char *line) {
    /* Strip comment */
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';

    char *eq = strchr(line, '=');
    if (!eq) return;

    *eq = '\0';
    char *key = trim(line);
    char *val = trim(eq + 1);

    if (!*key || !*val) return;

    /* bind line */
    if (strncmp(key, "bind ", 5) == 0) {
        parse_bind(trim(key + 5), val);
        return;
    }

    /* autostart */
    if (streq(key, "autostart")) {
        if (cfg.n_autostart < CONF_MAX_AUTO) {
            strncpy(cfg.autostart[cfg.n_autostart++], val,
                    sizeof(cfg.autostart[0])-1);
        }
        return;
    }

    /* Scalar settings */
    if (streq(key, "terminal"))
        strncpy(cfg.terminal, val, sizeof(cfg.terminal)-1);
    else if (streq(key, "mod"))
        cfg.mod = parse_mod(val);
    else if (streq(key, "gap_inner"))
        cfg.gap_inner = atoi(val);
    else if (streq(key, "gap_outer"))
        cfg.gap_outer = atoi(val);
    else if (streq(key, "border_width"))
        cfg.border_width = atoi(val);
    else if (streq(key, "title_bar_height"))
        cfg.title_bar_height = atoi(val);
    else if (streq(key, "focus_follows_mouse"))
        cfg.focus_follows_mouse = (streq(val,"true") || streq(val,"1"));
    else if (streq(key, "color_focused_border"))
        strncpy(cfg.color_focused_border,     val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_unfocused_border"))
        strncpy(cfg.color_unfocused_border,   val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_focused_title_bg"))
        strncpy(cfg.color_focused_title_bg,   val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_focused_title_fg"))
        strncpy(cfg.color_focused_title_fg,   val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_unfocused_title_bg"))
        strncpy(cfg.color_unfocused_title_bg, val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_unfocused_title_fg"))
        strncpy(cfg.color_unfocused_title_fg, val, CONF_COLOR_LEN-1);
    else if (streq(key, "color_urgent_border"))
        strncpy(cfg.color_urgent_border,      val, CONF_COLOR_LEN-1);
    else
        fprintf(stderr, "swordwm: config: unknown key '%s'\n", key);
}

/* ── config_load ─────────────────────────────────────────── */
void config_load(void) {
    set_defaults();

    const char *path = config_path();
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "swordwm: no config at %s — using defaults\n", path);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';
        parse_line(line);
    }
    fclose(f);

    fprintf(stderr, "swordwm: loaded config from %s (%d binds, %d autostart)\n",
            path, cfg.n_binds, cfg.n_autostart);
}

/* ── config_reload ───────────────────────────────────────── */
void config_reload(void) {
    fprintf(stderr, "swordwm: reloading config\n");
    config_load();

    /* Re-grab all keys with new bindings */
    x11_ungrab_keys();
    x11_grab_keys();

    /* Re-apply visual settings to all existing clients */
    config_apply();
}

/* ── config_apply ────────────────────────────────────────── */
void config_apply(void) {
    /* Update gaps on all workspaces */
    for (Workspace *ws = wm->workspaces; ws; ws = ws->next) {
        ws->gap = cfg.gap_inner;
        /* Redraw all client title bars with new colours */
        for (Client *c = ws->head; c; c = c->next)
            decorate_draw(c);
    }

    /* Re-arrange current workspace (gap changes take effect) */
    if (wm->current_ws)
        arrange_workspace(wm->current_ws);

    /* Update EWMH workarea (gap_outer may have changed) */
    ewmh_update_workarea();
}

/* ── config_run_autostart ────────────────────────────────── */
void config_run_autostart(void) {
    for (int i = 0; i < cfg.n_autostart; i++) {
        fprintf(stderr, "swordwm: autostart: %s\n", cfg.autostart[i]);
        if (fork() == 0) {
            setsid();
            execlp("/bin/sh", "sh", "-c", cfg.autostart[i], NULL);
            exit(1);
        }
    }
}
