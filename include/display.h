#ifndef DISPLAY_H
#define DISPLAY_H

#include "ww.h"

int  display_init(WWDisplay *d);
void display_create_window(WWDisplay *d);
void display_set_desktop_hints(WWDisplay *d);
void display_create_surface(WWDisplay *d);
void display_destroy(WWDisplay *d);

#endif
