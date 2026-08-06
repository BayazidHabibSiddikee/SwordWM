#include "renderer.h"
#include <math.h>

static double g_hue = 0.0;

static void gradient_init(int w, int h) {
    (void)w; (void)h;
    g_hue = 0.0;
}

static void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b) {
    int i = (int)(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);
    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

static void gradient_render(cairo_t *cr, int w, int h, double t) {
    g_hue = fmod(g_hue + 0.001, 1.0);

    double r, g, b;
    hsv_to_rgb(g_hue, 0.4, 0.3, &r, &g, &b);
    cairo_pattern_t *bg = cairo_pattern_create_radial(w * 0.5, h * 0.5, 0,
        w * 0.5, h * 0.5, fmax(w, h) * 0.7);
    cairo_pattern_add_color_stop_rgba(bg, 0, r, g, b, 1.0);

    double r2, g2, b2;
    hsv_to_rgb(fmod(g_hue + 0.39, 1.0), 0.5, 0.18, &r2, &g2, &b2);
    cairo_pattern_add_color_stop_rgba(bg, 1, r2, g2, b2, 1.0);

    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    for (int i = 0; i < 6; i++) {
        double ox = w * (0.15 + 0.7 * sin(t * 0.12 + i * 1.1));
        double oy = h * (0.2 + 0.6 * cos(t * 0.09 + i * 0.8));
        double radius = 100 + 80 * sin(t * 0.2 + i);

        double or_, og, ob;
        hsv_to_rgb(fmod(g_hue + i * 0.12, 1.0), 0.3, 0.14, &or_, &og, &ob);
        cairo_pattern_t *orb = cairo_pattern_create_radial(ox, oy, 0, ox, oy, fmax(radius, 1));
        cairo_pattern_add_color_stop_rgba(orb, 0, or_, og, ob, 0.35);
        cairo_pattern_add_color_stop_rgba(orb, 1, 0, 0, 0, 0);
        cairo_set_source(cr, orb);
        cairo_paint(cr);
        cairo_pattern_destroy(orb);
    }
}

static void gradient_destroy(void) {}

const WWRenderer renderer_gradient = {
    .name    = "gradient",
    .init    = gradient_init,
    .render  = gradient_render,
    .destroy = gradient_destroy,
};
