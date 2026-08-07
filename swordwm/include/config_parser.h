#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

/* =========================================================
 * include/config_parser.h — runtime config file API
 *
 * Config file location: ~/.config/swordwm/config
 *
 * Format (simple key = value, # comments, bind lines):
 *
 *   # comment
 *   terminal         = ghostty
 *   mod              = super           # super | alt
 *   gap_inner        = 8
 *   gap_outer        = 8
 *   border_width     = 2
 *   focus_follows_mouse = false
 *   color_focused_border    = #5e81f4
 *   color_unfocused_border  = #2a2d3e
 *   color_focused_title_bg  = #1e2030
 *   color_focused_title_fg  = #c0caf5
 *   color_unfocused_title_bg = #16161e
 *   color_unfocused_title_fg = #565f89
 *   bind Mod+Return  = spawn ghostty
 *   bind Mod+q       = close
 *   autostart        = ~/bin/myapp
 *
 * ========================================================= */

#include <X11/Xlib.h>
#include "types.h"

/* Max runtime keybindings from config (on top of built-ins) */
#define CONF_MAX_BINDS   128
#define CONF_MAX_AUTO    32
#define CONF_COLOR_LEN   16

/* Runtime configuration structure — loaded from file, can be
 * reloaded on SIGHUP without restarting the WM. */
typedef struct {
    /* Terminal emulator */
    char terminal[256];

    /* Modifier key bitmask (Mod4Mask=super, Mod1Mask=alt) */
    unsigned int mod;

    /* Layout settings */
    int gap_inner;
    int gap_outer;
    int border_width;
    int title_bar_height;
    int focus_follows_mouse;
    int master_ratio;        /* master area width % (10–90), default 50 */

    /* Colours (hex strings like "#5e81f4") */
    char color_focused_border    [CONF_COLOR_LEN];
    char color_unfocused_border  [CONF_COLOR_LEN];
    char color_focused_title_bg  [CONF_COLOR_LEN];
    char color_focused_title_fg  [CONF_COLOR_LEN];
    char color_unfocused_title_bg[CONF_COLOR_LEN];
    char color_unfocused_title_fg[CONF_COLOR_LEN];
    char color_urgent_border     [CONF_COLOR_LEN];

    /* Runtime keybindings from bind lines */
    KeyBinding binds[CONF_MAX_BINDS];
    int        n_binds;

    /* Autostart commands */
    char autostart[CONF_MAX_AUTO][256];
    int  n_autostart;
} SwordConfig;

/* Global config — extern so all modules can read it */
extern SwordConfig cfg;

/* Load config from ~/.config/swordwm/config.
 * Falls back to built-in defaults if file is missing. */
void config_load(void);

/* Reload config (called on SIGHUP or Mod+Shift+r).
 * Re-grabs keys and re-applies colours/gaps to all clients. */
void config_reload(void);

/* Apply loaded config values to all current clients
 * (gaps, colours, title bar height). */
void config_apply(void);

/* Run autostart commands (called once at WM start). */
void config_run_autostart(void);

#endif /* CONFIG_PARSER_H */
