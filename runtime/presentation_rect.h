#ifndef NOVA_PRESENTATION_RECT_H
#define NOVA_PRESENTATION_RECT_H
#include <stdint.h>

struct presentation_rect { int x, y, w, h; };

/* Bottom-left GLES viewport. Android input converts its top edge to top-left. */
static inline struct presentation_rect fit_layer(int width, int height, int layer_w, int layer_h) {
    int w = width, h = height;
    if ((int64_t)width * layer_h > (int64_t)height * layer_w)
        w = (int)((int64_t)height * layer_w / layer_h);
    else
        h = (int)((int64_t)width * layer_h / layer_w);
    return (struct presentation_rect){(width - w) / 2, (height - h) / 2, w, h};
}
#endif
