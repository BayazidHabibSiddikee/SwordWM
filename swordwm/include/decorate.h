#ifndef DECORATE_H
#define DECORATE_H

/* =========================================================
 * include/decorate.h — title bar drawing & mouse drag API
 * ========================================================= */

#include "types.h"
#include <X11/Xlib.h>

/* Draw the title bar on a client's frame window.
 * Called on Expose events and whenever focus/title changes. */
void decorate_draw(Client *c);

/* Called on ButtonPress on a frame — starts drag-move or
 * drag-resize, or fires close button.
 * Returns 1 if the event was consumed (no further processing). */
int  decorate_button_press(Client *c, XButtonEvent *e);

/* Called on MotionNotify during a drag — moves or resizes the window. */
void decorate_motion(Client *c, XMotionEvent *e);

/* Called on ButtonRelease — ends any active drag. */
void decorate_button_release(void);

/* Update title bar after window title changes. */
void decorate_update_title(Client *c);

/* Initialize shared GC / font resources once after X connect. */
void decorate_init(void);

/* Free GC / font resources on WM exit. */
void decorate_cleanup(void);

#endif /* DECORATE_H */
