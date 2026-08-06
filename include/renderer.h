#ifndef RENDERER_H
#define RENDERER_H

#include "ww.h"

extern const WWRenderer renderer_gradient;
extern const WWRenderer renderer_particles;
extern const WWRenderer renderer_wave;
extern const WWRenderer renderer_matrix;
extern const WWRenderer renderer_graph;

const WWRenderer *renderer_find(const char *name);

#endif
