#include <jni.h>

typedef struct { int width, height; } ANativeWindow;

static inline ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface) {
    (void)env;
    static ANativeWindow fixture = {1600, 768};
    return surface ? &fixture : NULL;
}
static inline int ANativeWindow_getWidth(ANativeWindow *w) { return w->width; }
static inline int ANativeWindow_getHeight(ANativeWindow *w) { return w->height; }
static inline void ANativeWindow_release(ANativeWindow *w) { (void)w; }
