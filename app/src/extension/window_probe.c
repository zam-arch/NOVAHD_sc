#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdio.h>

static int same_module(void *handle, const Dl_info *engine, const char *name) {
    Dl_info owner;
    return dladdr(dlsym(handle, name), &owner) && owner.dli_fbase == engine->dli_fbase;
}

JNIEXPORT jstring JNICALL
Java_com_novahd_recomp_NativeWindowProbe_sample(JNIEnv *env, jclass type, jobject surface) {
    (void)type;
    char dimensions[64] = "sin superficie";
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if ((*env)->ExceptionCheck(env)) return NULL;
    if (window) {
        int width = ANativeWindow_getWidth(window);
        int height = ANativeWindow_getHeight(window);
        if (width > 0 && height > 0)
            snprintf(dimensions, sizeof dimensions, "%d x %d px", width, height);
        ANativeWindow_release(window);
    }
    char message[220];
    void *engine = dlopen("libnovahd.so", RTLD_NOW | RTLD_NOLOAD);
    Dl_info owner;
    if (engine && dladdr(dlsym(engine, "ANativeActivity_onCreate"), &owner)) {
        snprintf(message, sizeof message,
                 "Ventana nativa: %s\nExportaciones visibles: swap %s / registro musical %s",
                 dimensions, same_module(engine, &owner, "rt_platform_swap") ? "si" : "no",
                 same_module(engine, &owner, "rt_objc_method") &&
                 same_module(engine, &owner, "rt_objc_cmethod") &&
                 same_module(engine, &owner, "rt_class") ? "si" : "no");
    } else {
        snprintf(message, sizeof message, "Ventana nativa: %s\nMotor fuera del alcance de la sonda", dimensions);
    }
    if (engine) dlclose(engine);
    return (*env)->NewStringUTF(env, message);
}
