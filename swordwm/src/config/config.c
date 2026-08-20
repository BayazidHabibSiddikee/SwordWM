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
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    
    if (xdg_config && xdg_config[0]) {
        snprintf(path, sizeof(path), "%s/swordwm/config", xdg_config);
    } else if (home) {
        snprintf(path, sizeof(path), "%s/.config/swordwm/config", home);
    } else {
        snprintf(path, sizeof(path), "/root/.config/swordwm/config");
    }
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
    cfg.master_ratio         = 50;
    cfg.focus_follows_mouse  = FOCUS_FOLLOWS_MOUSE;
    strncpy(cfg.font,                  DEFAULT_FONT,                sizeof(cfg.font)-1);

    strncpy(cfg.color_focused_border,     COLOR_FOCUSED_BORDER,     CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_border,   COLOR_UNFOCUSED_BORDER,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_focused_title_bg,   COLOR_FOCUSED_TITLE_BG,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_focused_title_fg,   COLOR_FOCUSED_TITLE_FG,   CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_title_bg, COLOR_UNFOCUSED_TITLE_BG, CONF_COLOR_LEN-1);
    strncpy(cfg.color_unfocused_title_fg, COLOR_UNFOCUSED_TITLE_FG, CONF_COLOR_LEN-1);
    strncpy(cfg.color_urgent_border,      COLOR_URGENT_BORDER,      CONF_COLOR_LEN-1);

    /* Physics defaults */
    cfg.physics_enabled     = 1;
    cfg.physics_gravity     = 981.0;
    cfg.physics_friction    = 0.985;
    cfg.physics_restitution = 0.3;
    cfg.physics_mass_density= 0.0005;
    cfg.physics_throw_mult  = 0.65;
    cfg.physics_max_throw   = 1800.0;

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
    } else if (streq(act, "close"))           { fn = action_close_focused; }
    else if (streq(act, "focus_next"))        { fn = action_focus_next; }
    else if (streq(act, "focus_prev"))        { fn = action_focus_prev; }
    else if (streq(act, "toggle_floating"))   { fn = action_toggle_floating; }
    else if (streq(act, "rotate_layout"))     { fn = action_rotate_layout; }
    else if (streq(act, "gap_inc"))           { fn = action_gap_inc; }
    else if (streq(act, "gap_dec"))           { fn = action_gap_dec; }
    else if (streq(act, "quit"))              { fn = action_quit; }
    else if (streq(act, "reload_config"))     { fn = action_reload_config; }
        else if (streq(act, "move_stack_up"))     { fn = action_move_stack_up; }
        else if (streq(act, "move_stack_down"))   { fn = action_move_stack_down; }
        else if (streq(act, "master_grow"))       { fn = action_master_grow; }
        else if (streq(act, "master_shrink"))     { fn = action_master_shrink; }
        else if (streq(act, "minimize"))          { fn = action_minimize; }
        else if (streq(act, "vol_up"))            { fn = action_vol_up; }
        else if (streq(act, "vol_down"))          { fn = action_vol_down; }
        else if (streq(act, "mute"))              { fn = action_mute; }
        else if (streq(act, "bright_up"))         { fn = action_bright_up; }
        else if (streq(act, "bright_down"))       { fn = action_bright_down; }
    else if (streq(act, "workspace_prev"))     { fn = action_workspace_prev; }
    else if (streq(act, "workspace_next"))     { fn = action_workspace_next; }
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
    /* Strip inline comment — carefully so color hex values like #5e81f4
     * are not truncated.
     *
     * Rules:
     *  1. Any '#' on the KEY side (before '=') terminates the line — pure comment.
     *  2. On the VALUE side: if the value STARTS with '#' (hex color), only
     *     strip a '#' that is preceded by whitespace (a trailing comment).
     *     e.g.  "color = #5e81f4  # nice blue"  → value = "#5e81f4"
     *           "color = #5e81f4"                → value = "#5e81f4"
     *  3. If the value does NOT start with '#', strip the first '#' found
     *     (inline comment).
     *     e.g.  "gap_inner = 8  # pixels"  → value = "8"
     */
    char *eq = strchr(line, '=');
    if (eq) {
        /* Step 1: strip any '#' comment in the key portion */
        for (char *p = line; p < eq; p++) {
            if (*p == '#') { *p = '\0'; break; }
        }

        /* Step 2: strip trailing comment from value */
        char *val_start = eq + 1;
        while (*val_start && isspace((unsigned char)*val_start)) val_start++;

        if (*val_start == '#') {
            /* Hex color value: skip the leading '#' + 6 hex digits (7 chars total),
             * then look for a whitespace-preceded '#' which is a trailing comment. */
            char *p = val_start + 1; /* skip the '#' of the color */
            while (*p && !isspace((unsigned char)*p)) p++; /* skip hex digits */
            /* p now points at whitespace or end-of-string */
            while (*p) {
                if (*p == '#') { *p = '\0'; break; } /* trailing comment */
                p++;
            }
        } else {
            /* Non-color value: strip from first '#' */
            char *vhash = strchr(val_start, '#');
            if (vhash) *vhash = '\0';
        }
    } else {
        /* No '=': whole line is a comment if it starts with '#', or unknown */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
    }

    eq = strchr(line, '=');
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
    else if (streq(key, "font"))
        strncpy(cfg.font, val, sizeof(cfg.font)-1);
    else if (streq(key, "mod"))
        cfg.mod = parse_mod(val);
    else if (streq(key, "gap_inner")) {
        int v = atoi(val);
        if (v >= MIN_GAP && v <= MAX_GAP) cfg.gap_inner = v;
        else fprintf(stderr, "swordwm: config: gap_inner %d out of range [%d,%d], ignored\n", v, MIN_GAP, MAX_GAP);
    }
    else if (streq(key, "gap_outer")) {
        int v = atoi(val);
        if (v >= MIN_GAP && v <= MAX_GAP) cfg.gap_outer = v;
        else fprintf(stderr, "swordwm: config: gap_outer %d out of range [%d,%d], ignored\n", v, MIN_GAP, MAX_GAP);
    }
    else if (streq(key, "border_width")) {
        int v = atoi(val);
        if (v >= MIN_BORDER && v <= MAX_BORDER) cfg.border_width = v;
        else fprintf(stderr, "swordwm: config: border_width %d out of range [%d,%d], ignored\n", v, MIN_BORDER, MAX_BORDER);
    }
    else if (streq(key, "title_bar_height")) {
        int v = atoi(val);
        if (v >= MIN_TITLE_BAR && v <= MAX_TITLE_BAR) cfg.title_bar_height = v;
        else fprintf(stderr, "swordwm: config: title_bar_height %d out of range [%d,%d], ignored\n", v, MIN_TITLE_BAR, MAX_TITLE_BAR);
    }
    else if (streq(key, "master_ratio")) {
        int r = atoi(val);
        if (r >= MIN_MASTER_RATIO && r <= MAX_MASTER_RATIO) cfg.master_ratio = r;
        else fprintf(stderr, "swordwm: config: master_ratio %d out of range [%d,%d], ignored\n", r, MIN_MASTER_RATIO, MAX_MASTER_RATIO);
    }
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
    /* Physics settings */
    else if (streq(key, "physics_enabled"))
        cfg.physics_enabled = (streq(val,"true") || streq(val,"1"));
    else if (streq(key, "physics_gravity"))
        cfg.physics_gravity = atof(val);
    else if (streq(key, "physics_friction")) {
        double v = atof(val);
        if (v >= 0.0 && v <= 1.0) cfg.physics_friction = v;
        else fprintf(stderr, "swordwm: config: physics_friction %.3f out of range [0,1], ignored\n", v);
    }
    else if (streq(key, "physics_restitution")) {
        double v = atof(val);
        if (v >= 0.0 && v <= 1.0) cfg.physics_restitution = v;
        else fprintf(stderr, "swordwm: config: physics_restitution %.3f out of range [0,1], ignored\n", v);
    }
    else if (streq(key, "physics_mass_density"))
        cfg.physics_mass_density = atof(val);
    else if (streq(key, "physics_throw_mult"))
        cfg.physics_throw_mult = atof(val);
    else if (streq(key, "physics_max_throw"))
        cfg.physics_max_throw = atof(val);
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
        ws->master_ratio = cfg.master_ratio;
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
        const char *cmd = cfg.autostart[i];
        if (!cmd[0]) continue;
        fprintf(stderr, "swordwm: autostart: %s\n", cmd);
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            /* execv with explicit argv — same rationale as action_spawn */
            char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
            execv("/bin/sh", argv);
            _exit(1);
        }
        /* parent: pid > 0 → child reaped by SIGCHLD handler */
    }
}
