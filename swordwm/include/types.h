#ifndef TYPES_H
#define TYPES_H

/* =========================================================
 * types.h — core data structures for SwordWM
 * ========================================================= */

#include <X11/Xlib.h>

/* ── Layout modes ─────────────────────────────────────────── */
typedef enum {
    LAYOUT_TILE    = 0,   /* master/stack tiling          */
    LAYOUT_MONOCLE = 1,   /* fullscreen/monocle           */
    LAYOUT_FLOAT   = 2,   /* free floating, no auto-tile  */
    LAYOUT_COUNT   = 3
} Layout;

/* ── Client — one managed window ─────────────────────────── */
typedef struct Client Client;
struct Client {
    Window       win;       /* the actual client X window   */
    Window       frame;     /* parent frame we created      */
    int          x, y;      /* frame position               */
    int          w, h;      /* frame size (incl. title bar) */
    int          old_x, old_y, old_w, old_h; /* saved for unfloat */
    int          floating;  /* 1 = floating, 0 = tiled      */
    int          focused;   /* 1 = has input focus          */
    int          urgent;    /* 1 = demands attention        */
    int          fullscreen;/* 1 = fullscreen               */
    char         title[256];/* window title (WM_NAME)       */
    struct Workspace *ws;   /* owning workspace             */
    Client      *next;      /* linked list → next           */
    Client      *prev;      /* linked list → prev           */
};

/* ── Workspace — virtual desktop ─────────────────────────── */
typedef struct Workspace Workspace;
struct Workspace {
    int      id;            /* 0-based workspace index      */
    char     name[32];      /* display name e.g. "1"        */
    Client  *head;          /* first client in list         */
    Client  *focused;       /* currently focused client     */
    Layout   layout;        /* current layout mode          */
    int      gap;           /* inner gap between windows    */
    Workspace *next;        /* linked list of workspaces    */
};

/* ── KeyBinding — keyboard shortcut → action ─────────────── */
typedef struct {
    unsigned int  modmask;               /* e.g. Mod4Mask              */
    KeySym        keysym;                /* e.g. XK_Return             */
    void        (*action)(const char *); /* handler function           */
    const char   *arg;                   /* argument passed to action  */
} KeyBinding;

/* ── Global WM state ─────────────────────────────────────── */
typedef struct {
    Display    *dpy;
    int         screen;
    Window      root;
    int         sw, sh;          /* screen width/height              */
    int         running;         /* event loop flag                  */
    Workspace  *workspaces;      /* head of workspace list           */
    Workspace  *current_ws;      /* active workspace                 */
    Client     *focused;         /* currently focused client         */
    int         num_workspaces;  /* total workspace count            */

    /* EWMH atoms (populated in x11_connect) */
    Atom        wm_protocols;
    Atom        wm_delete_window;
    Atom        wm_state;
    Atom        net_supported;
    Atom        net_wm_name;
    Atom        net_active_window;
    Atom        net_client_list;
    Atom        net_current_desktop;
    Atom        net_wm_desktop;
    Atom        net_wm_state;
    Atom        net_wm_state_fullscreen;
    Atom        net_wm_window_type;
    Atom        net_wm_window_type_dialog;
    Atom        net_wm_window_type_splash;
    Atom        net_wm_window_type_utility;
    Atom        net_wm_window_type_desktop;
    Atom        net_wm_window_type_dock;
} WMState;

#endif /* TYPES_H */
