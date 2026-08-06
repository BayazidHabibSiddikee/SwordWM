#include "renderer.h"
#include "renderers/gradient.h"

const WWRenderer *renderer_find(const char *name) {
    const WWRenderer *list[] = {
        &renderer_gradient,
    };
    int n = sizeof(list) / sizeof(list[0]);
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i]->name, name) == 0)
            return list[i];
    }
    return &renderer_gradient;
}
