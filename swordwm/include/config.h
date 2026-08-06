#ifndef CONFIG_H
#define CONFIG_H

/* =========================================================
 * config.h — default configuration constants for SwordWM
 * Edit here to change defaults before compiling.
 * Runtime config file support comes in Step 11.
 * ========================================================= */

/* ── Modifier key ─────────────────────────────────────────── */
#define MOD_KEY   Mod4Mask   /* Super/Windows key */

/* ── Terminal ─────────────────────────────────────────────── */
#define TERMINAL  "ghostty"

/* ── Gaps (pixels) ────────────────────────────────────────── */
#define GAP_INNER   8    /* gap between tiled windows         */
#define GAP_OUTER   8    /* gap between windows and screen edge */

/* ── Borders ──────────────────────────────────────────────── */
#define BORDER_WIDTH      2

/* ── Title bar ────────────────────────────────────────────── */
#define TITLE_BAR_HEIGHT  24

/* ── Colors (X11 pixel values — use hex RGB) ─────────────── */
#define COLOR_FOCUSED_BORDER    "#5e81f4"   /* blue-purple accent */
#define COLOR_UNFOCUSED_BORDER  "#2a2d3e"   /* dark grey          */
#define COLOR_FOCUSED_TITLE_BG  "#1e2030"   /* dark navy          */
#define COLOR_FOCUSED_TITLE_FG  "#c0caf5"   /* light lavender     */
#define COLOR_UNFOCUSED_TITLE_BG "#16161e"  /* very dark          */
#define COLOR_UNFOCUSED_TITLE_FG "#565f89"  /* dimmed             */
#define COLOR_URGENT_BORDER     "#f7768e"   /* red/pink           */
#define COLOR_ROOT_BG           "#1a1b26"   /* root window bg     */

/* ── Number of workspaces ─────────────────────────────────── */
#define NUM_WORKSPACES  9

/* ── Default layout per workspace ────────────────────────── */
#define DEFAULT_LAYOUT  LAYOUT_TILE

/* ── Focus model ──────────────────────────────────────────── */
#define FOCUS_FOLLOWS_MOUSE  0   /* 1 = focus-follows-mouse */

/* ── Keybindings ──────────────────────────────────────────── */
/*
 * Defined as a table that is included in input.c.
 * Format: { modmask, keysym, action_fn, arg }
 */
#include <X11/keysym.h>

/* Forward declarations for action functions — all take const char * */
void action_spawn(const char *cmd);
void action_close_window(const char *arg);
void action_focus_next(const char *arg);
void action_focus_prev(const char *arg);
void action_toggle_floating(const char *arg);
void action_quit(const char *arg);
void action_switch_workspace(const char *id);
void action_move_to_workspace(const char *id);
void action_rotate_layout(const char *arg);
void action_gap_inc(const char *arg);
void action_gap_dec(const char *arg);
void action_reload_config(const char *arg);

#define KEYBINDINGS \
    /* launch terminal */                                                      \
    { MOD_KEY,                    XK_Return, action_spawn,           TERMINAL }, \
    /* close focused window */                                                 \
    { MOD_KEY,                    XK_q,      action_close_window,    NULL     }, \
    /* focus next/prev */                                                      \
    { MOD_KEY,                    XK_j,      action_focus_next,      NULL     }, \
    { MOD_KEY,                    XK_k,      action_focus_prev,      NULL     }, \
    /* toggle floating */                                                      \
    { MOD_KEY|ShiftMask,          XK_space,  action_toggle_floating, NULL     }, \
    /* rotate layout */                                                        \
    { MOD_KEY,                    XK_space,  action_rotate_layout,   NULL     }, \
    /* increase/decrease gaps */                                               \
    { MOD_KEY,                    XK_equal,  action_gap_inc,         NULL     }, \
    { MOD_KEY,                    XK_minus,  action_gap_dec,         NULL     }, \
    /* reload config */                                                        \
    { MOD_KEY|ShiftMask,          XK_r,      action_reload_config,   NULL     }, \
    /* quit WM */                                                              \
    { MOD_KEY|ShiftMask,          XK_e,      action_quit,            NULL     }, \
    /* workspace switch: Mod+1..9 */                                           \
    { MOD_KEY,                    XK_1,      action_switch_workspace, "0"     }, \
    { MOD_KEY,                    XK_2,      action_switch_workspace, "1"     }, \
    { MOD_KEY,                    XK_3,      action_switch_workspace, "2"     }, \
    { MOD_KEY,                    XK_4,      action_switch_workspace, "3"     }, \
    { MOD_KEY,                    XK_5,      action_switch_workspace, "4"     }, \
    { MOD_KEY,                    XK_6,      action_switch_workspace, "5"     }, \
    { MOD_KEY,                    XK_7,      action_switch_workspace, "6"     }, \
    { MOD_KEY,                    XK_8,      action_switch_workspace, "7"     }, \
    { MOD_KEY,                    XK_9,      action_switch_workspace, "8"     }, \
    /* move window to workspace: Mod+Shift+1..9 */                            \
    { MOD_KEY|ShiftMask,          XK_1,      action_move_to_workspace, "0"   }, \
    { MOD_KEY|ShiftMask,          XK_2,      action_move_to_workspace, "1"   }, \
    { MOD_KEY|ShiftMask,          XK_3,      action_move_to_workspace, "2"   }, \
    { MOD_KEY|ShiftMask,          XK_4,      action_move_to_workspace, "3"   }, \
    { MOD_KEY|ShiftMask,          XK_5,      action_move_to_workspace, "4"   }, \
    { MOD_KEY|ShiftMask,          XK_6,      action_move_to_workspace, "5"   }, \
    { MOD_KEY|ShiftMask,          XK_7,      action_move_to_workspace, "6"   }, \
    { MOD_KEY|ShiftMask,          XK_8,      action_move_to_workspace, "7"   }, \
    { MOD_KEY|ShiftMask,          XK_9,      action_move_to_workspace, "8"   },

#endif /* CONFIG_H */
