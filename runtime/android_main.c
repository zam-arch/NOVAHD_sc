/* Android front end: a NativeActivity (no Java code of our own) that boots the recompiled N.O.V.A. HD executable and
 * runs the display loop UIApplicationMain hands over to (rt_platform_run). The Android counterpart of host_main.c.
 *
 * Files. image.bin (tools/recomp/pack_image.py) is an APK asset, read into game memory with rt_mem_init_from. The
 * game bundle (the contents of the NovaHD.app folder of the IPA) is not in the APK: it is pushed by adb to the app's
 * external files folder, which needs no storage permission:
 *     adb push NovaHD.app /sdcard/Android/data/com.novahd.recomp/files/
 * so that <files>/NovaHD.app/Info.plist exists. The home folder (Documents, Library, Library/Caches, tmp, created
 * here as the iOS installer would) is <files>/home. As on Windows the game sees /var/mobile/Applications/NOVAHD and
 * .../NovaHD.app; rt_path_alias maps both to those folders. <files> is the activity's externalDataPath, or its
 * internalDataPath when there is no external storage.
 *
 * Boot order (boot() below, the same as host_main.c): rt_mem_init_from; the host classes (rt_objc_define_root,
 * Foundation, collections, system, UIKit, graphics, offline); rt_gl_getproc; rt_imports_init; rt_data_imports_bind;
 * rt_objc_init; +load; the static constructors (__mod_init_func); the game's main(1, {argv0, NULL}). main() calls
 * UIApplicationMain, which launches the app delegate and then calls rt_platform_run, which never returns. The boot
 * waits for the first window, so the EGL context is current on this thread (the game's main thread: android_main
 * runs on the native_app_glue thread, not on the UI thread) before any game code runs.
 *   While the game launches (between main() and rt_platform_run) activity events are processed only when the game
 *   presents a frame (rt_platform_swap on the main thread drains them); a pause during the launch reaches UIKit when
 *   the loop starts. A launch that never presents leaves the glue's UI thread waiting for us (possible ANR).
 *
 * Display. OpenGL ES 1 context (EGL_CONTEXT_CLIENT_VERSION 1, EGL_OPENGL_ES_BIT; RGBA8 or RGB888 with depth 24/16,
 * stencil when available, RGB565 as the last resort). The window buffers are 1024x768 (the iPad screen in landscape,
 * ANativeWindow_setBuffersGeometry) and the compositor scales them to the whole window: the game always sees a
 * 1024x768 drawable (rt_platform_size), stretched (not letterboxed) on screens that are not 4:3. The manifest forces
 * sensorLandscape. Swap interval 1; the loop paces rt_uikit_frame to 60 Hz itself (ALooper_pollOnce timeouts), so a
 * 90/120 Hz display does not speed the game up.
 *
 * Lifecycle. APP_CMD_PAUSE -> rt_uikit_pause, APP_CMD_RESUME -> rt_uikit_resume (each once per change). The EGL
 * context lives for the whole process (the game's textures and buffers stay valid); APP_CMD_TERM_WINDOW destroys only
 * the window surface and keeps the context current on this thread without a window (EGL_KHR_surfaceless_context,
 * else a 1x1 pbuffer, else the context is released), so GL objects the game creates meanwhile are real ones;
 * APP_CMD_INIT_WINDOW creates a new surface on the new window and makes the same context current with it. A lost
 * Context ownership: the main thread has the context during the launch and releases it when rt_platform_run starts;
 * the game's render thread takes it in rt_platform_make_current and keeps it. After that the main thread only creates and
 * destroys surfaces: it posts the wanted surface (want_surf/want_gen) and, before destroying one, waits (2 s at most)
 * until the render thread has rebound at its next swap (render_sync).
 * A lost
 * context (EGL_CONTEXT_LOST) cannot be rebuilt (the game's textures are gone): the process exits. Frames run only while there is a surface and the activity is
 * resumed. Destroying the activity (Back, or the system) exits the process: the game keeps global state, so the next
 * start is a fresh boot.
 *
 * Touches. AInputQueue motion events, per pointer: each Android pointer id gets the lowest free finger slot 0-4
 * (a sixth simultaneous finger is ignored until a slot frees up); window pixels are scaled to the 1024x768 points
 * (the window's own pixel size is measured before the buffer geometry is set, and again on resize/config changes).
 * DOWN/POINTER_DOWN -> began, MOVE -> moved for every pointer, UP/POINTER_UP -> ended, CANCEL -> cancelled for all.
 *
 * Accelerometer. ASensorManager's accelerometer at 60 Hz (16667 us) on this thread's ALooper (LOOPER_ID_USER);
 * the latest sample of each drain goes to rt_uikit_accelerometer, in iOS g units and iOS axes. Android reports
 * m/s^2 in the NATURAL device frame (x right, y up, z out of the screen; resting face up gives z = +9.81, i.e. the
 * reaction to gravity); iOS reports g in the PORTRAIT device frame with the opposite sign (face up gives z = -1).
 * The mapping goes through the frame of the screen as displayed (X right, Y up), using the display rotation r
 * (Display.getRotation through JNI, polled about twice a second since a 180 degree flip between the two landscapes
 * does not produce a configuration change; ROTATION_90 is assumed when JNI fails):
 *     r = 0: X =  ax, Y =  ay      r = 90: X = -ay, Y =  ax      r = 180: X = -ax, Y = -ay      r = 270: X = ay, Y = -ax
 * UIKit (uikit.c) reports UIInterfaceOrientationLandscapeRight (home button on the right): the iPad's portrait top
 * points left, so its +x axis is the screen's up (Y) and its +y axis is the screen's left (-X). With the sign flip:
 *     ios_x = -Y / g       ios_y = X / g       ios_z = -az / g        (g = 9.80665)
 * Check: held upright in landscape, Y = +g, so ios_x = -1 (the iPad's x axis points up, as iOS reports it).
 * The sensor is disabled while paused.
 *
 * Crash reports: SIGSEGV/SIGBUS/SIGILL/SIGFPE inside game memory name the game address and print the game call chain
 * (checked builds) to logcat (tag "novahd", see core.c); then the previous handler (bionic's debuggerd, which writes
 * the tombstone) is put back and the fault is re-raised into it.
 *
 * Event loop (rt_platform_run): every iteration first drains all pending looper events (activity commands, input,
 * sensor), so input and lifecycle are handled even when frames take longer than 1/60 s; then it waits for the next
 * frame time with ALooper_pollOnce, or runs the frame when it is due.
 */
#include <EGL/egl.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android/sensor.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <jni.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rt.h"

void rt_objc_define_root(void); /* objc.c: NSObject and the root metaclass (not in rt.h's public list) */

#define PACKAGE "com.novahd.recomp"
#define GAME_HOME "/var/mobile/Applications/NOVAHD"
#define GAME_BUNDLE GAME_HOME "/NovaHD.app"
#define SCREEN_W 1024 /* the iPad screen in landscape, in points (= pixels on the iPad 1) */
#define SCREEN_H 768
#define MAX_FINGERS 5
#define GRAVITY 9.80665f

/* ---- state (all of it touched only by the android_main thread, except in the signal handler) ------------------ */
static struct android_app *app_state;
static EGLDisplay dpy = EGL_NO_DISPLAY;
static EGLSurface surf = EGL_NO_SURFACE;
static EGLContext ctx = EGL_NO_CONTEXT;
static EGLConfig cfg;
static EGLint visual_format;
static void *gles; /* libGLESv1_CM.so */
static cpu_t *game_cpu; /* the game's main thread; set once the boot reaches the game's code */
static int booted;      /* rt_platform_run has started: events go to UIKit */
static int resumed = 1; /* the activity is between RESUME and PAUSE (it starts resumed: we are launched) */
static int uikit_paused;
static int buf_w = SCREEN_W, buf_h = SCREEN_H; /* the window buffers: the window's aspect around the 4:3 picture */
static int win_w = SCREEN_W, win_h = SCREEN_H; /* the window in physical pixels (touch coordinates) */
static int finger_ptr[MAX_FINGERS] = {-1, -1, -1, -1, -1}; /* Android pointer id held by each finger slot */
static unsigned long long frames, swaps;
static EGLSurface idle_surf = EGL_NO_SURFACE; /* 1x1 pbuffer that keeps the context current without a window */
static int surfaceless;                        /* EGL_KHR_surfaceless_context */
static pthread_t main_thread;                  /* the android_main thread (the game's main thread) */

/* GL context hand-over (see rt_platform_make_current). Under gl_mx: handed_over (the main thread has given the context
 * up for good: from here on only the render thread may have it), render_owns (a thread has taken it), want_surf and
 * want_gen (what the render thread must be bound to: the window surface, the idle pbuffer, or EGL_NO_SURFACE, and a
 * counter that changes whenever that does), bound_gen (the generation the render thread has bound; the main thread
 * waits for it to catch up before it destroys a surface the render thread may still have current). tl_bound is the
 * surface the calling thread has current. */
static pthread_mutex_t gl_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gl_cv = PTHREAD_COND_INITIALIZER;
static int handed_over, render_owns;
static EGLSurface want_surf = EGL_NO_SURFACE;
static unsigned want_gen, bound_gen;
static __thread EGLSurface tl_bound = EGL_NO_SURFACE;
static __thread int tl_has;

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static int running(void) { return booted && resumed && surf != EGL_NO_SURFACE; }

/* GL entry points: the library's exports first (core ES 1.1 functions), then eglGetProcAddress (extensions) */
static void *getproc(const char *name) {
    void *p = gles ? dlsym(gles, name) : NULL;
    return p ? p : (void *)eglGetProcAddress(name);
}

/* ---- crash reports ------------------------------------------------------------------------------------------------ */
static const int crash_sigs[4] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
static struct sigaction crash_old[4]; /* the handlers before ours (bionic's debuggerd) */
static volatile int crash_reported;

static void on_signal(int sig, siginfo_t *info, void *uc) {
    (void)uc;
    if (!__sync_fetch_and_add(&crash_reported, 1)) { /* one report, even when several threads fault */
        uintptr_t a = (uintptr_t)info->si_addr;
        if (rt_mem && a >= (uintptr_t)rt_mem && a < (uintptr_t)rt_mem + (1ull << 32))
            rt_log("crash: signal %d at game address %#llx (frame %llu)", sig, (unsigned long long)(a - (uintptr_t)rt_mem), frames);
        else
            rt_log("crash: signal %d at host address %p (frame %llu)", sig, (void *)a, frames);
        rt_print_trace(rt_thread_cpu());
    }
    /* put the previous handler back; a real fault re-executes the instruction on return and lands there, a signal
     * sent with kill/raise (si_code <= 0) is sent again */
    for (int k = 0; k < 4; k++)
        if (crash_sigs[k] == sig) sigaction(sig, &crash_old[k], NULL);
    if (info->si_code <= 0) raise(sig);
}

static void install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int k = 0; k < 4; k++) sigaction(crash_sigs[k], &sa, &crash_old[k]);
}

/* ---- display rotation (JNI: activity.getWindowManager().getDefaultDisplay().getRotation()) --------------------- */
static JNIEnv *jni;
static jmethodID m_get_wm, m_get_display, m_get_rotation;
static int rotation_deg = 90; /* the assumed landscape when JNI fails */

char *rt_platform_music_catalog(int grouping, size_t *length) {
    *length = 0;
    JavaVM *vm = app_state->activity->vm;
    JNIEnv *env = NULL;
    jint state = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
    if (state != JNI_OK && (state != JNI_EDETACHED || (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK))
        return NULL;

    jobject activity = app_state->activity->clazz;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID method = cls ? (*env)->GetMethodID(env, cls, "readGameMusicCatalog", "(I)[B") : NULL;
    jbyteArray array = method ? (*env)->CallObjectMethod(env, activity, method, (jint)grouping) : NULL;
    char *result = NULL;
    if (!(*env)->ExceptionCheck(env) && array) {
        jsize size = (*env)->GetArrayLength(env, array);
        if (size > 0 && size <= 512 * 1024) {
            result = malloc((size_t)size);
            if (result) {
                (*env)->GetByteArrayRegion(env, array, 0, size, (jbyte *)result);
                *length = (size_t)size;
            }
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        free(result);
        result = NULL;
        *length = 0;
    }
    if (array) (*env)->DeleteLocalRef(env, array);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (state == JNI_EDETACHED) (*vm)->DetachCurrentThread(vm);
    return result;
}

static int jni_check(void) {
    if ((*jni)->ExceptionCheck(jni)) { (*jni)->ExceptionClear(jni); return 0; }
    return 1;
}

static void poll_rotation(void) {
    static int failed;
    if (failed) return;
    if (!jni) {
        JavaVM *vm = app_state->activity->vm;
        if ((*vm)->AttachCurrentThread(vm, &jni, NULL) != JNI_OK) { jni = NULL; failed = 1; rt_log("android: no JNI: rotation assumed 90"); return; }
        jclass act = (*jni)->GetObjectClass(jni, app_state->activity->clazz);
        jclass wm = (*jni)->FindClass(jni, "android/view/WindowManager");
        jclass disp = (*jni)->FindClass(jni, "android/view/Display");
        if (!jni_check() || !act || !wm || !disp) { failed = 1; rt_log("android: JNI classes missing: rotation assumed 90"); return; }
        m_get_wm = (*jni)->GetMethodID(jni, act, "getWindowManager", "()Landroid/view/WindowManager;");
        m_get_display = (*jni)->GetMethodID(jni, wm, "getDefaultDisplay", "()Landroid/view/Display;");
        m_get_rotation = (*jni)->GetMethodID(jni, disp, "getRotation", "()I");
        (*jni)->DeleteLocalRef(jni, act);
        (*jni)->DeleteLocalRef(jni, wm);
        (*jni)->DeleteLocalRef(jni, disp);
        if (!jni_check() || !m_get_wm || !m_get_display || !m_get_rotation) { failed = 1; rt_log("android: JNI methods missing: rotation assumed 90"); return; }
    }
    jobject wm = (*jni)->CallObjectMethod(jni, app_state->activity->clazz, m_get_wm);
    if (!jni_check() || !wm) return;
    jobject d = (*jni)->CallObjectMethod(jni, wm, m_get_display);
    if (jni_check() && d) {
        jint r = (*jni)->CallIntMethod(jni, d, m_get_rotation);
        if (jni_check() && r >= 0 && r <= 3 && r * 90 != rotation_deg) {
            rotation_deg = r * 90;
            rt_log("android: display rotation %d", rotation_deg);
        }
        (*jni)->DeleteLocalRef(jni, d);
    }
    (*jni)->DeleteLocalRef(jni, wm);
}

/* ---- accelerometer ----------------------------------------------------------------------------------------------- */
static ASensorManager *sensors;
static const ASensor *accel;
static ASensorEventQueue *accel_queue;
static int accel_on;

static void accel_enable(int on) {
    if (!accel_queue || on == accel_on) return;
    if (on) {
        ASensorEventQueue_enableSensor(accel_queue, accel);
        ASensorEventQueue_setEventRate(accel_queue, accel, 16667); /* 60 Hz, as the game asks UIAccelerometer */
    } else
        ASensorEventQueue_disableSensor(accel_queue, accel);
    accel_on = on;
}

static void accel_init(void) {
    sensors = ASensorManager_getInstanceForPackage(PACKAGE);
    accel = sensors ? ASensorManager_getDefaultSensor(sensors, ASENSOR_TYPE_ACCELEROMETER) : NULL;
    if (!accel) { rt_log("android: no accelerometer"); return; }
    accel_queue = ASensorManager_createEventQueue(sensors, app_state->looper, LOOPER_ID_USER, NULL, NULL);
    if (!accel_queue) rt_log("android: cannot create the sensor event queue");
}

/* drain the queue; the newest sample goes to UIKit, converted as the header comment explains */
static void accel_drain(void) {
    ASensorEvent ev[16];
    ssize_t n;
    int have = 0;
    float ax = 0, ay = 0, az = 0;
    while (accel_queue && (n = ASensorEventQueue_getEvents(accel_queue, ev, 16)) > 0)
        for (ssize_t k = 0; k < n; k++)
            if (ev[k].type == ASENSOR_TYPE_ACCELEROMETER) {
                ax = ev[k].acceleration.x; ay = ev[k].acceleration.y; az = ev[k].acceleration.z;
                have = 1;
            }
    if (!have || !booted || !resumed) return;
    float X, Y; /* the screen as displayed: X right, Y up */
    switch (rotation_deg) {
    case 0: X = ax; Y = ay; break;
    case 180: X = -ax; Y = -ay; break;
    case 270: X = ay; Y = -ax; break;
    default: X = -ay; Y = ax; break; /* 90 */
    }
    rt_uikit_accelerometer(game_cpu, -Y / GRAVITY, X / GRAVITY, -az / GRAVITY);
}

/* ---- EGL --------------------------------------------------------------------------------------------------------- */
static int choose_config(void) {
    static const int ds[][2] = {{24, 8}, {24, 0}, {16, 8}, {16, 0}};
    for (int alpha = 8; alpha >= 0; alpha -= 8)
        for (int k = 0; k < 4; k++) {
            const EGLint a[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, alpha,
                                EGL_DEPTH_SIZE, ds[k][0], EGL_STENCIL_SIZE, ds[k][1], EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_NONE};
            EGLint n = 0;
            if (eglChooseConfig(dpy, a, &cfg, 1, &n) && n > 0) {
                rt_log("android: EGL config RGB8%s depth %d stencil %d", alpha ? " alpha 8" : "", ds[k][0], ds[k][1]);
                return 1;
            }
        }
    const EGLint low[] = {EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_DEPTH_SIZE, 16,
                          EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_NONE};
    EGLint n = 0;
    if (eglChooseConfig(dpy, low, &cfg, 1, &n) && n > 0) { rt_log("android: EGL config RGB565 depth 16"); return 1; }
    return 0;
}

/* the display and the ES 1 context, once per process */
static int egl_init(void) {
    EGLint major = 0, minor = 0;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) { rt_log("android: eglInitialize failed (%#x)", eglGetError()); return 0; }
    if (!choose_config()) { rt_log("android: no OpenGL ES 1 EGL config"); return 0; }
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &visual_format);
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
    if (ctx == EGL_NO_CONTEXT) { rt_log("android: cannot create an OpenGL ES 1 context (%#x)", eglGetError()); return 0; }
    rt_log("android: EGL %d.%d", major, minor);
    /* how to keep the context current while there is no window (see the header comment) */
    const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
    surfaceless = ext && strstr(ext, "EGL_KHR_surfaceless_context") != NULL;
    if (!surfaceless) {
        const EGLint pa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        idle_surf = eglCreatePbufferSurface(dpy, cfg, pa); /* EGL_BAD_MATCH when the config has no pbuffers */
        if (idle_surf == EGL_NO_SURFACE) rt_log("android: no surfaceless context or pbuffer: the context is released without a window");
    }
    return 1;
}

static void context_lost(void) {
    rt_log("android: the GL context was lost (the game's textures are gone): exiting");
    exit(1);
}

/* the window's physical pixel size (touch scaling), then 1024x768 buffers */
static void window_geometry(ANativeWindow *w) {
    ANativeWindow_setBuffersGeometry(w, 0, 0, visual_format); /* back to the window's own size, to measure it */
    int ww = ANativeWindow_getWidth(w), wh = ANativeWindow_getHeight(w);
    if (ww > 0 && wh > 0) { win_w = ww; win_h = wh; }
    /* buffers with the window's aspect and the game's 768 lines (or 1024 columns on a window narrower than 4:3):
     * rt_gl_present centres the 4:3 picture in them, so nothing is stretched */
    buf_w = SCREEN_W; buf_h = SCREEN_H;
    if (win_w * 3 > win_h * 4) buf_w = (SCREEN_H * win_w / win_h + 1) & ~1;
    else buf_h = (SCREEN_W * win_h / win_w + 1) & ~1;
    rt_gl_wide_long = buf_w > SCREEN_W ? buf_w : 0; /* widescreen: gles1.c fills the extra width with the 3D scene */
    ANativeWindow_setBuffersGeometry(w, buf_w, buf_h, visual_format);
}

static int surface_create(void) {
    ANativeWindow *w = app_state->window;
    if (!w || surf != EGL_NO_SURFACE) return surf != EGL_NO_SURFACE;
    window_geometry(w);
    surf = eglCreateWindowSurface(dpy, cfg, w, NULL);
    if (surf == EGL_NO_SURFACE) { rt_log("android: eglCreateWindowSurface failed (%#x)", eglGetError()); return 0; }
    if (handed_over) { /* the render thread owns the context: it rebinds to the new surface before its next frame */
        pthread_mutex_lock(&gl_mx);
        want_surf = surf;
        want_gen++;
        pthread_mutex_unlock(&gl_mx);
    } else if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        EGLint e = eglGetError();
        rt_log("android: eglMakeCurrent failed (%#x)", e);
        eglDestroySurface(dpy, surf);
        surf = EGL_NO_SURFACE;
        if (e == EGL_CONTEXT_LOST) context_lost();
        return 0;
    }
    eglSwapInterval(dpy, 1);
    EGLint sw = 0, sh = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &sw);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &sh);
    rt_log("android: window %dx%d, surface %dx%d", win_w, win_h, sw, sh);
    return 1;
}

/* no window, and no way to keep a context bound without one */
static int idle_none(void) { return !surfaceless && idle_surf == EGL_NO_SURFACE; }

static void surface_destroy(void) {
    if (surf == EGL_NO_SURFACE) return;
    if (handed_over) {
        /* the render thread may have the surface current: it rebinds to the idle state at its next swap (it renders
         * at 60 Hz); wait until it has, and only then destroy the surface. The native window must not be used after
         * this returns, so a stuck render thread is waited for 2 s at most. */
        pthread_mutex_lock(&gl_mx);
        want_surf = surfaceless ? EGL_NO_SURFACE : idle_surf;
        unsigned gen = ++want_gen;
        struct timespec dl;
        clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_sec += 2;
        int rc = 0;
        while (render_owns && (int)(bound_gen - gen) < 0 && rc == 0) rc = pthread_cond_timedwait(&gl_cv, &gl_mx, &dl);
        if (rc) rt_log("android: the render thread did not release the window surface in 2 s");
        pthread_mutex_unlock(&gl_mx);
        eglDestroySurface(dpy, surf); /* EGL defers it until no thread has it current */
        surf = EGL_NO_SURFACE;
        return;
    }
    /* the context itself is kept, and stays current on this thread when EGL allows it without a window */
    EGLBoolean ok = EGL_FALSE;
    if (surfaceless) ok = eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    else if (idle_surf != EGL_NO_SURFACE) ok = eglMakeCurrent(dpy, idle_surf, idle_surf, ctx);
    if (!ok) {
        if ((surfaceless || idle_surf != EGL_NO_SURFACE) && eglGetError() == EGL_CONTEXT_LOST) context_lost();
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    eglDestroySurface(dpy, surf);
    surf = EGL_NO_SURFACE;
}

/* ---- touches ------------------------------------------------------------------------------------------------------ */
static int finger_of(int32_t pid, int take) {
    for (int k = 0; k < MAX_FINGERS; k++) if (finger_ptr[k] == pid) return k;
    if (!take) return -1;
    for (int k = 0; k < MAX_FINGERS; k++) if (finger_ptr[k] < 0) { finger_ptr[k] = pid; return k; }
    return -1;
}

static void touch(int phase, const AInputEvent *e, size_t i) {
    int32_t pid = AMotionEvent_getPointerId(e, i);
    int f = finger_of(pid, phase == RT_TOUCH_BEGAN);
    if (f < 0) return;
    static float finger_t[MAX_FINGERS][4]; /* how the 2D element under the finger was placed: kept for the touch */
    float x = AMotionEvent_getX(e, i) * buf_w / (float)win_w,
          y = AMotionEvent_getY(e, i) * buf_h / (float)win_h - (buf_h - SCREEN_H) / 2;
    if (phase == RT_TOUCH_BEGAN) {
        float *t = finger_t[f];
        t[0] = 1; t[1] = 0; t[2] = 1; t[3] = 0;
        if (buf_w > SCREEN_W) rt_gl_wide_touch(x, y, t);
    }
    x = x * finger_t[f][0] + finger_t[f][1];
    y = y * finger_t[f][2] + finger_t[f][3];
    if (x < 0) x = 0;
    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    if (y < 0) y = 0;
    if (y > SCREEN_H - 1) y = SCREEN_H - 1;
    rt_uikit_touch(game_cpu, phase, f, x, y);
    if (phase == RT_TOUCH_ENDED || phase == RT_TOUCH_CANCELLED) finger_ptr[f] = -1;
}

static int32_t on_input(struct android_app *app, AInputEvent *e) {
    (void)app;
    if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION) return 0; /* keys: the default handling (Back finishes) */
    if (!booted) return 1;
    int32_t action = AMotionEvent_getAction(e), kind = action & AMOTION_EVENT_ACTION_MASK;
    size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    size_t count = AMotionEvent_getPointerCount(e);
    switch (kind) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN: touch(RT_TOUCH_BEGAN, e, index); break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP: touch(RT_TOUCH_ENDED, e, index); break;
    case AMOTION_EVENT_ACTION_MOVE:
        for (size_t k = 0; k < count; k++) touch(RT_TOUCH_MOVED, e, k);
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        for (size_t k = 0; k < count; k++) touch(RT_TOUCH_CANCELLED, e, k);
        for (int k = 0; k < MAX_FINGERS; k++) /* fingers the event no longer lists */
            if (finger_ptr[k] >= 0) { rt_uikit_touch(game_cpu, RT_TOUCH_CANCELLED, k, 0, 0); finger_ptr[k] = -1; }
        break;
    default: return 0;
    }
    return 1;
}

/* every finger still down ends as cancelled (the app goes to the background, the window goes away) */
static void cancel_fingers(void) {
    for (int k = 0; k < MAX_FINGERS; k++)
        if (finger_ptr[k] >= 0) {
            if (booted) rt_uikit_touch(game_cpu, RT_TOUCH_CANCELLED, k, 0, 0);
            finger_ptr[k] = -1;
        }
}

/* ---- lifecycle ---------------------------------------------------------------------------------------------------- */
static void sync_pause(void) {
    if (!booted) return;
    int want = !resumed;
    if (want == uikit_paused) return;
    uikit_paused = want;
    if (want) { rt_log("android: pause"); rt_uikit_pause(game_cpu); }
    else { rt_log("android: resume"); rt_uikit_resume(game_cpu); }
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (ctx != EGL_NO_CONTEXT) surface_create();
        break;
    case APP_CMD_TERM_WINDOW:
        cancel_fingers();
        surface_destroy();
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (app->window && surf != EGL_NO_SURFACE) window_geometry(app->window);
        poll_rotation();
        break;
    case APP_CMD_PAUSE:
        resumed = 0;
        cancel_fingers();
        accel_enable(0);
        sync_pause();
        break;
    case APP_CMD_RESUME:
        resumed = 1;
        accel_enable(1);
        sync_pause();
        break;
    case APP_CMD_LOW_MEMORY:
        rt_log("android: low memory"); /* UIApplicationDidReceiveMemoryWarning is UIKit's business; not sent here */
        break;
    default:
        break;
    }
}

/* one event at most. timeout_ms: -1 waits for an event, 0 returns at once. Returns the looper ident of the event
 * handled (>= 0), or a negative ALOOPER_POLL_* code when the timeout expired. Never returns when the activity is
 * destroyed (the process exits). */
static int poll_events(int timeout_ms) {
    struct android_poll_source *source = NULL;
    int events;
    int id = ALooper_pollOnce(timeout_ms, NULL, &events, (void **)&source);
    if (id == LOOPER_ID_USER) accel_drain();
    else if (id >= 0 && source) source->process(app_state, source);
    if (app_state->destroyRequested) {
        rt_log("android: activity destroyed after %llu frames, %llu swaps", frames, swaps);
        exit(0); /* the game keeps global state: start fresh next time */
    }
    return id;
}

/* every event already pending */
static void drain_events(void) {
    while (poll_events(0) >= 0) {}
}

/* ---- the platform interface (rt.h) -------------------------------------------------------------------------------- */
void rt_platform_size(int *width, int *height) {
    if (width) *width = buf_w;
    if (height) *height = buf_h;
}

/* the render thread: bring its EGL binding up to date with want_surf/want_gen (a surface created, or about to be
 * destroyed) and tell the main thread */
static void render_sync(void) {
    pthread_mutex_lock(&gl_mx);
    if (tl_has && bound_gen != want_gen) {
        EGLSurface s = want_surf;
        if (s == EGL_NO_SURFACE && idle_none()) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            tl_bound = EGL_NO_SURFACE;
        } else if (eglMakeCurrent(dpy, s, s, ctx)) {
            tl_bound = s;
        } else {
            EGLint e = eglGetError();
            rt_log("android: render thread rebind failed (%#x)", e);
            if (e == EGL_CONTEXT_LOST) context_lost();
        }
        bound_gen = want_gen;
        pthread_cond_broadcast(&gl_cv);
    }
    pthread_mutex_unlock(&gl_mx);
}

#ifdef NOVA_FRAME_LOG
static pthread_mutex_t diagnostic_mutex = PTHREAD_MUTEX_INITIALIZER;
static char diagnostic_gl[900], diagnostic_swaps[240];
enum { GRAPHICS_EVENTS = 12, GRAPHICS_EVENT_SIZE = 256 };
static char diagnostic_events[GRAPHICS_EVENTS][GRAPHICS_EVENT_SIZE];
static unsigned diagnostic_event_count, diagnostic_events_dropped;

void rt_platform_graphics_event(const char *details) {
    struct timespec stamp = {0};
    clock_gettime(CLOCK_MONOTONIC, &stamp);
    pthread_mutex_lock(&diagnostic_mutex);
    if (diagnostic_event_count < GRAPHICS_EVENTS) {
        snprintf(diagnostic_events[diagnostic_event_count++], GRAPHICS_EVENT_SIZE, "%llu ms %s",
                 (unsigned long long)stamp.tv_sec * 1000 + stamp.tv_nsec / 1000000, details);
    } else diagnostic_events_dropped++;
    pthread_mutex_unlock(&diagnostic_mutex);
}

void rt_platform_graphics_sample(const char *details) {
    pthread_mutex_lock(&diagnostic_mutex);
    snprintf(diagnostic_gl, sizeof diagnostic_gl, "%s", details);
    pthread_mutex_unlock(&diagnostic_mutex);
}

JNIEXPORT void JNICALL
Java_com_novahd_recomp_NativeWindowProbe_configureGraphics(JNIEnv *env, jclass cls, jboolean enabled, jint cull) {
    (void)env; (void)cls;
    rt_gl_configure_diagnostics(enabled, cull);
}

JNIEXPORT jstring JNICALL
Java_com_novahd_recomp_NativeWindowProbe_readGraphics(JNIEnv *env, jclass cls) {
    (void)cls;
    char text[sizeof diagnostic_gl + sizeof diagnostic_swaps + sizeof diagnostic_events + 96];
    pthread_mutex_lock(&diagnostic_mutex);
    size_t used = (size_t)snprintf(text, sizeof text, "%s\n%s", diagnostic_swaps, diagnostic_gl);
    for (unsigned i = 0; i < diagnostic_event_count; ++i)
        used += (size_t)snprintf(text + used, sizeof text - used, "\n%s", diagnostic_events[i]);
    if (diagnostic_events_dropped)
        snprintf(text + used, sizeof text - used, "\neventos omitidos: %u", diagnostic_events_dropped);
    diagnostic_event_count = diagnostic_events_dropped = 0;
    pthread_mutex_unlock(&diagnostic_mutex);
    return (*env)->NewStringUTF(env, text);
}

static int compare_intervals(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

/* Timings at successful eglSwapBuffers return. This is submission cadence, not display presentation. */
static void log_swap_sample(EGLSurface target) {
    static _Thread_local uint64_t previous, start, frames_in_window;
    static _Thread_local EGLSurface previous_target;
    static _Thread_local uint64_t samples[512], maximum;
    static _Thread_local unsigned sample_count;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return;
    uint64_t now = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    if (!start || !previous || now - previous > UINT64_C(1000000000) || target != previous_target) {
        start = previous = now;
        previous_target = target;
        frames_in_window = sample_count = maximum = 0;
        return;
    }
    frames_in_window++;
    uint64_t gap = now - previous;
    if (sample_count < sizeof samples / sizeof samples[0]) samples[sample_count++] = gap;
    if (gap > maximum) maximum = gap;
    if (now - start >= UINT64_C(1000000000)) {
        EGLint w, h;
        int rw, rh;
        rt_gl_measure_render(&rw, &rh);
        char render[40] = "unavailable";
        if (rw > 0 && rh > 0) snprintf(render, sizeof render, "%dx%d", rw, rh);
        if (eglQuerySurface(dpy, target, EGL_WIDTH, &w) && eglQuerySurface(dpy, target, EGL_HEIGHT, &h)) {
            double cadence = 1e9 * frames_in_window / (now - start), interval = (now - previous) / 1e6;
            qsort(samples, sample_count, sizeof samples[0], compare_intervals);
            double p50 = samples[(sample_count - 1) / 2] / 1e6;
            double p95 = samples[(sample_count * 95 + 99) / 100 - 1] / 1e6;
            pthread_mutex_lock(&diagnostic_mutex);
            snprintf(diagnostic_swaps, sizeof diagnostic_swaps,
                     "%.2f swaps/s | ultimo %.2f ms | FBO %s | EGL %dx%d\n"
                     "swap ms media %.2f p50 %.2f p95 %.2f max %.2f (n=%u)",
                     cadence, interval, render, w, h, (now - start) / (1e6 * frames_in_window),
                     p50, p95, maximum / 1e6, sample_count);
            pthread_mutex_unlock(&diagnostic_mutex);
            rt_log("frame: %.2f swaps/s, last %.2f ms, render %s, EGL %dx%d", cadence, interval, render, w, h);
        }
        start = now;
        frames_in_window = sample_count = maximum = 0;
    }
    previous = now;
}
#endif

void rt_platform_swap(void) {
    EGLSurface target = surf;
    if (handed_over) { /* only the thread that has the context swaps; never on the idle pbuffer */
        render_sync();
        target = (tl_has && tl_bound != idle_surf) ? tl_bound : EGL_NO_SURFACE;
    }
    if (target != EGL_NO_SURFACE) {
        swaps++;
        if (!eglSwapBuffers(dpy, target)) {
            EGLint e = eglGetError();
            static int logged;
            if (e == EGL_CONTEXT_LOST) context_lost();
            if (!logged++) rt_log("android: eglSwapBuffers failed (%#x)", e);
        }
#ifdef NOVA_FRAME_LOG
        else log_swap_sample(target);
#endif
    }
    /* during the launch (before rt_platform_run) the activity's events are handled here, so the glue's UI thread does
     * not wait for the whole load. Only on the main thread: the looper, EGL current-ness and UIKit belong to it. Once
     * the loop runs, events are handled between frames, never inside one. */
    if (!booted && pthread_equal(pthread_self(), main_thread)) drain_events();
}

/* the main thread gives the context up when the display loop starts: it is the game's render thread's from now on */
static void release_context(void) {
    pthread_mutex_lock(&gl_mx);
    if (ctx != EGL_NO_CONTEXT) eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    want_surf = surf != EGL_NO_SURFACE ? surf : (surfaceless ? EGL_NO_SURFACE : idle_surf);
    want_gen++;
    bound_gen = want_gen; /* nobody is bound yet: the render thread binds to want_surf when it arrives */
    handed_over = 1;
    pthread_cond_broadcast(&gl_cv);
    pthread_mutex_unlock(&gl_mx);
    rt_log("android: the main thread released the GL context");
}

void rt_platform_run(cpu_t *c) {
    game_cpu = c;
    booted = 1;
    release_context();
    rt_log("android: display loop");
    sync_pause(); /* a pause that arrived during the launch */
    accel_enable(resumed);
    const double period = 1.0 / 60.0;
    double next = now_s();
    int was_running = 0;
    for (;;) {
        /* everything pending first (even when frames run late), then wait for the next frame time, or for an event
         * while there is nothing to draw */
        drain_events();
        if (!running()) { was_running = 0; poll_events(-1); continue; }
        if (!was_running) { was_running = 1; next = now_s(); } /* no catch-up after a pause */
        double wait = next - now_s();
        if (wait > 0) { poll_events((int)(wait * 1000.0) + 1); continue; }
        if (frames % 30 == 0) poll_rotation();
        rt_uikit_frame(c);
        if (c->fault) {
            rt_log("android: frame %llu: game fault %u", frames, c->fault);
            rt_print_trace(c);
            exit(1);
        }
        if (frames == 0) rt_log("android: first frame done");
        frames++;
        next += period;
        double now = now_s();
        if (now > next + 0.25) next = now; /* drop the backlog after a long frame */
    }
}

/* ---- boot ---------------------------------------------------------------------------------------------------------- */
static void make_dir(const char *base, const char *sub) {
    char p[600];
    snprintf(p, sizeof p, "%s%s%s", base, sub[0] ? "/" : "", sub);
    if (mkdir(p, 0770) && errno != EEXIST) rt_log("android: cannot create %s (%s)", p, strerror(errno));
}

#ifndef NOVA_MANAGED_BUNDLE
/* The game bundle packed in the APK (build_apk.py --bundle): assets/bundle.lst has one "<size> <relative path>" line
 * per file under assets/NovaHD.app. Files that are missing or of another size are copied out once, because the game
 * reads its data with fopen. */
static void extract_bundle(const char *bundle) {
    AAssetManager *am = app_state->activity->assetManager;
    AAsset *lst = AAssetManager_open(am, "bundle.lst", AASSET_MODE_BUFFER);
    if (!lst) return;
    size_t n = (size_t)AAsset_getLength64(lst);
    char *text = malloc(n + 1);
    if (!text || AAsset_read(lst, text, n) != (int)n) { AAsset_close(lst); free(text); rt_log("android: cannot read bundle.lst"); return; }
    text[n] = 0;
    AAsset_close(lst);
    make_dir(bundle, "");
    int copied = 0, failed = 0;
    static char block[1 << 16];
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        long long size = atoll(line);
        const char *rel = sp + 1;
        size_t rl = strlen(rel);
        if (rl && rel[rl - 1] == '\r') ((char *)rel)[--rl] = 0;
        char dst[900], src[700];
        snprintf(dst, sizeof dst, "%s/%s", bundle, rel);
        struct stat st;
        if (!stat(dst, &st) && (long long)st.st_size == size) continue;
        for (char *q = dst + strlen(bundle) + 1; (q = strchr(q, '/')); q++) { *q = 0; mkdir(dst, 0770); *q = '/'; }
        snprintf(src, sizeof src, "NovaHD.app/%s", rel);
        AAsset *a = AAssetManager_open(am, src, AASSET_MODE_STREAMING);
        FILE *f = a ? fopen(dst, "wb") : NULL;
        int ok = f != NULL, got;
        while (ok && (got = AAsset_read(a, block, sizeof block)) > 0) ok = fwrite(block, 1, (size_t)got, f) == (size_t)got;
        if (f) fclose(f);
        if (a) AAsset_close(a);
        if (ok) copied++;
        else { failed++; rt_log("android: cannot extract %s to %s", src, dst); }
    }
    free(text);
    if (copied || failed) rt_log("android: game bundle extracted from the APK: %d files copied, %d failed", copied, failed);
}

#endif

static void setup_paths(void) {
    const char *files = app_state->activity->externalDataPath;
    if (!files || !files[0]) files = app_state->activity->internalDataPath;
    static char bundle[512], home[512];
#ifdef NOVA_MANAGED_BUNDLE
    snprintf(bundle, sizeof bundle, "%s/NovaHD.app", app_state->activity->internalDataPath);
#else
    snprintf(bundle, sizeof bundle, "%s/NovaHD.app", files);
#endif
    snprintf(home, sizeof home, "%s/home", files);
    make_dir(files, "");
    make_dir(home, "");
    make_dir(home, "Documents");
    make_dir(home, "Library");
    make_dir(home, "Library/Caches");
    make_dir(home, "tmp");
    snprintf(rt_bundle_path, sizeof rt_bundle_path, "%s", GAME_BUNDLE);
    snprintf(rt_home_path, sizeof rt_home_path, "%s", GAME_HOME);
    rt_path_alias(GAME_BUNDLE, bundle);
    rt_path_alias(GAME_HOME, home);
#ifndef NOVA_MANAGED_BUNDLE
    extract_bundle(bundle);
#endif
    char probe[600];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/Info.plist", bundle);
    if (stat(probe, &st))
#ifdef NOVA_MANAGED_BUNDLE
        rt_fatal("android: verified campaign bundle missing: %s", probe);
#else
        rt_log("android: the game bundle is missing: adb push NovaHD.app %s/ (so that %s exists)", files, probe);
#endif
    rt_log("android: bundle %s, home %s", bundle, home);
}

static void boot(void) {
    AAsset *a = AAssetManager_open(app_state->activity->assetManager, "image.bin", AASSET_MODE_BUFFER);
    if (!a) rt_fatal("image.bin missing from the APK assets");
    const void *buf = AAsset_getBuffer(a);
    if (!buf) rt_fatal("cannot read the image.bin asset");
    if (rt_mem_init_from(buf, (size_t)AAsset_getLength64(a))) rt_fatal("cannot load image.bin"); /* copies the image */
    AAsset_close(a);

    rt_objc_define_root();
    rt_foundation_define();
    rt_collections_define();
    rt_system_define();
    rt_uikit_define();
    rt_graphics_define();
    rt_offline_define();
    gles = dlopen("libGLESv1_CM.so", RTLD_NOW | RTLD_LOCAL);
    if (!gles) rt_log("android: dlopen libGLESv1_CM.so failed (%s): eglGetProcAddress only", dlerror());
    rt_gl_getproc = getproc;
    rt_imports_init();
    rt_data_imports_bind();
    rt_objc_init();

    cpu_t *c = rt_thread_cpu();
    game_cpu = c;
    rt_objc_load();
    for (uint32_t k = 0; k < rt_init_func_count; k++) {
        rt_call(c, rt_init_funcs[k], 0);
        if (c->fault) { rt_print_trace(c); rt_fatal("constructor %#x faulted (%u)", rt_init_funcs[k], c->fault); }
    }
    rt_log("android: %u static constructors done", rt_init_func_count);

    char argv0[600];
    snprintf(argv0, sizeof argv0, "%s/NovaHD", rt_bundle_path);
    uint32_t gargv = rt_calloc(2, 4); /* argv[0], NULL */
    rt_st32(gargv, rt_static_string(argv0));
    uint32_t r = rt_call(c, rt_main, 2, 1, gargv); /* UIApplicationMain -> rt_platform_run: does not return */
    rt_log("android: main returned %u (fault %u)", r, c->fault);
    if (c->fault) rt_print_trace(c);
    exit(c->fault ? 1 : 0);
}

void android_main(struct android_app *app) {
    app_state = app;
    main_thread = pthread_self();
    install_signals();
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    setup_paths();
    if (!egl_init()) rt_fatal("OpenGL ES 1 is not available");
    accel_init();
    poll_rotation();
    /* the first window: the context must be current before the game creates its EAGLContext */
    while (surf == EGL_NO_SURFACE) poll_events(-1);
    boot();
}

/* +[EAGLContext setCurrentContext:] (any thread). EGL allows the context on one thread at a time: the main thread has
 * it during the launch and gives it up for good when it enters rt_platform_run (release_context); the first other
 * thread that asks afterwards (the game's render thread) waits for that, then takes it with the current window
 * surface (or the idle state) and keeps it. From then on the main thread only hands surfaces over (surface_create,
 * surface_destroy) and the render thread follows in render_sync() at its next swap, so the two never race. */
void rt_platform_make_current(void) {
    if (ctx == EGL_NO_CONTEXT || pthread_equal(pthread_self(), main_thread) || tl_has) return;
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 10;
    pthread_mutex_lock(&gl_mx);
    int rc = 0;
    while ((!handed_over || render_owns) && rc == 0) rc = pthread_cond_timedwait(&gl_cv, &gl_mx, &dl);
    if (rc) {
        rt_log("android: a thread waited 10 s for the GL context (%s)", render_owns ? "held by another thread" : "the main thread never released it");
        pthread_mutex_unlock(&gl_mx);
        return;
    }
    EGLSurface s = want_surf;
    int ok = (s == EGL_NO_SURFACE && idle_none()) ? 1 : eglMakeCurrent(dpy, s, s, ctx); /* none: binds at the first surface */
    if (ok) {
        tl_has = 1;
        tl_bound = s;
        render_owns = 1;
        bound_gen = want_gen;
        pthread_cond_broadcast(&gl_cv);
        rt_log("android: the GL context is on the render thread");
    } else
        rt_log("android: eglMakeCurrent on the render thread failed (%#x)", eglGetError());
    pthread_mutex_unlock(&gl_mx);
}
