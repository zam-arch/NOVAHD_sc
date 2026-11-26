/* UIKit, QuartzCore and EAGL for the recompiled N.O.V.A. HD: the application and its launch, views, windows and
 * view controllers, touches, the accelerometer, and the EAGL context and drawable.
 *
 * The device is a first-generation iPad on iPhone OS 3.2: [UIScreen mainScreen].bounds is 768x1024 (portrait;
 * on 3.2 the screen bounds never rotate), the model is "iPad", userInterfaceIdiom is UIUserInterfaceIdiomPad, and
 * the status bar orientation is what the game sets (it sets UIInterfaceOrientationLandscapeRight = 3, home button
 * on the right).
 *
 * ---- What the game does with UIKit (from the disassembly) ----------------------------------------------------------
 * Launch. main() calls UIApplicationMain(argc, argv, nil, @"AppController"). -[AppController
 * application:didFinishLaunchingWithOptions:] calls its own applicationDidFinishLaunching: (0x26b3c), which hides
 * the status bar, sets the status bar orientation to 3, sets up the accelerometer, makes a UIWindow with the screen
 * bounds (768x1024), an EAGLView with the same frame as its only subview, makeKeyAndVisible, and then starts the
 * game on its own pthread (0x91890 -> 0x92110). That thread is the render loop: each pass it sends
 * +[EAGLContext setCurrentContext:] with the game's context, binds its framebuffer, runs a game frame, and sends
 * -presentRenderbuffer: (0x91b7c); while the in-game promotion (IGP) screens are up it runs CFRunLoopRunInMode
 * instead. So nothing has to be driven from here for the game to render: rt_uikit_frame (main thread) only runs
 * the main run loop (NSTimers such as EAGLView's IGP timer, performSelector requests, alert callbacks) inside an
 * autorelease pool, and lays out views that asked for it. The EAGL context is therefore current per thread
 * (+currentContext answers the calling thread's), as on iOS.
 * -[EAGLView initWithFrame:] creates the context through 0x919a0: first initWithAPI:2 (ES 2; when that works the
 * game only notes it and releases it), then initWithAPI:1. This runtime implements ES 1 only, so initWithAPI:2
 * returns nil like a device without ES 2. The game then creates its framebuffer (0x91e78): glGenFramebuffersOES,
 * glGenRenderbuffersOES, -renderbufferStorage:GL_RENDERBUFFER_OES fromDrawable:<the view's CAEAGLLayer>, reads
 * the renderbuffer size back with glGetRenderbufferParameterivOES, and adds a depth renderbuffer. The layer's
 * bounds (768x1024, the view's) are what EAGLView keeps as its surface size.
 *
 * Touches. -[EAGLView touchesBegan/Moved/Ended:withEvent:] (0x611c, 0x5ee4, 0x5cac) enumerate the touch set with
 * countByEnumeratingWithState:objects:count:, ask each touch for locationInView:self (objc_msgSend_stret), and
 * pass the point through -[EAGLView ConvertCoordP:] (0x5648), which reads [UIApplication statusBarOrientation]:
 *     orientation 3 (LandscapeRight):  game = (view.y, 768 - view.x)       (768 = the view width it saved)
 *     orientation 4 (LandscapeLeft):   game = (1024 - view.y, view.x)
 *     orientation 1 (Portrait):        game = view
 * So the game expects PORTRAIT view coordinates (768x1024, the iPad's view space, which does not rotate on 3.2) and
 * rotates them itself into its 1024x768 landscape space. rt_uikit_touch gets landscape screen points (sx, sy), as
 * the player sees them, with the home button on the right; the portrait point under that spot is
 *     portrait = (768 - sy, sx)
 * which the game's conversion (orientation 3) turns back into (sx, sy). The window and the views below it work in
 * that portrait space; hit testing, locationInView: and convertPoint: all use it. The IGP path (0x1aa7f8) takes the
 * raw portrait points together with previousLocationInView:, phase and tapCount.
 *
 * Accelerometer. -[AppController accelerometer:didAccelerate:] reads x, y, z (doubles, returned in r0:r1) and
 * converts them to floats. rt_uikit_accelerometer's arguments go out unchanged: they must already be in g along the
 * iPad's own axes as UIAcceleration reports them (x towards the right edge of the portrait screen, y towards its
 * top, z out of the screen; lying flat face up reads (0, 0, -1)). Deliveries are spaced by -setUpdateInterval:.
 *
 * Text input. TextInput (the game's name entry, a UIViewController) makes a UITextField with its delegate set to
 * itself, sets the current text, sends becomeFirstResponder and polls isDone, which is "the return key was hit and
 * the field is no longer editing". There is no keyboard here: one run loop turn after becomeFirstResponder the
 * field "hits return" for the player (textFieldShouldReturn: to the delegate, which reads the text and resigns
 * first responder), so the flow completes with the text the game put there. When the environment variable
 * NOVAHD_TEXT_INPUT is set and the field is empty, its value is typed in first (with
 * UITextFieldTextDidChangeNotification posted, as typing does).
 *
 * Alerts. -show logs the title and message. An alert with exactly one button is answered on the next run loop
 * turn: alertView:clickedButtonAtIndex:0 then alertView:didDismissWithButtonIndex:0 to the delegate (when it
 * implements them). Alerts with more buttons stay up (logged): there is nobody to choose.
 *
 * Drawing. UIKit views are not composited: only the EAGL drawable reaches the screen. Labels, buttons and image
 * views keep their state (texts, images, targets) so that game code reading it back works.
 *
 * ---- Layouts and choices ---------------------------------------------------------------------------------------
 * Instance sizes match the instanceStart of the game's subclasses (UIView 36 bytes: EAGLView starts at 36;
 * UIImageView 40; UIViewController 120; UINavigationController 200). The ivars are never used: all UIKit state is
 * host state in a side table of this file (object -> ui_state), not rt_objc_state, so that classes other modules
 * derive from UIView (UITableView, UIWebView in offline.c) keep rt_objc_state for themselves. The entry is freed
 * by the -dealloc of the classes defined here.
 * Geometry is kept the way UIKit keeps it: center, bounds and transform; -frame is the bounding box of the
 * transformed bounds. Hit testing and point conversion apply the inverse transforms.
 * Simple properties (tag, alpha, hidden, text, font, delegate, ...) go through one generic setter and getter that
 * key on the selector name ("setFooBar:" and "fooBar"/"isFooBar" share the key "fooBar"); object-valued ones
 * (colors, images, texts, fonts, ...) are retained, delegates are not. Floats are stored as their bits, which is
 * also how softfp returns them.
 * "The next run loop turn": a queue of deferred messages (alert answers, text field returns, animation-did-stop
 * callbacks) run by rt_uikit_frame after the run loop, on the main thread.
 * Struct results (CGRect, CGPoint, CGSize, CGAffineTransform) come back through the stret pointer: r0 = result,
 * r1 = self, r2 = _cmd, arguments from word 3 (SARG).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt.h"

#if defined(_WIN32) && !defined(_WIN64)
#define GLAPI __stdcall
#else
#define GLAPI
#endif

#define SELF c->r[0]
#define ARG(k) rt_arg(c, (k) + 2)
#define SSELF c->r[1]             /* self of a method called through objc_msgSend_stret */
#define SARG(k) rt_arg(c, (k) + 3) /* its arguments */
#define OUT c->r[0]               /* its result address */
#define M(name) static void name(cpu_t *c, uint8_t *m)

#define SCREEN_W 768.0f  /* the portrait screen in points */
#define SCREEN_H 1024.0f
#define MAX_FINGERS 16
#define MAX_WINDOWS 8

enum { UIControlEventTouchDown = 1 << 0, UIControlEventTouchUpInside = 1 << 6, UIControlEventTouchUpOutside = 1 << 7,
       UIControlEventTouchCancel = 1 << 8, UIControlEventEditingChanged = 1 << 17 };

static float fw(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
static uint32_t wf(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
static void put_floats(uint32_t g, const float *v, int n) { if (g) memcpy(rt_mem + g, v, 4 * (size_t)n); }

/* ---- host state ------------------------------------------------------------------------------------------------ */
typedef struct { SEL key; uint32_t val; int retained; } prop;
typedef struct { id target; SEL action; uint32_t events; } target_action;

typedef struct ui_state {
    prop *props;                        /* generic properties */
    uint32_t nprops, capprops;
    /* views: center, bounds, transform (a b c d tx ty), hierarchy */
    float cx, cy, bx, by, bw, bh, t[6];
    int has_t, needs_layout;
    id superview, vc, layer;            /* vc: the controller whose view this is (not retained) */
    id *subs;                           /* retained */
    uint32_t nsubs, capsubs;
    float content_w, content_h;         /* UIScrollView contentSize */
    /* layers */
    id owner;                           /* the view a layer belongs to (not retained) */
    float lb[4];                        /* bounds of a layer without a view */
    /* view controllers */
    id view, modal, presenter, parent;  /* view and modal retained */
    id *kids;                           /* navigation stack / tab controllers, retained */
    uint32_t nkids, capkids, selected;
    int loading;
    /* controls */
    target_action *acts;
    uint32_t nacts, capacts;
    id titles[4], images[4], backgrounds[4]; /* per state: normal, highlighted, disabled, selected (retained) */
    /* touches */
    int phase, taps;
    double ts;
    float x, y, px, py;                 /* in the touch's window */
    id tview, twin;                     /* retained */
    /* touch sets */
    uint32_t items, nitems;             /* game array of retained objects */
    /* alerts */
    id buttons[8];                      /* retained titles */
    int nbuttons, cancel_index;
    /* misc */
    double interval, last, ax, ay, az;  /* accelerometer / acceleration */
    int api, animating, pending_return;
} ui_state;

static volatile int ui_lock_word;
static void lk(void) { while (__atomic_exchange_n(&ui_lock_word, 1, __ATOMIC_ACQUIRE)) { } }
static void ulk(void) { __atomic_store_n(&ui_lock_word, 0, __ATOMIC_RELEASE); }

/* object -> state: open addressing; key 0 empty, 1 deleted (objects are never at address 1) */
static uint32_t *map_keys;
static ui_state **map_vals;
static uint32_t map_cap, map_used;

static uint32_t map_slot(uint32_t key) { return (key * 2654435761u) >> 3; }

static void map_insert(uint32_t key, ui_state *v);
static void map_grow(void) {
    uint32_t *ok = map_keys, oc = map_cap;
    ui_state **ov = map_vals;
    map_cap = oc ? oc * 2 : 4096;
    map_keys = calloc(map_cap, sizeof *map_keys);
    map_vals = calloc(map_cap, sizeof *map_vals);
    map_used = 0;
    for (uint32_t k = 0; k < oc; k++)
        if (ok[k] > 1) map_insert(ok[k], ov[k]);
    free(ok);
    free(ov);
}

static void map_insert(uint32_t key, ui_state *v) {
    if ((map_used + 1) * 10 >= map_cap * 7) map_grow();
    uint32_t i = map_slot(key) & (map_cap - 1);
    while (map_keys[i] > 1) i = (i + 1) & (map_cap - 1);
    map_keys[i] = key;
    map_vals[i] = v;
    map_used++;
}

static int map_find(uint32_t key) {
    if (!map_cap) return -1;
    for (uint32_t i = map_slot(key) & (map_cap - 1);; i = (i + 1) & (map_cap - 1)) {
        if (map_keys[i] == key) return (int)i;
        if (map_keys[i] == 0) return -1;
    }
}

static ui_state *peek(id o) {
    if (!o) return NULL;
    lk();
    int i = map_find(o);
    ui_state *s = i >= 0 ? map_vals[i] : NULL;
    ulk();
    return s;
}

static const float identity[6] = {1, 0, 0, 1, 0, 0};

/* the state of an object, created on first use */
static ui_state *st(id o) {
    static ui_state dummy; /* for nil receivers (never written in practice) */
    if (!o) { memset(&dummy, 0, sizeof dummy); return &dummy; }
    ui_state *s = peek(o);
    if (s) return s;
    s = calloc(1, sizeof *s);
    memcpy(s->t, identity, sizeof identity);
    s->interval = 0.1;
    lk();
    int i = map_find(o);
    if (i >= 0) { ulk(); free(s); return map_vals[i]; }
    map_insert(o, s);
    ulk();
    return s;
}

static void st_remove(id o) {
    lk();
    int i = map_find(o);
    if (i >= 0) { map_keys[i] = 1; map_vals[i] = NULL; }
    ulk();
}

/* ---- classes and shared objects ----------------------------------------------------------------------------------- */
static id cls_app, cls_view, cls_window, cls_control, cls_button, cls_label, cls_imageview, cls_scroll, cls_textfield,
    cls_textview, cls_activity, cls_alert, cls_vc, cls_nav, cls_tab, cls_layer, cls_eagllayer, cls_context, cls_touch,
    cls_event, cls_touchset, cls_acceleration, cls_popover;
static id app_object, app_delegate, screen_object, device_object, accel_object, event_object;
static id accel_delegate;
static id windows[MAX_WINDOWS];
static int nwindows;
static id key_window, first_responder;
static int status_bar_hidden;
static uint32_t status_bar_orientation = 3; /* UIInterfaceOrientationLandscapeRight, until the game sets it */
static __thread id current_context;          /* +[EAGLContext currentContext], per thread as on iOS */

static SEL k_hidden, k_alpha, k_user_interaction, k_multitouch, k_enabled, k_highlighted, k_selected, k_delegate,
    k_text, k_tag, k_editing, k_title, k_message, k_image, k_hides_stopped;

/* ---- generic properties --------------------------------------------------------------------------------------- */
static int prop_index(ui_state *s, SEL key) {
    for (uint32_t k = 0; k < s->nprops; k++)
        if (s->props[k].key == key) return (int)k;
    return -1;
}

static uint32_t default_for(id o, SEL key) {
    const char *n = rt_sel_name(key);
    if (key == k_alpha || !strcmp(n, "opacity") || !strcmp(n, "contentsScale") || !strcmp(n, "contentScaleFactor"))
        return wf(1.0f);
    if (key == k_user_interaction) return !(rt_is_kind(o, cls_label) || rt_is_kind(o, cls_imageview));
    if (key == k_enabled || key == k_hides_stopped || !strcmp(n, "opaque") || !strcmp(n, "autoresizesSubviews") ||
        !strcmp(n, "editable") || !strcmp(n, "scrollEnabled") || !strcmp(n, "numberOfLines") ||
        !strcmp(n, "clearsContextBeforeDrawing") || !strcmp(n, "showsVerticalScrollIndicator") ||
        !strcmp(n, "showsHorizontalScrollIndicator") || !strcmp(n, "bounces"))
        return 1;
    return 0;
}

static uint32_t prop_get(id o, SEL key) {
    ui_state *s = peek(o);
    int i = s ? prop_index(s, key) : -1;
    return i >= 0 ? s->props[i].val : default_for(o, key);
}

/* a BOOL property: only the low byte counts (BOOL is a signed char; the caller's upper register bits are not
 * guaranteed to be clear) */
static int prop_bool(id o, SEL key) { return (prop_get(o, key) & 0xff) != 0; }

static void prop_set(cpu_t *c, id o, SEL key, uint32_t val, int retained) {
    if (!o) return;
    ui_state *s = st(o);
    int i = prop_index(s, key);
    if (i < 0) {
        if (s->nprops == s->capprops) {
            s->capprops = s->capprops ? s->capprops * 2 : 8;
            s->props = realloc(s->props, sizeof(prop) * s->capprops);
        }
        i = (int)s->nprops++;
        s->props[i] = (prop){key, default_for(o, key), 0};
    }
    prop *p = &s->props[i];
    if (retained) rt_retain(val);
    uint32_t old = p->val;
    int old_retained = p->retained;
    p->val = val;
    p->retained = retained;
    if (old_retained) rt_release(c, old);
}

/* objects that properties hold strongly: colors, images, texts, fonts, ... (delegates are not retained).
 * Colors and images are recognised by the key's last word ("textColor", "highlightedImage", "titleColor:forState:"),
 * never by a substring: BOOL keys such as adjustsImageWhenHighlighted or allowsImageEditing hold 0/1, and
 * retaining "object" 1 would later send -dealloc to address 1. */
static int ends_with(const char *k, size_t n, const char *suffix) {
    size_t m = strlen(suffix);
    return n >= m && !strncmp(k + n - m, suffix, m);
}
static int is_retained_key(const char *k) {
    static const char *const names[] = {"text", "font", "placeholder", "title", "message", "drawableProperties",
                                        "tabBarItem", "background", "contents", "name", "mediaTypes", "titleLabel",
                                        "contentViewController", "userInfo", "image", "color"};
    size_t n = strcspn(k, ":"); /* "titleColor:forState:" -> "titleColor" */
    if (ends_with(k, n, "Color") || ends_with(k, n, "Image") || ends_with(k, n, "Images")) return 1;
    for (size_t j = 0; j < sizeof names / sizeof *names; j++)
        if (!strcmp(k, names[j])) return 1;
    return 0;
}

/* the key of a setter ("setFooBar:" -> "fooBar", also for "setTitleColor:forState:") or getter ("isFoo" -> "foo") */
static SEL key_of(const char *sel, int setter) {
    char b[128];
    const char *p = sel;
    if (setter && !strncmp(p, "set", 3)) p += 3;
    else if (!setter && p[0] == 'i' && p[1] == 's' && p[2] >= 'A' && p[2] <= 'Z') p += 2;
    snprintf(b, sizeof b, "%s", p);
    size_t n = strlen(b);
    if (setter && n && b[n - 1] == ':' && strchr(b, ':') == b + n - 1) b[n - 1] = 0; /* one argument: drop the colon */
    if (b[0] >= 'A' && b[0] <= 'Z' && !(b[1] >= 'A' && b[1] <= 'Z')) b[0] = (char)(b[0] - 'A' + 'a');
    return rt_sel(b);
}

M(p_set) {
    SEL key = key_of(rt_sel_name(c->r[1]), 1);
    prop_set(c, SELF, key, ARG(0), is_retained_key(rt_sel_name(key)));
}
M(p_get) { rt_ret(c, prop_get(SELF, key_of(rt_sel_name(c->r[1]), 0))); }

static void props(id cls, const char *const *sels, size_t n) {
    for (size_t k = 0; k < n; k++) {
        const char *s = sels[k];
        rt_objc_method(cls, s, strchr(s, ':') ? p_set : p_get);
    }
}
#define PROPS(cls, ...) do { static const char *const l_[] = {__VA_ARGS__}; props(cls, l_, sizeof l_ / sizeof *l_); } while (0)

/* ---- small helpers --------------------------------------------------------------------------------------------- */
M(ret_self) { (void)c; }
M(ret_zero) { rt_ret(c, 0); }
M(ret_one) { rt_ret(c, 1); }
M(ret_zero_stret) { if (OUT) memset(rt_mem + OUT, 0, 8); }    /* an 8-byte struct: NSRange, CGPoint, CGSize */
M(ret_zero_stret16) { if (OUT) memset(rt_mem + OUT, 0, 16); } /* UIEdgeInsets, CGRect */

static void log_once(const char *what) {
    static const char *seen[64];
    static int n;
    lk();
    for (int k = 0; k < n; k++)
        if (seen[k] == what) { ulk(); return; }
    if (n < 64) seen[n++] = what;
    ulk();
    rt_log("uikit: %s", what);
}

static id new_object(cpu_t *c, id cls) { return rt_send(c, rt_send(c, cls, "alloc", 0), "init", 0); }

static id pool_begin(cpu_t *c) { return new_object(c, rt_class("NSAutoreleasePool")); }
static void pool_end(cpu_t *c, id pool) { if (pool) rt_send(c, pool, "drain", 0); }

static const char *utf8(id s) { const char *u = rt_nsstring_utf8(s); return u ? u : ""; }

/* [obj sel] for a method returning a struct through the stret pointer `out` */
static void send_stret(cpu_t *c, id obj, const char *sel, uint32_t out, int nargs, uint32_t a0, uint32_t a1) {
    if (!obj || !out) return;
    SEL s = rt_sel(sel);
    uint32_t imp = rt_lookup(rt_object_class(obj), s);
    if (!imp) return;
    uint32_t w[5] = {out, obj, s, a0, a1};
    rt_callv(c, imp, 3 + nargs, w);
}

/* ---- the deferred queue ("the next run loop turn") ------------------------------------------------------------- */
enum { DEFER_SEND, DEFER_TEXT_RETURN };
typedef struct { int kind; id target; const char *sel; int nargs; uint32_t a[3]; id hold[2]; } deferred;
static deferred defer_q[64];
static int defer_n;

/* target, hold and hold2 stay retained until the message has run (taken before the entry becomes visible to
 * run_deferred, which may run on another thread) */
static void defer(int kind, id target, const char *sel, int nargs, uint32_t a0, uint32_t a1, uint32_t a2, id hold,
                  id hold2) {
    rt_retain(target);
    rt_retain(hold);
    rt_retain(hold2);
    lk();
    if (defer_n == 64) {
        ulk();
        log_once("deferred queue full (a deferred message was dropped)");
        cpu_t *c = rt_thread_cpu();
        rt_release(c, target);
        rt_release(c, hold);
        rt_release(c, hold2);
        return;
    }
    defer_q[defer_n++] = (deferred){kind, target, sel, nargs, {a0, a1, a2}, {hold, hold2}};
    ulk();
}

static void text_return(cpu_t *c, id field);

static void run_deferred(cpu_t *c) {
    lk();
    int n = defer_n;
    deferred q[64];
    memcpy(q, defer_q, sizeof(deferred) * (size_t)n);
    defer_n = 0;
    ulk();
    for (int k = 0; k < n; k++) {
        deferred *d = &q[k];
        if (d->kind == DEFER_TEXT_RETURN) text_return(c, d->target);
        else if (d->target && rt_responds(d->target, d->sel)) rt_send(c, d->target, d->sel, d->nargs, d->a[0], d->a[1], d->a[2]);
        rt_release(c, d->target);
        rt_release(c, d->hold[0]);
        rt_release(c, d->hold[1]);
    }
}

/* ---- geometry --------------------------------------------------------------------------------------------------- */
static void frame_of(ui_state *s, float f[4]) {
    if (!s->has_t) { f[0] = s->cx - s->bw / 2; f[1] = s->cy - s->bh / 2; f[2] = s->bw; f[3] = s->bh; return; }
    float lo_x = 1e30f, lo_y = 1e30f, hi_x = -1e30f, hi_y = -1e30f;
    for (int k = 0; k < 4; k++) {
        float x = (k & 1 ? 0.5f : -0.5f) * s->bw, y = (k & 2 ? 0.5f : -0.5f) * s->bh;
        float X = s->t[0] * x + s->t[2] * y + s->t[4], Y = s->t[1] * x + s->t[3] * y + s->t[5];
        if (X < lo_x) lo_x = X;
        if (X > hi_x) hi_x = X;
        if (Y < lo_y) lo_y = Y;
        if (Y > hi_y) hi_y = Y;
    }
    f[0] = s->cx + lo_x; f[1] = s->cy + lo_y; f[2] = hi_x - lo_x; f[3] = hi_y - lo_y;
}

static void set_frame(ui_state *s, float x, float y, float w, float h) {
    if (w != s->bw || h != s->bh) s->needs_layout = 1;
    s->cx = x + w / 2;
    s->cy = y + h / 2;
    s->bw = w;
    s->bh = h;
}

/* a point in the superview's space -> the view's own (bounds) space, and back */
static void to_local(ui_state *s, float px, float py, float *lx, float *ly) {
    float ux = px - s->cx, uy = py - s->cy;
    if (s->has_t) {
        ux -= s->t[4];
        uy -= s->t[5];
        float det = s->t[0] * s->t[3] - s->t[1] * s->t[2];
        if (det != 0) {
            float x = (s->t[3] * ux - s->t[2] * uy) / det, y = (-s->t[1] * ux + s->t[0] * uy) / det;
            ux = x;
            uy = y;
        }
    }
    *lx = ux + s->bx + s->bw / 2;
    *ly = uy + s->by + s->bh / 2;
}

static void from_local(ui_state *s, float lx, float ly, float *px, float *py) {
    float ux = lx - s->bx - s->bw / 2, uy = ly - s->by - s->bh / 2;
    if (s->has_t) {
        float x = s->t[0] * ux + s->t[2] * uy + s->t[4], y = s->t[1] * ux + s->t[3] * uy + s->t[5];
        ux = x;
        uy = y;
    }
    *px = ux + s->cx;
    *py = uy + s->cy;
}

/* a view's point -> the space of its topmost ancestor (its window), and back */
static void view_to_root(id v, float x, float y, float *rx, float *ry) {
    for (ui_state *s; v && (s = peek(v)) && s->superview; v = s->superview) from_local(s, x, y, &x, &y);
    *rx = x;
    *ry = y;
}

static void root_to_view(id v, float x, float y, float *vx, float *vy) {
    id chain[64];
    int n = 0;
    for (ui_state *s; v && n < 64 && (s = peek(v)) && s->superview; v = s->superview) chain[n++] = v;
    while (n-- > 0) to_local(st(chain[n]), x, y, &x, &y);
    *vx = x;
    *vy = y;
}

/* the deepest visible, interactive view under a point given in the view's superview space */
static id hit_test(id v, float px, float py) {
    ui_state *s = peek(v);
    if (!s || prop_bool(v, k_hidden) || fw(prop_get(v, k_alpha)) < 0.01f || !prop_bool(v, k_user_interaction)) return 0;
    float lx, ly;
    to_local(s, px, py, &lx, &ly);
    if (lx < s->bx || ly < s->by || lx >= s->bx + s->bw || ly >= s->by + s->bh) return 0;
    for (uint32_t k = s->nsubs; k-- > 0;) {
        id h = hit_test(s->subs[k], lx, ly);
        if (h) return h;
    }
    return v;
}

/* ---- view hierarchy -------------------------------------------------------------------------------------------- */
static id window_of(id v) {
    for (; v; v = peek(v) ? peek(v)->superview : 0)
        if (rt_is_kind(v, cls_window)) return v;
    return 0;
}

static void detach(cpu_t *c, id sub) { /* remove from its superview's list; the caller owns the reference after */
    ui_state *ss = peek(sub);
    if (!ss || !ss->superview) return;
    ui_state *p = st(ss->superview);
    for (uint32_t k = 0; k < p->nsubs; k++)
        if (p->subs[k] == sub) {
            memmove(p->subs + k, p->subs + k + 1, sizeof(id) * (p->nsubs - k - 1));
            p->nsubs--;
            break;
        }
    ss->superview = 0;
    (void)c;
}

static void insert_sub(cpu_t *c, id v, id sub, uint32_t at) {
    if (!v || !sub || v == sub) return;
    rt_retain(sub);
    if (peek(sub) && peek(sub)->superview) {
        detach(c, sub);
        rt_release(c, sub); /* the old superview's reference */
    }
    ui_state *s = st(v), *ss = st(sub);
    if (s->nsubs == s->capsubs) {
        s->capsubs = s->capsubs ? s->capsubs * 2 : 8;
        s->subs = realloc(s->subs, sizeof(id) * s->capsubs);
    }
    if (at > s->nsubs) at = s->nsubs;
    memmove(s->subs + at + 1, s->subs + at, sizeof(id) * (s->nsubs - at));
    s->subs[at] = sub;
    s->nsubs++;
    ss->superview = v;
    ss->needs_layout = 1;
    s->needs_layout = 1;
    rt_send(c, sub, "didMoveToSuperview", 0);
    if (window_of(v)) rt_send(c, sub, "didMoveToWindow", 0);
}

static int sub_index(ui_state *s, id sub) {
    for (uint32_t k = 0; k < s->nsubs; k++)
        if (s->subs[k] == sub) return (int)k;
    return -1;
}

static void remove_from_superview(cpu_t *c, id v) {
    ui_state *s = peek(v);
    if (!s || !s->superview) return;
    if (first_responder && (first_responder == v)) first_responder = 0;
    detach(c, v);
    rt_send(c, v, "didMoveToSuperview", 0);
    rt_release(c, v);
}

static void layout_tree(cpu_t *c, id v, int depth) {
    ui_state *s = peek(v);
    if (!s || depth > 64) return;
    if (s->needs_layout) {
        s->needs_layout = 0;
        rt_send(c, v, "layoutSubviews", 0);
    }
    uint32_t n = s->nsubs;
    if (!n) return;
    id copy[128];
    if (n > 128) n = 128;
    memcpy(copy, s->subs, sizeof(id) * n);
    for (uint32_t k = 0; k < n; k++) rt_retain(copy[k]); /* layoutSubviews may remove (and free) siblings */
    for (uint32_t k = 0; k < n; k++) layout_tree(c, copy[k], depth + 1);
    for (uint32_t k = 0; k < n; k++) rt_release(c, copy[k]);
}

static void layout_windows(cpu_t *c) {
    for (int k = 0; k < nwindows; k++) layout_tree(c, windows[k], 0);
}

/* the responder after a responder: a view's controller or superview, a controller's view's superview, the app */
static id next_responder(id r) {
    if (!r) return 0;
    if (rt_is_kind(r, cls_vc)) { ui_state *s = peek(r); return s && s->view && peek(s->view) ? peek(s->view)->superview : 0; }
    if (rt_is_kind(r, cls_window)) return app_object;
    if (rt_is_kind(r, cls_view)) { ui_state *s = peek(r); return s ? (s->vc ? s->vc : s->superview) : 0; }
    return 0;
}

/* ---- -dealloc of every class defined here ------------------------------------------------------------------------ */
static void release_state(cpu_t *c, id o) {
    ui_state *s = peek(o);
    if (!s) return;
    st_remove(o);
    if (first_responder == o) first_responder = 0;
    if (key_window == o) key_window = 0;
    if (accel_delegate == o) accel_delegate = 0;
    for (uint32_t k = 0; k < s->nprops; k++)
        if (s->props[k].retained) rt_release(c, s->props[k].val);
    for (uint32_t k = 0; k < s->nsubs; k++) {
        ui_state *ss = peek(s->subs[k]);
        if (ss) ss->superview = 0;
        rt_release(c, s->subs[k]);
    }
    if (s->layer) { ui_state *ls = peek(s->layer); if (ls) ls->owner = 0; rt_release(c, s->layer); }
    if (s->view) { ui_state *vs = peek(s->view); if (vs && vs->vc == o) vs->vc = 0; rt_release(c, s->view); }
    if (s->modal) { ui_state *ms = peek(s->modal); if (ms && ms->presenter == o) ms->presenter = 0; rt_release(c, s->modal); }
    for (uint32_t k = 0; k < s->nkids; k++) {
        ui_state *ks = peek(s->kids[k]);
        if (ks && ks->parent == o) ks->parent = 0;
        rt_release(c, s->kids[k]);
    }
    if (s->presenter) { ui_state *ps = peek(s->presenter); if (ps && ps->modal == o) ps->modal = 0; }
    for (int k = 0; k < 4; k++) { rt_release(c, s->titles[k]); rt_release(c, s->images[k]); rt_release(c, s->backgrounds[k]); }
    for (int k = 0; k < s->nbuttons; k++) rt_release(c, s->buttons[k]);
    rt_release(c, s->tview);
    rt_release(c, s->twin);
    if (s->items) {
        for (uint32_t k = 0; k < s->nitems; k++) rt_release(c, rt_ld32(s->items + 4 * k));
        rt_free(s->items);
    }
    free(s->props);
    free(s->subs);
    free(s->kids);
    free(s->acts);
    free(s);
}

M(ui_dealloc) {
    release_state(c, SELF);
    rt_objc_free(SELF);
}

/* ---- UIResponder ----------------------------------------------------------------------------------------------- */
M(r_next) { rt_ret(c, next_responder(SELF)); }
M(r_forward_touches) { /* touchesBegan/Moved/Ended/Cancelled:withEvent: not handled: up the responder chain */
    id n = next_responder(SELF);
    if (n) rt_send(c, n, rt_sel_name(c->r[1]), 2, ARG(0), ARG(1));
}
M(r_become) { first_responder = SELF; rt_ret(c, 1); }
M(r_resign) { if (first_responder == SELF) first_responder = 0; rt_ret(c, 1); }
M(r_is_first) { rt_ret(c, first_responder == SELF); }

/* ---- UIApplication ------------------------------------------------------------------------------------------------ */
M(app_shared) { rt_ret(c, app_object); }
M(app_delegate_get) { rt_ret(c, app_delegate); }
M(app_delegate_set) { app_delegate = ARG(0); }
M(app_orientation) { rt_ret(c, status_bar_orientation); }
M(app_set_orientation) { status_bar_orientation = ARG(0); } /* setStatusBarOrientation:(animated:) */
M(app_set_status_hidden) { status_bar_hidden = ARG(0) & 0xff; } /* setStatusBarHidden:(animated:) */
M(app_status_hidden) { rt_ret(c, (uint32_t)status_bar_hidden); }
M(app_key_window) { rt_ret(c, key_window); }
M(app_windows) { rt_ret(c, rt_autorelease(rt_nsarray_new(windows, (uint32_t)nwindows))); }
M(app_open_url) {
    rt_log("uikit: [UIApplication openURL:%s] (no browser here)", rt_objc_describe(c, ARG(0)));
    rt_ret(c, 0);
}
M(app_register_notifications) { rt_log("uikit: registerForRemoteNotificationTypes:%u ignored (offline)", ARG(0)); }
M(app_state) { rt_ret(c, 0); } /* UIApplicationStateActive */
M(app_status_frame) {
    float f[4] = {0, 0, 0, 0};
    if (!status_bar_hidden) {
        if (status_bar_orientation == 3) { f[0] = SCREEN_W - 20; f[2] = 20; f[3] = SCREEN_H; }
        else if (status_bar_orientation == 4) { f[2] = 20; f[3] = SCREEN_H; }
        else { f[2] = SCREEN_W; f[3] = 20; }
    }
    put_floats(OUT, f, 4);
}

/* UIApplicationMain(argc, argv, principalClassName, delegateClassName): never returns on iOS */
static void imp_UIApplicationMain(cpu_t *c) {
    const char *pname = rt_nsstring_utf8(rt_arg(c, 2)), *dname = rt_nsstring_utf8(rt_arg(c, 3));
    id pool = pool_begin(c);
    id pcls = pname ? rt_class(pname) : 0;
    if (pname && !pcls) rt_log("uikit: UIApplicationMain: no principal class %s, using UIApplication", pname);
    app_object = new_object(c, pcls ? pcls : cls_app);
    rt_log("uikit: UIApplicationMain: delegate class %s", dname ? dname : "(none: would come from the main nib)");
    id dcls = dname ? rt_class(dname) : 0;
    if (!dcls) rt_fatal("UIApplicationMain: no delegate class %s (this app has no main nib)", dname ? dname : "?");
    app_delegate = new_object(c, dcls);
    if (rt_responds(app_delegate, "application:didFinishLaunchingWithOptions:")) {
        rt_log("uikit: -[%s application:didFinishLaunchingWithOptions:]", dname);
        rt_send(c, app_delegate, "application:didFinishLaunchingWithOptions:", 2, app_object, 0);
    } else if (rt_responds(app_delegate, "applicationDidFinishLaunching:")) {
        rt_log("uikit: -[%s applicationDidFinishLaunching:]", dname);
        rt_send(c, app_delegate, "applicationDidFinishLaunching:", 1, app_object);
    }
    rt_notify(c, "UIApplicationDidFinishLaunchingNotification", app_object, 0);
    layout_windows(c);
    if (rt_responds(app_delegate, "applicationDidBecomeActive:"))
        rt_send(c, app_delegate, "applicationDidBecomeActive:", 1, app_object);
    rt_notify(c, "UIApplicationDidBecomeActiveNotification", app_object, 0);
    pool_end(c, pool);
    rt_log("uikit: launch finished, entering the run loop");
    rt_platform_run(c);
    rt_ret(c, 0); /* only if the platform loop returns */
}

/* ---- UIDevice, UIScreen ------------------------------------------------------------------------------------------ */
M(dev_current) { rt_ret(c, device_object); }
M(dev_model) { rt_ret(c, rt_nsstring("iPad")); }
M(dev_name) { rt_ret(c, rt_nsstring("iPad")); }
M(dev_system_name) { rt_ret(c, rt_nsstring("iPhone OS")); }
M(dev_system_version) { rt_ret(c, rt_nsstring("3.2")); }
M(dev_udid) { rt_ret(c, rt_nsstring("0123456789abcdef0123456789abcdef01234567")); }
M(dev_idiom) { rt_ret(c, 1); } /* UIUserInterfaceIdiomPad */
M(dev_orientation) { rt_ret(c, status_bar_orientation); } /* UIDeviceOrientationLandscapeLeft = 3 = interface LandscapeRight */
M(dev_battery) { rt_fret(c, -1.0f); } /* monitoring off */
M(scr_main) { rt_ret(c, screen_object); }
M(scr_bounds) { float f[4] = {0, 0, SCREEN_W, SCREEN_H}; put_floats(OUT, f, 4); }
M(scr_app_frame) {
    float f[4] = {0, 0, SCREEN_W, SCREEN_H};
    if (!status_bar_hidden) {
        if (status_bar_orientation == 3) f[2] -= 20;
        else if (status_bar_orientation == 4) { f[0] = 20; f[2] -= 20; }
        else { f[1] = 20; f[3] -= 20; }
    }
    put_floats(OUT, f, 4);
}
M(scr_scale) { rt_fret(c, 1.0f); }

/* ---- UIView -------------------------------------------------------------------------------------------------- */
static void view_setup(id v, float x, float y, float w, float h) {
    ui_state *s = st(v);
    set_frame(s, x, y, w, h);
    s->needs_layout = 1;
}

M(view_init_frame) { /* initWithFrame:(CGRect) in four argument words */
    if (SELF) view_setup(SELF, fw(ARG(0)), fw(ARG(1)), fw(ARG(2)), fw(ARG(3)));
}
M(view_init) { rt_ret(c, rt_send(c, SELF, "initWithFrame:", 4, 0, 0, 0, 0)); }
M(view_frame) { float f[4]; frame_of(st(SSELF), f); put_floats(OUT, f, 4); }
M(view_bounds) { ui_state *s = st(SSELF); float f[4] = {s->bx, s->by, s->bw, s->bh}; put_floats(OUT, f, 4); }
M(view_center) { ui_state *s = st(SSELF); float f[2] = {s->cx, s->cy}; put_floats(OUT, f, 2); }
M(view_transform) { put_floats(OUT, st(SSELF)->t, 6); }
M(view_set_frame) {
    ui_state *s = st(SELF);
    float x = fw(ARG(0)), y = fw(ARG(1)), w = fw(ARG(2)), h = fw(ARG(3));
    if (s->has_t) { /* UIKit leaves this undefined; keep the transform and fit the bounding box's centre */
        s->cx = x + w / 2;
        s->cy = y + h / 2;
    } else set_frame(s, x, y, w, h);
}
M(view_set_bounds) {
    ui_state *s = st(SELF);
    float w = fw(ARG(2)), h = fw(ARG(3));
    if (w != s->bw || h != s->bh) s->needs_layout = 1;
    s->bx = fw(ARG(0)); s->by = fw(ARG(1)); s->bw = w; s->bh = h;
}
M(view_set_center) { ui_state *s = st(SELF); s->cx = fw(ARG(0)); s->cy = fw(ARG(1)); }
M(view_set_transform) {
    ui_state *s = st(SELF);
    for (int k = 0; k < 6; k++) s->t[k] = fw(ARG(k));
    s->has_t = memcmp(s->t, identity, sizeof identity) != 0;
}
M(view_layer_class) { rt_ret(c, cls_layer); }
M(view_layer) {
    ui_state *s = st(SELF);
    if (!s->layer) {
        id lc = rt_send(c, rt_object_class(SELF), "layerClass", 0);
        s->layer = new_object(c, lc ? lc : cls_layer);
        st(s->layer)->owner = SELF;
        prop_set(c, s->layer, k_delegate, SELF, 0);
    }
    rt_ret(c, s->layer);
}
M(view_add_subview) { insert_sub(c, SELF, ARG(0), ~0u); }
M(view_insert_at) { insert_sub(c, SELF, ARG(0), ARG(1)); }
M(view_insert_above) {
    ui_state *s = st(SELF);
    int i = sub_index(s, ARG(1));
    insert_sub(c, SELF, ARG(0), i >= 0 ? (uint32_t)i + 1 : ~0u);
}
M(view_insert_below) {
    ui_state *s = st(SELF);
    int i = sub_index(s, ARG(1));
    insert_sub(c, SELF, ARG(0), i >= 0 ? (uint32_t)i : ~0u);
}
M(view_remove) { remove_from_superview(c, SELF); }
M(view_bring_front) {
    ui_state *s = st(SELF);
    int i = sub_index(s, ARG(0));
    if (i < 0) return;
    id v = s->subs[i];
    memmove(s->subs + i, s->subs + i + 1, sizeof(id) * (s->nsubs - (uint32_t)i - 1));
    s->subs[s->nsubs - 1] = v;
}
M(view_send_back) {
    ui_state *s = st(SELF);
    int i = sub_index(s, ARG(0));
    if (i < 0) return;
    id v = s->subs[i];
    memmove(s->subs + 1, s->subs, sizeof(id) * (uint32_t)i);
    s->subs[0] = v;
}
M(view_exchange) {
    ui_state *s = st(SELF);
    uint32_t a = ARG(0), b = ARG(1);
    if (a < s->nsubs && b < s->nsubs) { id t = s->subs[a]; s->subs[a] = s->subs[b]; s->subs[b] = t; }
}
M(view_subviews) { ui_state *s = st(SELF); rt_ret(c, rt_autorelease(rt_nsarray_new(s->subs, s->nsubs))); }
M(view_superview) { ui_state *s = peek(SELF); rt_ret(c, s ? s->superview : 0); }
M(view_window) { rt_ret(c, window_of(SELF)); }
static id with_tag(id v, uint32_t tag, int depth) {
    if (prop_get(v, k_tag) == tag) return v;
    ui_state *s = peek(v);
    if (!s || depth > 64) return 0;
    for (uint32_t k = 0; k < s->nsubs; k++) {
        id r = with_tag(s->subs[k], tag, depth + 1);
        if (r) return r;
    }
    return 0;
}
M(view_with_tag) { rt_ret(c, with_tag(SELF, ARG(0), 0)); }
M(view_is_descendant) {
    id v = SELF;
    while (v && v != ARG(0)) v = peek(v) ? peek(v)->superview : 0;
    rt_ret(c, v != 0);
}
M(view_set_needs_layout) { st(SELF)->needs_layout = 1; }
M(view_layout_if_needed) { layout_tree(c, SELF, 0); }
M(view_size_that_fits) { ui_state *s = st(SSELF); float f[2] = {s->bw, s->bh}; put_floats(OUT, f, 2); }
/* convertPoint:toView: / convertPoint:fromView: (stret: x, y, view); nil is the window */
static void convert_point(cpu_t *c, int to) {
    id self = SSELF, other = SARG(2);
    float x = fw(SARG(0)), y = fw(SARG(1)), rx, ry;
    if (!other) other = window_of(self);
    if (to) { view_to_root(self, x, y, &rx, &ry); root_to_view(other, rx, ry, &x, &y); }
    else { view_to_root(other, x, y, &rx, &ry); root_to_view(self, rx, ry, &x, &y); }
    float f[2] = {x, y};
    put_floats(OUT, f, 2);
}
M(view_convert_to) { convert_point(c, 1); }
M(view_convert_from) { convert_point(c, 0); }
static void convert_rect(cpu_t *c, int to) { /* origin only; sizes are taken as unscaled */
    id self = SSELF, other = SARG(4);
    float x = fw(SARG(0)), y = fw(SARG(1)), rx, ry;
    if (!other) other = window_of(self);
    if (to) { view_to_root(self, x, y, &rx, &ry); root_to_view(other, rx, ry, &x, &y); }
    else { view_to_root(other, x, y, &rx, &ry); root_to_view(self, rx, ry, &x, &y); }
    float f[4] = {x, y, fw(SARG(2)), fw(SARG(3))};
    put_floats(OUT, f, 4);
}
M(view_convert_rect_to) { convert_rect(c, 1); }
M(view_convert_rect_from) { convert_rect(c, 0); }
M(view_point_inside) {
    ui_state *s = st(SELF);
    float x = fw(ARG(0)), y = fw(ARG(1));
    rt_ret(c, x >= s->bx && y >= s->by && x < s->bx + s->bw && y < s->by + s->bh);
}
M(view_hit_test) { /* hitTest:withEvent: with the point in the view's own space */
    ui_state *s = st(SELF);
    float px, py;
    from_local(s, fw(ARG(0)), fw(ARG(1)), &px, &py);
    rt_ret(c, hit_test(SELF, px, py));
}

/* UIView animations: they finish at once; the did-stop selector runs on the next run loop turn */
static id anim_delegate, anim_id;
static uint32_t anim_context;
static SEL anim_stop_sel;
M(anim_begin) { anim_id = ARG(0); anim_context = ARG(1); anim_delegate = 0; anim_stop_sel = 0; }
M(anim_set_delegate) { anim_delegate = ARG(0); }
M(anim_set_stop) { anim_stop_sel = ARG(0); }
M(anim_commit) {
    if (anim_delegate && anim_stop_sel) { /* animationDidStop:(id)animationID finished:(NSNumber *)YES context: */
        id finished = rt_nsnumber_new_bool(1);
        defer(DEFER_SEND, anim_delegate, rt_sel_name(anim_stop_sel), 3, anim_id, finished, anim_context, finished, anim_id);
        rt_release(c, finished);
    }
    anim_delegate = 0;
    anim_stop_sel = 0;
}

/* ---- UIWindow -------------------------------------------------------------------------------------------------- */
static void add_window(id w) {
    for (int k = 0; k < nwindows; k++)
        if (windows[k] == w) return;
    if (nwindows < MAX_WINDOWS) windows[nwindows++] = rt_retain(w);
}
M(win_make_key_visible) {
    add_window(SELF);
    key_window = SELF;
    prop_set(c, SELF, k_hidden, 0, 0);
    st(SELF)->needs_layout = 1;
}
M(win_make_key) { add_window(SELF); key_window = SELF; }
M(win_is_key) { rt_ret(c, key_window == SELF); }
M(win_root_vc) { rt_ret(c, prop_get(SELF, rt_sel("rootViewController"))); }
static void show_controller_in(cpu_t *c, id vc, id container, int animated);
M(win_set_root_vc) {
    id vc = ARG(0);
    prop_set(c, SELF, rt_sel("rootViewController"), vc, 1);
    if (vc) show_controller_in(c, vc, SELF, 0);
}

/* ---- UIControl, UIButton ------------------------------------------------------------------------------------------ */
static int state_slot(uint32_t state) { return state & 4 ? 3 : state & 2 ? 2 : state & 1 ? 1 : 0; }

M(ctl_add_target) {
    ui_state *s = st(SELF);
    if (s->nacts == s->capacts) {
        s->capacts = s->capacts ? s->capacts * 2 : 4;
        s->acts = realloc(s->acts, sizeof(target_action) * s->capacts);
    }
    s->acts[s->nacts++] = (target_action){ARG(0), ARG(1), ARG(2)};
}
M(ctl_remove_target) {
    ui_state *s = st(SELF);
    for (uint32_t k = 0; k < s->nacts;)
        if ((!ARG(0) || s->acts[k].target == ARG(0)) && (!ARG(1) || s->acts[k].action == ARG(1)) && (s->acts[k].events & ARG(2))) {
            memmove(s->acts + k, s->acts + k + 1, sizeof(target_action) * (s->nacts - k - 1));
            s->nacts--;
        } else k++;
}
static void send_actions(cpu_t *c, id ctl, uint32_t events, id event) {
    ui_state *s = peek(ctl);
    if (!s) return;
    target_action acts[32];
    uint32_t n = s->nacts < 32 ? s->nacts : 32;
    memcpy(acts, s->acts, sizeof(target_action) * n);
    for (uint32_t k = 0; k < n; k++) {
        if (!(acts[k].events & events)) continue;
        id target = acts[k].target ? acts[k].target : first_responder;
        uint32_t imp = target ? rt_lookup(rt_object_class(target), acts[k].action) : 0;
        if (rt_verbose) rt_log("uikit: control action -[%s %s]", target ? rt_class_name(rt_object_class(target)) : "nil", rt_sel_name(acts[k].action));
        if (imp) rt_call(c, imp, 4, target, acts[k].action, ctl, event);
    }
}
M(ctl_send_actions) { send_actions(c, SELF, ARG(0), 0); }
M(ctl_state) {
    uint32_t v = 0;
    if (prop_bool(SELF, k_highlighted)) v |= 1;
    if (!prop_bool(SELF, k_enabled)) v |= 2;
    if (prop_bool(SELF, k_selected)) v |= 4;
    rt_ret(c, v);
}
static int touch_inside(id ctl, id set) {
    ui_state *ts = peek(set);
    if (!ts || !ts->nitems) return 0;
    ui_state *t = peek(rt_ld32(ts->items));
    if (!t) return 0;
    float x, y;
    root_to_view(ctl, t->x, t->y, &x, &y);
    ui_state *s = st(ctl);
    return x >= s->bx - 20 && y >= s->by - 20 && x < s->bx + s->bw + 20 && y < s->by + s->bh + 20; /* UIKit's slop */
}
M(ctl_touches_began) {
    if (!prop_bool(SELF, k_enabled)) return;
    prop_set(c, SELF, k_highlighted, 1, 0);
    send_actions(c, SELF, UIControlEventTouchDown, ARG(1));
}
M(ctl_touches_moved) { if (prop_bool(SELF, k_enabled)) prop_set(c, SELF, k_highlighted, (uint32_t)touch_inside(SELF, ARG(0)), 0); }
M(ctl_touches_ended) {
    if (!prop_bool(SELF, k_enabled)) return;
    prop_set(c, SELF, k_highlighted, 0, 0);
    send_actions(c, SELF, touch_inside(SELF, ARG(0)) ? UIControlEventTouchUpInside : UIControlEventTouchUpOutside, ARG(1));
}
M(ctl_touches_cancelled) { prop_set(c, SELF, k_highlighted, 0, 0); send_actions(c, SELF, UIControlEventTouchCancel, ARG(1)); }

M(btn_with_type) {
    id b = rt_send(c, rt_send(c, SELF, "alloc", 0), "initWithFrame:", 4, 0, 0, 0, 0);
    prop_set(c, b, rt_sel("buttonType"), ARG(0), 0);
    rt_ret(c, rt_autorelease(b));
}
static void set_state_obj(cpu_t *c, id *slots, id v, uint32_t state) {
    int k = state_slot(state);
    rt_retain(v);
    rt_release(c, slots[k]);
    slots[k] = v;
}
static id state_obj(id *slots, uint32_t state) { id v = slots[state_slot(state)]; return v ? v : slots[0]; }
static uint32_t cur_state(id b) { return (prop_bool(b, k_highlighted) ? 1 : 0) | (prop_bool(b, k_enabled) ? 0 : 2) | (prop_bool(b, k_selected) ? 4 : 0); }
M(btn_set_title) {
    ui_state *s = st(SELF);
    set_state_obj(c, s->titles, ARG(0), ARG(1));
    id label = prop_get(SELF, rt_sel("titleLabel"));
    if (label && state_slot(ARG(1)) == 0) prop_set(c, label, k_text, ARG(0), 1);
}
M(btn_title_for) { rt_ret(c, state_obj(st(SELF)->titles, ARG(0))); }
M(btn_current_title) { rt_ret(c, state_obj(st(SELF)->titles, cur_state(SELF))); }
M(btn_set_image) { set_state_obj(c, st(SELF)->images, ARG(0), ARG(1)); }
M(btn_image_for) { rt_ret(c, state_obj(st(SELF)->images, ARG(0))); }
M(btn_current_image) { rt_ret(c, state_obj(st(SELF)->images, cur_state(SELF))); }
M(btn_set_bg) { set_state_obj(c, st(SELF)->backgrounds, ARG(0), ARG(1)); }
M(btn_bg_for) { rt_ret(c, state_obj(st(SELF)->backgrounds, ARG(0))); }
M(btn_title_label) {
    SEL key = rt_sel("titleLabel");
    id l = prop_get(SELF, key);
    if (!l) {
        ui_state *s = st(SELF);
        l = rt_send(c, rt_send(c, cls_label, "alloc", 0), "initWithFrame:", 4, 0, 0, wf(s->bw), wf(s->bh));
        prop_set(c, l, k_text, s->titles[0], 1);
        prop_set(c, SELF, key, l, 1);
        rt_release(c, l);
    }
    rt_ret(c, l);
}

/* ---- UIImageView -------------------------------------------------------------------------------------------------- */
M(iv_init_image) {
    id img = ARG(0);
    float sz[2] = {0, 0};
    if (img) {
        uint32_t g = rt_calloc(1, 8);
        send_stret(c, img, "size", g, 0, 0, 0);
        memcpy(sz, rt_mem + g, 8);
        rt_free(g);
    }
    view_setup(SELF, 0, 0, sz[0], sz[1]);
    prop_set(c, SELF, k_image, img, 1);
}
M(anim_start) { st(SELF)->animating = 1; }
M(anim_stop) { st(SELF)->animating = 0; }
M(anim_is) { ui_state *s = peek(SELF); rt_ret(c, s ? (uint32_t)s->animating : 0); }

/* ---- UIActivityIndicatorView ----------------------------------------------------------------------------------------- */
M(act_init_style) {
    float d = ARG(0) == 0 ? 37.0f : 20.0f; /* UIActivityIndicatorViewStyleWhiteLarge is 37 points */
    view_setup(SELF, 0, 0, d, d);
    prop_set(c, SELF, rt_sel("activityIndicatorViewStyle"), ARG(0), 0);
    prop_set(c, SELF, k_hidden, 1, 0); /* hidesWhenStopped */
}
M(act_start) { st(SELF)->animating = 1; prop_set(c, SELF, k_hidden, 0, 0); }
M(act_stop) { st(SELF)->animating = 0; if (prop_bool(SELF, k_hides_stopped)) prop_set(c, SELF, k_hidden, 1, 0); }

/* ---- UIAlertView ------------------------------------------------------------------------------------------------- */
static int alert_add(cpu_t *c, id a, id title) {
    ui_state *s = st(a);
    if (!title || s->nbuttons >= 8) return -1;
    s->buttons[s->nbuttons] = rt_retain(title);
    (void)c;
    return s->nbuttons++;
}
/* initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:, ... (nil-terminated) */
M(alert_init) {
    id self = SELF;
    view_setup(self, 0, 0, 284, 141);
    prop_set(c, self, k_title, ARG(0), 1);
    prop_set(c, self, k_message, ARG(1), 1);
    prop_set(c, self, k_delegate, ARG(2), 0);
    ui_state *s = st(self);
    s->cancel_index = -1;
    if (ARG(3)) s->cancel_index = alert_add(c, self, ARG(3));
    for (int k = 4; k < 12; k++) {
        id t = ARG(k);
        if (!t) break;
        alert_add(c, self, t);
    }
    rt_ret(c, self);
}
M(alert_add_button) { rt_ret(c, (uint32_t)alert_add(c, SELF, ARG(0))); }
M(alert_count) { ui_state *s = peek(SELF); rt_ret(c, s ? (uint32_t)s->nbuttons : 0); }
M(alert_cancel_index) { ui_state *s = peek(SELF); rt_ret(c, s ? (uint32_t)s->cancel_index : ~0u); }
M(alert_title_at) { ui_state *s = peek(SELF); int i = (int)ARG(0); rt_ret(c, s && i >= 0 && i < s->nbuttons ? s->buttons[i] : 0); }
M(alert_show) {
    ui_state *s = st(SELF);
    rt_log("uikit: alert \"%s\": \"%s\" (%d button%s)", utf8(prop_get(SELF, k_title)), utf8(prop_get(SELF, k_message)),
           s->nbuttons, s->nbuttons == 1 ? "" : "s");
    prop_set(c, SELF, rt_sel("visible"), 1, 0);
    id d = prop_get(SELF, k_delegate);
    if (s->nbuttons == 1 && d) {
        rt_log("uikit: alert answered with its only button (\"%s\")", utf8(s->buttons[0]));
        defer(DEFER_SEND, d, "alertView:clickedButtonAtIndex:", 2, SELF, 0, 0, SELF, 0);
        defer(DEFER_SEND, d, "alertView:didDismissWithButtonIndex:", 2, SELF, 0, 0, SELF, 0);
    } else if (s->nbuttons > 1) log_once("an alert with several buttons stays up (nobody to answer it)");
}
M(alert_dismiss) { /* dismissWithClickedButtonIndex:animated: */
    prop_set(c, SELF, rt_sel("visible"), 0, 0);
    id d = prop_get(SELF, k_delegate);
    if (d) defer(DEFER_SEND, d, "alertView:didDismissWithButtonIndex:", 2, SELF, ARG(0), 0, SELF, 0);
}

/* ---- UITextField, UITextView ---------------------------------------------------------------------------------------- */
M(tf_text) {
    id t = prop_get(SELF, k_text);
    rt_ret(c, t ? t : rt_nsstring(""));
}
M(tf_become) {
    id d = prop_get(SELF, k_delegate);
    if (first_responder == SELF) { rt_ret(c, 1); return; }
    if (d && rt_responds(d, "textFieldShouldBeginEditing:") && !(rt_send(c, d, "textFieldShouldBeginEditing:", 1, SELF) & 0xff)) {
        rt_ret(c, 0);
        return;
    }
    first_responder = SELF;
    prop_set(c, SELF, k_editing, 1, 0);
    if (d && rt_responds(d, "textFieldDidBeginEditing:")) rt_send(c, d, "textFieldDidBeginEditing:", 1, SELF);
    rt_notify(c, "UITextFieldTextDidBeginEditingNotification", SELF, 0);
    ui_state *s = st(SELF);
    if (!s->pending_return) {
        s->pending_return = 1;
        rt_log("uikit: text field editing (no keyboard: it returns on the next run loop turn)");
        defer(DEFER_TEXT_RETURN, SELF, NULL, 0, 0, 0, 0, 0, 0);
    }
    rt_ret(c, 1);
}
M(tf_resign) {
    id d = prop_get(SELF, k_delegate);
    if (first_responder != SELF) { rt_ret(c, 1); return; }
    if (d && rt_responds(d, "textFieldShouldEndEditing:") && !(rt_send(c, d, "textFieldShouldEndEditing:", 1, SELF) & 0xff)) {
        rt_ret(c, 0);
        return;
    }
    first_responder = 0;
    prop_set(c, SELF, k_editing, 0, 0);
    if (d && rt_responds(d, "textFieldDidEndEditing:")) rt_send(c, d, "textFieldDidEndEditing:", 1, SELF);
    rt_notify(c, "UITextFieldTextDidEndEditingNotification", SELF, 0);
    rt_ret(c, 1);
}
/* the player "hits return": optional text from NOVAHD_TEXT_INPUT, then textFieldShouldReturn: (or a resign) */
static void text_return(cpu_t *c, id field) {
    ui_state *s = peek(field);
    if (!s) return;
    s->pending_return = 0;
    if (first_responder != field) return;
    const char *typed = getenv("NOVAHD_TEXT_INPUT");
    id cur = prop_get(field, k_text);
    if (typed && *typed && rt_nsstring_len(cur) == 0) {
        prop_set(c, field, k_text, rt_nsstring(typed), 1);
        rt_notify(c, "UITextFieldTextDidChangeNotification", field, 0);
        send_actions(c, field, UIControlEventEditingChanged, 0);
    }
    rt_log("uikit: text field returns with \"%s\"", utf8(prop_get(field, k_text)));
    id d = prop_get(field, k_delegate);
    if (d && rt_responds(d, "textFieldShouldReturn:")) rt_send(c, d, "textFieldShouldReturn:", 1, field);
    else rt_send(c, field, "resignFirstResponder", 0);
}
M(tv_become) {
    id d = prop_get(SELF, k_delegate);
    if (d && rt_responds(d, "textViewShouldBeginEditing:") && !(rt_send(c, d, "textViewShouldBeginEditing:", 1, SELF) & 0xff)) {
        rt_ret(c, 0);
        return;
    }
    first_responder = SELF;
    prop_set(c, SELF, k_editing, 1, 0);
    if (d && rt_responds(d, "textViewDidBeginEditing:")) rt_send(c, d, "textViewDidBeginEditing:", 1, SELF);
    log_once("text view editing (no keyboard: its text stays as the game set it)");
    rt_ret(c, 1);
}
M(tv_resign) {
    id d = prop_get(SELF, k_delegate);
    if (first_responder == SELF) {
        first_responder = 0;
        prop_set(c, SELF, k_editing, 0, 0);
        if (d && rt_responds(d, "textViewDidEndEditing:")) rt_send(c, d, "textViewDidEndEditing:", 1, SELF);
    }
    rt_ret(c, 1);
}

/* ---- UIScrollView: contentOffset is the bounds origin ------------------------------------------------------------- */
M(sv_offset) { ui_state *s = st(SSELF); float f[2] = {s->bx, s->by}; put_floats(OUT, f, 2); }
M(sv_set_offset) { /* setContentOffset:(animated:) */
    ui_state *s = st(SELF);
    s->bx = fw(ARG(0));
    s->by = fw(ARG(1));
    id d = prop_get(SELF, k_delegate);
    if (d && rt_responds(d, "scrollViewDidScroll:")) rt_send(c, d, "scrollViewDidScroll:", 1, SELF);
}
M(sv_size) { ui_state *s = st(SSELF); float f[2] = {s->content_w, s->content_h}; put_floats(OUT, f, 2); }
M(sv_set_size) { ui_state *s = st(SELF); s->content_w = fw(ARG(0)); s->content_h = fw(ARG(1)); }

/* ---- UIViewController ---------------------------------------------------------------------------------------------- */
static void vc_appear(cpu_t *c, id vc, int will, int appear, int animated) {
    static const char *const sels[2][2] = {{"viewDidDisappear:", "viewWillDisappear:"}, {"viewDidAppear:", "viewWillAppear:"}};
    rt_send(c, vc, sels[appear][will], 1, (uint32_t)animated);
}

static id vc_view_of(cpu_t *c, id vc) { return rt_send(c, vc, "view", 0); }

/* put a controller's view into a container view (a window or a parent's view), with the appearance callbacks */
static void show_controller_in(cpu_t *c, id vc, id container, int animated) {
    id v = vc_view_of(c, vc);
    if (!v || !container) return;
    ui_state *vs = st(v), *cs = st(container);
    if (vs->bw == 0 && vs->bh == 0) set_frame(vs, 0, 0, cs->bw, cs->bh);
    vc_appear(c, vc, 1, 1, animated);
    insert_sub(c, container, v, ~0u);
    vc_appear(c, vc, 0, 1, animated);
}

static void hide_controller(cpu_t *c, id vc, int animated) {
    ui_state *s = peek(vc);
    if (!s || !s->view || !peek(s->view) || !peek(s->view)->superview) return;
    vc_appear(c, vc, 1, 0, animated);
    remove_from_superview(c, s->view);
    vc_appear(c, vc, 0, 0, animated);
}

static void container_show_current(cpu_t *c, id vc);

M(vc_init) { st(SELF)->selected = 0; } /* init and initWithNibName:bundle: (no nibs in this app) */
M(vc_view) {
    ui_state *s = st(SELF);
    if (!s->view && !s->loading) {
        s->loading = 1;
        rt_send(c, SELF, "loadView", 0);
        s->loading = 0;
        if (!s->view) { log_once("a view controller's loadView made no view: using an empty one"); }
        if (s->view) {
            rt_send(c, SELF, "viewDidLoad", 0);
            container_show_current(c, SELF);
        }
    }
    rt_ret(c, s->view);
}
M(vc_load_view) {
    float f[4] = {0, 0, SCREEN_W, SCREEN_H};
    id v = rt_send(c, rt_send(c, cls_view, "alloc", 0), "initWithFrame:", 4, wf(f[0]), wf(f[1]), wf(f[2]), wf(f[3]));
    rt_send(c, SELF, "setView:", 1, v);
    rt_release(c, v);
}
M(vc_set_view) {
    ui_state *s = st(SELF);
    id v = ARG(0), old = s->view;
    if (v == old) return;
    rt_retain(v);
    if (old && peek(old) && peek(old)->vc == SELF) peek(old)->vc = 0;
    s->view = v;
    if (v) st(v)->vc = SELF;
    rt_release(c, old);
}
M(vc_is_loaded) { ui_state *s = peek(SELF); rt_ret(c, s && s->view); }
M(vc_orientation) { rt_ret(c, status_bar_orientation); }
M(vc_autorotate) { rt_ret(c, ARG(0) == 1); } /* UIViewController's default: portrait only */
M(vc_present_modal) {
    ui_state *s = st(SELF);
    id mvc = ARG(0);
    if (!mvc || s->modal) return;
    s->modal = rt_retain(mvc);
    st(mvc)->presenter = SELF;
    id win = key_window ? key_window : window_of(s->view);
    if (rt_verbose) rt_log("uikit: present modal %s", rt_class_name(rt_object_class(mvc)));
    show_controller_in(c, mvc, win, ARG(1) & 0xff);
}
M(vc_dismiss_modal) {
    id self = SELF;
    ui_state *s = st(self);
    if (!s->modal && s->presenter) { self = s->presenter; s = st(self); } /* sent to the modal controller itself */
    id mvc = s->modal;
    if (!mvc) return;
    hide_controller(c, mvc, ARG(0) & 0xff);
    st(mvc)->presenter = 0;
    s->modal = 0;
    rt_autorelease(mvc);
}
M(vc_modal) { ui_state *s = peek(SELF); rt_ret(c, s ? s->modal : 0); }
M(vc_parent) { ui_state *s = peek(SELF); rt_ret(c, s ? (s->parent ? s->parent : s->presenter) : 0); }
static id ancestor_of_kind(id vc, id cls) {
    for (int k = 0; vc && k < 32; k++) {
        ui_state *s = peek(vc);
        vc = s ? s->parent : 0;
        if (vc && rt_is_kind(vc, cls)) return vc;
    }
    return 0;
}
M(vc_nav) { rt_ret(c, ancestor_of_kind(SELF, cls_nav)); }
M(vc_tab) { rt_ret(c, ancestor_of_kind(SELF, cls_tab)); }

/* ---- UINavigationController, UITabBarController: inert but consistent ------------------------------------------------ */
static void kids_add(id vc, id child) {
    ui_state *s = st(vc);
    if (s->nkids == s->capkids) {
        s->capkids = s->capkids ? s->capkids * 2 : 4;
        s->kids = realloc(s->kids, sizeof(id) * s->capkids);
    }
    s->kids[s->nkids++] = rt_retain(child);
    st(child)->parent = vc;
}
static id current_child(id vc) {
    ui_state *s = peek(vc);
    if (!s || !s->nkids) return 0;
    if (rt_is_kind(vc, cls_tab)) return s->selected < s->nkids ? s->kids[s->selected] : 0;
    return s->kids[s->nkids - 1];
}
static void container_show_current(cpu_t *c, id vc) {
    ui_state *s = peek(vc);
    id child = current_child(vc);
    if (s && s->view && child) show_controller_in(c, child, s->view, 0);
}
static void kids_clear(cpu_t *c, id vc) {
    ui_state *s = st(vc);
    id cur = current_child(vc);
    if (cur && s->view) hide_controller(c, cur, 0);
    for (uint32_t k = 0; k < s->nkids; k++) { st(s->kids[k])->parent = 0; rt_release(c, s->kids[k]); }
    s->nkids = 0;
    s->selected = 0;
}
static void kids_set_array(cpu_t *c, id vc, id array) {
    kids_clear(c, vc);
    for (uint32_t k = 0, n = rt_nsarray_count(array); k < n; k++) kids_add(vc, rt_nsarray_at(array, k));
    container_show_current(c, vc);
}

M(nav_init_root) {
    rt_send(c, SELF, "initWithNibName:bundle:", 2, 0, 0);
    if (ARG(0)) kids_add(SELF, ARG(0));
    rt_ret(c, SELF);
}
M(nav_push) {
    ui_state *s = st(SELF);
    id old = current_child(SELF), child = ARG(0);
    if (!child) return;
    if (old && s->view) hide_controller(c, old, ARG(1) & 0xff);
    kids_add(SELF, child);
    if (s->view) show_controller_in(c, child, s->view, ARG(1) & 0xff);
}
static id nav_pop_one(cpu_t *c, id nav, int animated) {
    ui_state *s = st(nav);
    if (s->nkids <= 1) return 0;
    id top = s->kids[--s->nkids];
    if (s->view) hide_controller(c, top, animated);
    st(top)->parent = 0;
    rt_autorelease(top);
    if (s->view) show_controller_in(c, s->kids[s->nkids - 1], s->view, animated);
    return top;
}
M(nav_pop) { rt_ret(c, nav_pop_one(c, SELF, ARG(0) & 0xff)); }
M(nav_pop_root) {
    id popped[64];
    uint32_t n = 0;
    id p;
    while (n < 64 && (p = nav_pop_one(c, SELF, ARG(0) & 0xff))) popped[n++] = p;
    rt_ret(c, rt_autorelease(rt_nsarray_new(popped, n)));
}
M(nav_pop_to) {
    id popped[64];
    uint32_t n = 0;
    id p;
    while (n < 64 && current_child(SELF) && current_child(SELF) != ARG(0) && (p = nav_pop_one(c, SELF, ARG(1) & 0xff))) popped[n++] = p;
    rt_ret(c, rt_autorelease(rt_nsarray_new(popped, n)));
}
M(nav_vcs) { ui_state *s = st(SELF); rt_ret(c, rt_autorelease(rt_nsarray_new(s->kids, s->nkids))); }
M(nav_set_vcs) { kids_set_array(c, SELF, ARG(0)); } /* setViewControllers:(animated:) */
M(nav_top) { rt_ret(c, current_child(SELF)); }
M(nav_visible) {
    id top = current_child(SELF);
    ui_state *s = peek(top);
    rt_ret(c, s && s->modal ? s->modal : top);
}
M(tab_selected_vc) { rt_ret(c, current_child(SELF)); }
M(tab_selected_index) { rt_ret(c, st(SELF)->selected); }
M(tab_set_index) {
    ui_state *s = st(SELF);
    uint32_t i = ARG(0);
    if (i >= s->nkids || i == s->selected) return;
    id old = current_child(SELF);
    if (old && s->view) hide_controller(c, old, 0);
    s->selected = i;
    container_show_current(c, SELF);
}
M(tab_set_vc) {
    ui_state *s = st(SELF);
    for (uint32_t k = 0; k < s->nkids; k++)
        if (s->kids[k] == ARG(0)) {
            c->r[2] = k; /* reuse setSelectedIndex: */
            tab_set_index(c, m);
            return;
        }
}

/* ---- UIPopoverController, UIImagePickerController ------------------------------------------------------------------- */
M(pop_init) { prop_set(c, SELF, rt_sel("contentViewController"), ARG(0), 1); }
M(pop_present) { /* presentPopoverFromRect:inView:permittedArrowDirections:animated: (rect in 4 words) */
    id vc = prop_get(SELF, rt_sel("contentViewController"));
    id win = key_window ? key_window : window_of(ARG(4));
    prop_set(c, SELF, rt_sel("popoverVisible"), 1, 0);
    if (vc && win) show_controller_in(c, vc, win, 0);
}
M(pop_dismiss) {
    id vc = prop_get(SELF, rt_sel("contentViewController"));
    prop_set(c, SELF, rt_sel("popoverVisible"), 0, 0);
    if (vc) hide_controller(c, vc, 0);
}
M(picker_available) { log_once("UIImagePickerController: no photo library or camera"); rt_ret(c, 0); }

/* ---- CALayer, CAEAGLLayer --------------------------------------------------------------------------------------- */
M(layer_bounds) {
    ui_state *s = st(SSELF);
    if (s->owner) { ui_state *v = st(s->owner); float f[4] = {v->bx, v->by, v->bw, v->bh}; put_floats(OUT, f, 4); }
    else put_floats(OUT, s->lb, 4);
}
M(layer_frame) {
    ui_state *s = st(SSELF);
    if (s->owner) { float f[4]; frame_of(st(s->owner), f); put_floats(OUT, f, 4); }
    else put_floats(OUT, s->lb, 4);
}
M(layer_position) {
    ui_state *s = st(SSELF);
    float f[2] = {s->lb[0] + s->lb[2] / 2, s->lb[1] + s->lb[3] / 2};
    if (s->owner) { f[0] = st(s->owner)->cx; f[1] = st(s->owner)->cy; }
    put_floats(OUT, f, 2);
}
M(layer_set_bounds) {
    ui_state *s = st(SELF);
    if (s->owner) { rt_send(c, s->owner, "setBounds:", 4, ARG(0), ARG(1), ARG(2), ARG(3)); return; }
    for (int k = 0; k < 4; k++) s->lb[k] = fw(ARG(k));
}
M(layer_set_frame) {
    ui_state *s = st(SELF);
    if (s->owner) { rt_send(c, s->owner, "setFrame:", 4, ARG(0), ARG(1), ARG(2), ARG(3)); return; }
    for (int k = 0; k < 4; k++) s->lb[k] = fw(ARG(k));
}
M(layer_render) { log_once("-[CALayer renderInContext:] draws nothing (views are not composited)"); }

/* ---- EAGLContext ----------------------------------------------------------------------------------------------------- */
/* the game's view of a GL integer: the glGetIntegerv import (gles1.c tracks the game's bindings), else the host */
static uint32_t gl_get_integer(cpu_t *c, uint32_t pname) {
    static uint32_t slot = ~1u;
    if (slot == ~1u) slot = rt_import_slot("glGetIntegerv");
    uint32_t out = rt_calloc(1, 16), v = 0;
    if (slot != ~0u) {
        cpu_t saved = *c;
        c->r[0] = pname;
        c->r[1] = out;
        rc_import(c, rt_mem, slot);
        *c = saved;
        v = rt_ld32(out);
    } else if (rt_gl_getproc) {
        void (GLAPI *f)(uint32_t, int32_t *) = (void (GLAPI *)(uint32_t, int32_t *))rt_gl_getproc("glGetIntegerv");
        int32_t i = 0;
        if (f) f(pname, &i);
        v = (uint32_t)i;
    }
    rt_free(out);
    return v;
}

M(eagl_init_api) { /* initWithAPI: and initWithAPI:sharegroup: */
    uint32_t api = ARG(0);
    if (api != 1) {
        rt_log("uikit: EAGLContext initWithAPI:%u -> nil (OpenGL ES 1 only)", api);
        rt_release(c, SELF);
        rt_ret(c, 0);
        return;
    }
    st(SELF)->api = 1;
    rt_log("uikit: EAGLContext initWithAPI:1 (OpenGL ES 1.1)");
}
M(eagl_api) { ui_state *s = peek(SELF); rt_ret(c, s ? (uint32_t)s->api : 0); }
M(eagl_set_current) {
    id ctx = ARG(0);
    if (ctx) rt_platform_make_current(); /* the EGL context follows the thread that makes the game's context current */
    if (ctx != current_context) {
        rt_retain(ctx);
        rt_release(c, current_context);
        current_context = ctx;
    }
    rt_ret(c, 1);
}
M(eagl_current) { rt_ret(c, current_context); }
/* renderbufferStorage:fromDrawable: the renderbuffer bound now becomes the window surface */
M(eagl_storage) {
    uint32_t rb = gl_get_integer(c, 0x8CA7 /* GL_RENDERBUFFER_BINDING_OES */);
    int w = (int)SCREEN_W, h = (int)SCREEN_H; /* the layer is the portrait 768x1024; gles1.c shows it turned a quarter turn */
    static int logged;
    if (!logged++) rt_log("uikit: renderbufferStorage:%#x fromDrawable:%s -> renderbuffer %u is the window (%dx%d)", ARG(0),
                          rt_class_name(rt_object_class(ARG(1))), rb, w, h);
    rt_gl_bind_drawable(rb, w, h);
    rt_ret(c, 1);
}
M(eagl_present) { rt_gl_present(); rt_ret(c, 1); }

/* ---- UIAccelerometer, UIAcceleration ---------------------------------------------------------------------------------- */
M(acc_shared) { rt_ret(c, accel_object); }
M(acc_set_delegate) { accel_delegate = ARG(0); }
M(acc_delegate) { rt_ret(c, accel_delegate); }
M(acc_set_interval) {
    double d = rt_darg(c, 2);
    st(SELF)->interval = d > 0.01 ? d : 0.01;
}
M(acc_interval) { rt_dret(c, st(SELF)->interval); }
M(acn_x) { rt_dret(c, st(SELF)->ax); }
M(acn_y) { rt_dret(c, st(SELF)->ay); }
M(acn_z) { rt_dret(c, st(SELF)->az); }
M(acn_timestamp) { rt_dret(c, st(SELF)->ts); }

/* ---- UITouch, UIEvent, the touch set ------------------------------------------------------------------------------------ */
M(touch_location) { /* locationInView: (stret) */
    ui_state *t = st(SSELF);
    float f[2] = {t->x, t->y};
    if (SARG(0)) root_to_view(SARG(0), t->x, t->y, &f[0], &f[1]);
    put_floats(OUT, f, 2);
}
M(touch_prev_location) {
    ui_state *t = st(SSELF);
    float f[2] = {t->px, t->py};
    if (SARG(0)) root_to_view(SARG(0), t->px, t->py, &f[0], &f[1]);
    put_floats(OUT, f, 2);
}
M(touch_phase) { rt_ret(c, (uint32_t)st(SELF)->phase); }
M(touch_taps) { rt_ret(c, (uint32_t)st(SELF)->taps); }
M(touch_timestamp) { rt_dret(c, st(SELF)->ts); }
M(touch_view) { rt_ret(c, st(SELF)->tview); }
M(touch_window) { rt_ret(c, st(SELF)->twin); }

/* a touch set: an immutable NSSet subclass whose elements live in a game array (for fast enumeration) */
static id touchset_new(cpu_t *c, const id *items, uint32_t n) {
    id s = rt_objc_alloc(cls_touchset);
    ui_state *ss = st(s);
    ss->items = rt_calloc(n ? n : 1, 4);
    ss->nitems = n;
    for (uint32_t k = 0; k < n; k++) rt_st32(ss->items + 4 * k, rt_retain(items[k]));
    (void)c;
    return s;
}
M(set_count) { ui_state *s = peek(SELF); rt_ret(c, s ? s->nitems : 0); }
M(set_any) { ui_state *s = peek(SELF); rt_ret(c, s && s->nitems ? rt_ld32(s->items) : 0); }
M(set_all) {
    ui_state *s = peek(SELF);
    id tmp[64];
    uint32_t n = s ? (s->nitems < 64 ? s->nitems : 64) : 0;
    for (uint32_t k = 0; k < n; k++) tmp[k] = rt_ld32(s->items + 4 * k);
    rt_ret(c, rt_autorelease(rt_nsarray_new(tmp, n)));
}
static int set_has(id set, id obj) {
    ui_state *s = peek(set);
    for (uint32_t k = 0; s && obj && k < s->nitems; k++)
        if (rt_ld32(s->items + 4 * k) == obj) return 1;
    return 0;
}
/* containsObject: is a BOOL (an object address can have a zero low byte, which would read as NO); member: is the object */
M(set_contains) { rt_ret(c, (uint32_t)set_has(SELF, ARG(0))); }
M(set_member) { rt_ret(c, set_has(SELF, ARG(0)) ? ARG(0) : 0); }
/* countByEnumeratingWithState:objects:count: state = {state, itemsPtr, mutationsPtr, extra[5]} */
M(set_enumerate) {
    ui_state *s = peek(SELF);
    uint32_t state = ARG(0);
    if (!s || rt_ld32(state) != 0) { rt_ret(c, 0); return; }
    rt_st32(state, 1);
    rt_st32(state + 4, s->items);
    rt_st32(state + 8, SELF); /* the isa word: never changes */
    rt_ret(c, s->nitems);
}
M(set_enumerator) {
    id all = rt_autorelease(rt_nsarray_new(NULL, 0));
    ui_state *s = peek(SELF);
    if (s && s->nitems) {
        id tmp[64];
        uint32_t n = s->nitems < 64 ? s->nitems : 64;
        for (uint32_t k = 0; k < n; k++) tmp[k] = rt_ld32(s->items + 4 * k);
        all = rt_autorelease(rt_nsarray_new(tmp, n));
    }
    rt_ret(c, rt_send(c, all, "objectEnumerator", 0));
}

static id fingers[MAX_FINGERS];
static double event_time, last_tap_time = -10;
static float last_tap_x, last_tap_y;
static int last_taps;

static uint32_t active_touches(id *out) {
    uint32_t n = 0;
    for (int k = 0; k < MAX_FINGERS; k++)
        if (fingers[k]) out[n++] = fingers[k];
    return n;
}
M(event_all) { id t[MAX_FINGERS]; uint32_t n = active_touches(t); rt_ret(c, rt_autorelease(touchset_new(c, t, n))); }
M(event_for_view) { /* touchesForView: and touchesForWindow: */
    id t[MAX_FINGERS], sel[MAX_FINGERS];
    uint32_t n = active_touches(t), k2 = 0;
    for (uint32_t k = 0; k < n; k++) {
        ui_state *ts = st(t[k]);
        if (ts->tview == ARG(0) || ts->twin == ARG(0)) sel[k2++] = t[k];
    }
    rt_ret(c, rt_autorelease(touchset_new(c, sel, k2)));
}
M(event_timestamp) { rt_dret(c, event_time); }
M(event_type) { rt_ret(c, 0); } /* UIEventTypeTouches */

/* ---- the platform layer's entry points ------------------------------------------------------------------------------ */
static void deliver(cpu_t *c, id touch, const char *sel) {
    ui_state *t = st(touch);
    if (!t->tview) return;
    id set = rt_autorelease(touchset_new(c, &touch, 1));
    if (rt_verbose > 1) rt_log("uikit: %s -> %s (%.0f, %.0f)", sel, rt_class_name(rt_object_class(t->tview)), t->x, t->y);
    rt_send(c, t->tview, sel, 2, set, event_object);
}

static const char *phase_sel(int phase) {
    switch (phase) {
    case RT_TOUCH_BEGAN: return "touchesBegan:withEvent:";
    case RT_TOUCH_MOVED: return "touchesMoved:withEvent:";
    case RT_TOUCH_ENDED: return "touchesEnded:withEvent:";
    default: return "touchesCancelled:withEvent:";
    }
}

/* UITouchPhase: began 0, moved 1, stationary 2, ended 3, cancelled 4 */
static int ui_phase(int phase) {
    return phase == RT_TOUCH_BEGAN ? 0 : phase == RT_TOUCH_MOVED ? 1 : phase == RT_TOUCH_ENDED ? 3 : 4;
}

static void finish_touch(cpu_t *c, int finger, int phase) {
    id t = fingers[finger];
    ui_state *ts = st(t);
    ts->phase = ui_phase(phase);
    ts->ts = event_time;
    deliver(c, t, phase_sel(phase));
    if (phase == RT_TOUCH_ENDED) { last_tap_time = event_time; last_tap_x = ts->x; last_tap_y = ts->y; last_taps = ts->taps; }
    fingers[finger] = 0;
    rt_release(c, t);
}

void rt_uikit_touch(cpu_t *c, int phase, int finger, float x, float y) {
    if (finger < 0 || finger >= MAX_FINGERS || !nwindows) return;
    id pool = pool_begin(c);
    event_time = rt_now();
    float sx = SCREEN_W - y, sy = x; /* landscape screen point (home button right) -> portrait screen point */
    if (phase == RT_TOUCH_BEGAN) {
        if (fingers[finger]) finish_touch(c, finger, RT_TOUCH_CANCELLED);
        id win = 0, view = 0;
        for (int k = nwindows; k-- > 0 && !view;) {
            if (prop_bool(windows[k], k_hidden)) continue;
            /* the far screen edges (x = 768 for a touch on the top row) still count as inside */
            view = hit_test(windows[k], fminf(sx, SCREEN_W - 0.01f), fminf(sy, SCREEN_H - 0.01f));
            if (view) win = windows[k];
        }
        if (!win) win = key_window ? key_window : windows[nwindows - 1];
        if (view && !prop_bool(view, k_multitouch)) /* a single-touch view takes no second finger */
            for (int k = 0; k < MAX_FINGERS; k++)
                if (fingers[k] && st(fingers[k])->tview == view) { view = 0; break; }
        id t = rt_objc_alloc(cls_touch);
        ui_state *ts = st(t);
        to_local(st(win), sx, sy, &ts->x, &ts->y);
        ts->px = ts->x;
        ts->py = ts->y;
        ts->tview = rt_retain(view);
        ts->twin = rt_retain(win);
        ts->taps = event_time - last_tap_time < 0.35 && fabsf(ts->x - last_tap_x) < 40 && fabsf(ts->y - last_tap_y) < 40 ? last_taps + 1 : 1;
        ts->phase = 0;
        ts->ts = event_time;
        fingers[finger] = t;
        deliver(c, t, phase_sel(phase));
    } else if (fingers[finger]) {
        ui_state *ts = st(fingers[finger]);
        if (phase != RT_TOUCH_CANCELLED) {
            float wx, wy;
            to_local(st(ts->twin), sx, sy, &wx, &wy);
            ts->px = ts->x;
            ts->py = ts->y;
            ts->x = wx;
            ts->y = wy;
        }
        if (phase == RT_TOUCH_MOVED) {
            ts->phase = 1;
            ts->ts = event_time;
            deliver(c, fingers[finger], phase_sel(phase));
        } else finish_touch(c, finger, phase);
    }
    pool_end(c, pool);
}

void rt_uikit_accelerometer(cpu_t *c, float x, float y, float z) {
    if (!accel_delegate || !rt_responds(accel_delegate, "accelerometer:didAccelerate:")) return;
    ui_state *s = st(accel_object);
    double now = rt_now();
    if (now - s->last < s->interval * 0.9) return;
    s->last = now;
    id pool = pool_begin(c);
    id a = rt_objc_alloc(cls_acceleration);
    ui_state *as = st(a);
    as->ax = x;
    as->ay = y;
    as->az = z;
    as->ts = now;
    rt_autorelease(a);
    rt_send(c, accel_delegate, "accelerometer:didAccelerate:", 2, accel_object, a);
    pool_end(c, pool);
}

/* one display frame on the main thread: the main run loop, deferred messages, layout */
void rt_uikit_frame(cpu_t *c) {
    id pool = pool_begin(c);
    layout_windows(c);
    rt_runloop_run_once(c);
    run_deferred(c);
#ifdef __ANDROID__
    rt_offline_music_tick(c);
#endif
    pool_end(c, pool);
}

void rt_uikit_pause(cpu_t *c) {
    id pool = pool_begin(c);
    rt_log("uikit: application will resign active");
    if (rt_responds(app_delegate, "applicationWillResignActive:")) rt_send(c, app_delegate, "applicationWillResignActive:", 1, app_object);
    rt_notify(c, "UIApplicationWillResignActiveNotification", app_object, 0);
    pool_end(c, pool);
}

void rt_uikit_resume(cpu_t *c) {
    id pool = pool_begin(c);
    rt_log("uikit: application did become active");
    if (rt_responds(app_delegate, "applicationDidBecomeActive:")) rt_send(c, app_delegate, "applicationDidBecomeActive:", 1, app_object);
    rt_notify(c, "UIApplicationDidBecomeActiveNotification", app_object, 0);
    pool_end(c, pool);
}

/* ---- data imports: NSString constants (the bound word gets the address of a word holding the string) ------------ */
static uint32_t string_word(const char *value) {
    uint32_t w = rt_calloc(1, 4);
    rt_st32(w, rt_nsstring_new(value));
    return w;
}

static uint32_t resolve(const char *name) {
    static struct { const char *name; uint32_t word; } made[32];
    static int nmade;
    const char *value = NULL;
    if (!strncmp(name, "kEAGL", 5)) value = name + 1; /* kEAGLColorFormatRGBA8 = @"EAGLColorFormatRGBA8", ... */
    else if (!strncmp(name, "UI", 2) && (strstr(name, "Notification") || strstr(name, "Key")) && !strchr(name, '$'))
        value = name; /* UIApplicationLaunchOptionsRemoteNotificationKey, UITextFieldTextDidChangeNotification, ... */
    if (!value) return 0;
    for (int k = 0; k < nmade; k++)
        if (!strcmp(made[k].name, name)) return made[k].word;
    uint32_t w = string_word(value);
    if (nmade < 32) { made[nmade].name = name; made[nmade].word = w; nmade++; }
    return w;
}

/* ---- class set-up ---------------------------------------------------------------------------------------------- */
static id def(const char *name, const char *super, uint32_t ivars) { return rt_objc_define(name, super, ivars); }
static void im(id cls, const char *sel, rt_hostfn fn) { rt_objc_method(cls, sel, fn); }
static void cm(id cls, const char *sel, rt_hostfn fn) { rt_objc_cmethod(cls, sel, fn); }
static void ims(id cls, const char *const *sels, size_t n, rt_hostfn fn) { for (size_t k = 0; k < n; k++) rt_objc_method(cls, sels[k], fn); }
#define IMS(cls, fn, ...) do { static const char *const l_[] = {__VA_ARGS__}; ims(cls, l_, sizeof l_ / sizeof *l_, fn); } while (0)

void rt_uikit_define(void) {
    k_hidden = rt_sel("hidden"); k_alpha = rt_sel("alpha"); k_user_interaction = rt_sel("userInteractionEnabled");
    k_multitouch = rt_sel("multipleTouchEnabled"); k_enabled = rt_sel("enabled"); k_highlighted = rt_sel("highlighted");
    k_selected = rt_sel("selected"); k_delegate = rt_sel("delegate"); k_text = rt_sel("text"); k_tag = rt_sel("tag");
    k_editing = rt_sel("editing"); k_title = rt_sel("title"); k_message = rt_sel("message"); k_image = rt_sel("image");
    k_hides_stopped = rt_sel("hidesWhenStopped");

    id responder = def("UIResponder", "NSObject", 0);
    cls_app = def("UIApplication", "UIResponder", 0);
    cls_view = def("UIView", "UIResponder", 32);              /* 36 bytes: EAGLView's instanceStart */
    cls_window = def("UIWindow", "UIView", 0);
    cls_control = def("UIControl", "UIView", 0);
    cls_button = def("UIButton", "UIControl", 88);            /* UIIconButton starts at 126 */
    cls_imageview = def("UIImageView", "UIView", 4);          /* 40: ScreenshotView's instanceStart */
    cls_label = def("UILabel", "UIView", 0);
    cls_scroll = def("UIScrollView", "UIView", 0);            /* offline.c derives UITableView, UIWebView from these */
    cls_textfield = def("UITextField", "UIControl", 0);
    cls_textview = def("UITextView", "UIScrollView", 0);
    cls_activity = def("UIActivityIndicatorView", "UIView", 0);
    cls_alert = def("UIAlertView", "UIView", 0);
    cls_vc = def("UIViewController", "UIResponder", 116);     /* 120: the game's controllers start there */
    cls_nav = def("UINavigationController", "UIViewController", 80); /* 200 */
    cls_tab = def("UITabBarController", "UIViewController", 0);
    id picker = def("UIImagePickerController", "UINavigationController", 0);
    cls_popover = def("UIPopoverController", "NSObject", 0);
    id device = def("UIDevice", "NSObject", 0);
    id screen = def("UIScreen", "NSObject", 0);
    id accel = def("UIAccelerometer", "NSObject", 0);
    cls_acceleration = def("UIAcceleration", "NSObject", 0);
    cls_touch = def("UITouch", "NSObject", 0);
    cls_event = def("UIEvent", "NSObject", 0);
    cls_touchset = def("__UITouchSet", rt_class("NSSet") ? "NSSet" : "NSObject", 0);
    cls_layer = def("CALayer", "NSObject", 0);
    cls_eagllayer = def("CAEAGLLayer", "CALayer", 0);
    cls_context = def("EAGLContext", "NSObject", 0);

    /* -dealloc frees the host state of every root class here */
    id roots[] = {responder, cls_popover, device, screen, accel, cls_acceleration, cls_touch, cls_event, cls_touchset,
                  cls_layer, cls_context};
    for (size_t k = 0; k < sizeof roots / sizeof *roots; k++) im(roots[k], "dealloc", ui_dealloc);

    /* UIResponder */
    im(responder, "nextResponder", r_next);
    IMS(responder, r_forward_touches, "touchesBegan:withEvent:", "touchesMoved:withEvent:", "touchesEnded:withEvent:",
        "touchesCancelled:withEvent:");
    IMS(responder, ret_zero, "motionBegan:withEvent:", "motionEnded:withEvent:", "motionCancelled:withEvent:",
        "canResignFirstResponder");
    im(responder, "canBecomeFirstResponder", ret_zero);
    im(responder, "becomeFirstResponder", r_become);
    im(responder, "resignFirstResponder", r_resign);
    im(responder, "isFirstResponder", r_is_first);

    /* UIApplication */
    cm(cls_app, "sharedApplication", app_shared);
    im(cls_app, "delegate", app_delegate_get);
    im(cls_app, "setDelegate:", app_delegate_set);
    im(cls_app, "statusBarOrientation", app_orientation);
    im(cls_app, "setStatusBarOrientation:", app_set_orientation);
    im(cls_app, "setStatusBarOrientation:animated:", app_set_orientation);
    im(cls_app, "setStatusBarHidden:", app_set_status_hidden);
    im(cls_app, "setStatusBarHidden:animated:", app_set_status_hidden);
    im(cls_app, "setStatusBarHidden:withAnimation:", app_set_status_hidden);
    im(cls_app, "isStatusBarHidden", app_status_hidden);
    im(cls_app, "statusBarFrame", app_status_frame);
    im(cls_app, "keyWindow", app_key_window);
    im(cls_app, "windows", app_windows);
    im(cls_app, "openURL:", app_open_url);
    im(cls_app, "canOpenURL:", ret_zero);
    im(cls_app, "registerForRemoteNotificationTypes:", app_register_notifications);
    im(cls_app, "unregisterForRemoteNotifications", ret_self);
    im(cls_app, "applicationState", app_state);
    IMS(cls_app, ret_self, "beginIgnoringInteractionEvents", "endIgnoringInteractionEvents");
    PROPS(cls_app, "setIdleTimerDisabled:", "isIdleTimerDisabled", "setApplicationIconBadgeNumber:",
          "applicationIconBadgeNumber", "setNetworkActivityIndicatorVisible:", "isNetworkActivityIndicatorVisible",
          "setStatusBarStyle:", "setStatusBarStyle:animated:", "statusBarStyle", "setApplicationSupportsShakeToEdit:");

    /* UIDevice, UIScreen */
    cm(device, "currentDevice", dev_current);
    im(device, "model", dev_model);
    im(device, "localizedModel", dev_model);
    im(device, "name", dev_name);
    im(device, "systemName", dev_system_name);
    im(device, "systemVersion", dev_system_version);
    im(device, "uniqueIdentifier", dev_udid);
    im(device, "userInterfaceIdiom", dev_idiom);
    im(device, "orientation", dev_orientation);
    im(device, "batteryLevel", dev_battery);
    im(device, "batteryState", ret_zero);
    im(device, "isMultitaskingSupported", ret_zero);
    IMS(device, ret_self, "beginGeneratingDeviceOrientationNotifications", "endGeneratingDeviceOrientationNotifications");
    PROPS(device, "setBatteryMonitoringEnabled:", "isBatteryMonitoringEnabled", "setProximityMonitoringEnabled:",
          "isProximityMonitoringEnabled", "proximityState", "isGeneratingDeviceOrientationNotifications");
    cm(screen, "mainScreen", scr_main);
    im(screen, "bounds", scr_bounds);
    im(screen, "applicationFrame", scr_app_frame);
    im(screen, "scale", scr_scale);

    /* UIView */
    im(cls_view, "init", view_init);
    im(cls_view, "initWithFrame:", view_init_frame);
    im(cls_view, "frame", view_frame);
    im(cls_view, "bounds", view_bounds);
    im(cls_view, "center", view_center);
    im(cls_view, "transform", view_transform);
    im(cls_view, "setFrame:", view_set_frame);
    im(cls_view, "setBounds:", view_set_bounds);
    im(cls_view, "setCenter:", view_set_center);
    im(cls_view, "setTransform:", view_set_transform);
    cm(cls_view, "layerClass", view_layer_class);
    im(cls_view, "layer", view_layer);
    im(cls_view, "addSubview:", view_add_subview);
    im(cls_view, "insertSubview:atIndex:", view_insert_at);
    im(cls_view, "insertSubview:aboveSubview:", view_insert_above);
    im(cls_view, "insertSubview:belowSubview:", view_insert_below);
    im(cls_view, "removeFromSuperview", view_remove);
    im(cls_view, "bringSubviewToFront:", view_bring_front);
    im(cls_view, "sendSubviewToBack:", view_send_back);
    im(cls_view, "exchangeSubviewAtIndex:withSubviewAtIndex:", view_exchange);
    im(cls_view, "subviews", view_subviews);
    im(cls_view, "superview", view_superview);
    im(cls_view, "window", view_window);
    im(cls_view, "viewWithTag:", view_with_tag);
    im(cls_view, "isDescendantOfView:", view_is_descendant);
    im(cls_view, "setNeedsLayout", view_set_needs_layout);
    im(cls_view, "layoutIfNeeded", view_layout_if_needed);
    im(cls_view, "sizeThatFits:", view_size_that_fits);
    im(cls_view, "convertPoint:toView:", view_convert_to);
    im(cls_view, "convertPoint:fromView:", view_convert_from);
    im(cls_view, "convertRect:toView:", view_convert_rect_to);
    im(cls_view, "convertRect:fromView:", view_convert_rect_from);
    im(cls_view, "pointInside:withEvent:", view_point_inside);
    im(cls_view, "hitTest:withEvent:", view_hit_test);
    IMS(cls_view, ret_self, "layoutSubviews", "setNeedsDisplay", "setNeedsDisplayInRect:", "drawRect:", "sizeToFit",
        "didMoveToSuperview", "didMoveToWindow", "willMoveToSuperview:", "willMoveToWindow:", "didAddSubview:",
        "willRemoveSubview:", "endEditing:");
    PROPS(cls_view, "setMultipleTouchEnabled:", "isMultipleTouchEnabled", "setExclusiveTouch:", "isExclusiveTouch",
          "setUserInteractionEnabled:", "isUserInteractionEnabled", "setBackgroundColor:", "backgroundColor",
          "setOpaque:", "isOpaque", "setClipsToBounds:", "clipsToBounds", "setContentMode:", "contentMode",
          "setAutoresizingMask:", "autoresizingMask", "setAutoresizesSubviews:", "autoresizesSubviews", "setTag:",
          "tag", "setAlpha:", "alpha", "setHidden:", "isHidden", "setContentScaleFactor:", "contentScaleFactor",
          "setClearsContextBeforeDrawing:", "clearsContextBeforeDrawing", "setContentStretch:");
    cm(cls_view, "beginAnimations:context:", anim_begin);
    cm(cls_view, "commitAnimations", anim_commit);
    cm(cls_view, "setAnimationDelegate:", anim_set_delegate);
    cm(cls_view, "setAnimationDidStopSelector:", anim_set_stop);
    IMS(rt_ld32(cls_view) /* metaclass */, ret_self, "setAnimationDuration:", "setAnimationDelay:", "setAnimationCurve:",
        "setAnimationRepeatCount:", "setAnimationRepeatAutoreverses:", "setAnimationTransition:forView:cache:",
        "setAnimationsEnabled:", "setAnimationBeginsFromCurrentState:", "setAnimationWillStartSelector:",
        "setAnimationStartDate:");
    cm(cls_view, "areAnimationsEnabled", ret_one);

    /* UIWindow */
    im(cls_window, "makeKeyAndVisible", win_make_key_visible);
    im(cls_window, "makeKeyWindow", win_make_key);
    im(cls_window, "isKeyWindow", win_is_key);
    im(cls_window, "rootViewController", win_root_vc);
    im(cls_window, "setRootViewController:", win_set_root_vc);
    IMS(cls_window, ret_self, "sendEvent:", "becomeKeyWindow", "resignKeyWindow");
    PROPS(cls_window, "setWindowLevel:", "windowLevel", "setScreen:", "screen");

    /* UIControl, UIButton */
    im(cls_control, "addTarget:action:forControlEvents:", ctl_add_target);
    im(cls_control, "removeTarget:action:forControlEvents:", ctl_remove_target);
    im(cls_control, "sendActionsForControlEvents:", ctl_send_actions);
    im(cls_control, "state", ctl_state);
    im(cls_control, "touchesBegan:withEvent:", ctl_touches_began);
    im(cls_control, "touchesMoved:withEvent:", ctl_touches_moved);
    im(cls_control, "touchesEnded:withEvent:", ctl_touches_ended);
    im(cls_control, "touchesCancelled:withEvent:", ctl_touches_cancelled);
    PROPS(cls_control, "setEnabled:", "isEnabled", "setSelected:", "isSelected", "setHighlighted:", "isHighlighted",
          "setContentHorizontalAlignment:", "contentHorizontalAlignment", "setContentVerticalAlignment:",
          "contentVerticalAlignment", "isTracking", "isTouchInside");
    cm(cls_button, "buttonWithType:", btn_with_type);
    im(cls_button, "setTitle:forState:", btn_set_title);
    im(cls_button, "titleForState:", btn_title_for);
    im(cls_button, "currentTitle", btn_current_title);
    im(cls_button, "setImage:forState:", btn_set_image);
    im(cls_button, "imageForState:", btn_image_for);
    im(cls_button, "currentImage", btn_current_image);
    im(cls_button, "setBackgroundImage:forState:", btn_set_bg);
    im(cls_button, "backgroundImageForState:", btn_bg_for);
    im(cls_button, "titleLabel", btn_title_label);
    PROPS(cls_button, "buttonType", "setTitleColor:forState:", "setTitleShadowColor:forState:",
          "setShowsTouchWhenHighlighted:", "showsTouchWhenHighlighted", "setAdjustsImageWhenHighlighted:",
          "adjustsImageWhenHighlighted", "setAdjustsImageWhenDisabled:", "setReversesTitleShadowWhenHighlighted:",
          "setFont:", "font", "setLineBreakMode:", "lineBreakMode", "setTitleShadowOffset:");

    /* UILabel */
    PROPS(cls_label, "setText:", "text", "setFont:", "font", "setTextColor:", "textColor", "setTextAlignment:",
          "textAlignment", "setNumberOfLines:", "numberOfLines", "setLineBreakMode:", "lineBreakMode",
          "setAdjustsFontSizeToFitWidth:", "adjustsFontSizeToFitWidth", "setHighlightedTextColor:",
          "highlightedTextColor", "setHighlighted:", "isHighlighted", "setEnabled:", "isEnabled", "setMinimumFontSize:",
          "minimumFontSize", "setShadowColor:", "shadowColor", "setBaselineAdjustment:", "baselineAdjustment");

    /* UIImageView */
    im(cls_imageview, "initWithImage:", iv_init_image);
    im(cls_imageview, "startAnimating", anim_start);
    im(cls_imageview, "stopAnimating", anim_stop);
    im(cls_imageview, "isAnimating", anim_is);
    PROPS(cls_imageview, "setImage:", "image", "setHighlightedImage:", "highlightedImage", "setAnimationImages:",
          "animationImages", "setAnimationRepeatCount:", "animationRepeatCount", "setHighlighted:", "isHighlighted");
    im(cls_imageview, "setAnimationDuration:", ret_self); /* a double: not a one-word property */

    /* UIScrollView */
    im(cls_scroll, "contentOffset", sv_offset);
    im(cls_scroll, "setContentOffset:", sv_set_offset);
    im(cls_scroll, "setContentOffset:animated:", sv_set_offset);
    im(cls_scroll, "contentSize", sv_size);
    im(cls_scroll, "setContentSize:", sv_set_size);
    im(cls_scroll, "contentInset", ret_zero_stret16);
    IMS(cls_scroll, ret_zero, "isDragging", "isDecelerating", "zoomScale");
    IMS(cls_scroll, ret_self, "flashScrollIndicators", "scrollRectToVisible:animated:", "setContentInset:",
        "setScrollIndicatorInsets:", "setZoomScale:animated:");
    PROPS(cls_scroll, "setDelegate:", "delegate", "setScrollEnabled:", "isScrollEnabled", "setPagingEnabled:",
          "isPagingEnabled", "setShowsHorizontalScrollIndicator:", "showsHorizontalScrollIndicator",
          "setShowsVerticalScrollIndicator:", "showsVerticalScrollIndicator", "setBounces:", "bounces",
          "setAlwaysBounceVertical:", "setAlwaysBounceHorizontal:", "setDecelerationRate:", "setIndicatorStyle:",
          "setDirectionalLockEnabled:", "setMinimumZoomScale:", "setMaximumZoomScale:", "setZoomScale:",
          "setDelaysContentTouches:", "setCanCancelContentTouches:", "setBouncesZoom:", "setScrollsToTop:");

    /* UITextField, UITextView */
    im(cls_textfield, "text", tf_text);
    im(cls_textfield, "becomeFirstResponder", tf_become);
    im(cls_textfield, "resignFirstResponder", tf_resign);
    im(cls_textfield, "canBecomeFirstResponder", ret_one);
    PROPS(cls_textfield, "setText:", "setFont:", "font", "setTextColor:", "textColor", "setTextAlignment:",
          "textAlignment", "setPlaceholder:", "placeholder", "setBorderStyle:", "borderStyle", "setClearButtonMode:",
          "clearButtonMode", "setClearsOnBeginEditing:", "clearsOnBeginEditing", "setKeyboardType:", "keyboardType",
          "setAutocapitalizationType:", "autocapitalizationType", "setAutocorrectionType:", "autocorrectionType",
          "setSecureTextEntry:", "isSecureTextEntry", "setReturnKeyType:", "returnKeyType", "setKeyboardAppearance:",
          "keyboardAppearance", "setDelegate:", "delegate", "isEditing", "setAdjustsFontSizeToFitWidth:",
          "adjustsFontSizeToFitWidth", "setMinimumFontSize:", "setBackground:", "background",
          "setEnablesReturnKeyAutomatically:", "setLeftView:", "setRightView:", "setLeftViewMode:", "setRightViewMode:");
    im(cls_textview, "text", tf_text);
    im(cls_textview, "becomeFirstResponder", tv_become);
    im(cls_textview, "resignFirstResponder", tv_resign);
    im(cls_textview, "canBecomeFirstResponder", ret_one);
    im(cls_textview, "selectedRange", ret_zero_stret);
    PROPS(cls_textview, "setText:", "setFont:", "font", "setTextColor:", "textColor", "setTextAlignment:",
          "textAlignment", "setEditable:", "isEditable", "setDelegate:", "delegate", "setKeyboardType:",
          "keyboardType", "setReturnKeyType:", "setAutocorrectionType:", "setAutocapitalizationType:",
          "setSelectedRange:", "setDataDetectorTypes:", "isEditing", "setKeyboardAppearance:", "setSecureTextEntry:");

    /* UIActivityIndicatorView */
    im(cls_activity, "initWithActivityIndicatorStyle:", act_init_style);
    im(cls_activity, "startAnimating", act_start);
    im(cls_activity, "stopAnimating", act_stop);
    im(cls_activity, "isAnimating", anim_is);
    PROPS(cls_activity, "setHidesWhenStopped:", "hidesWhenStopped", "setActivityIndicatorViewStyle:",
          "activityIndicatorViewStyle", "setColor:", "color");

    /* UIAlertView */
    im(cls_alert, "initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:", alert_init);
    im(cls_alert, "addButtonWithTitle:", alert_add_button);
    im(cls_alert, "numberOfButtons", alert_count);
    im(cls_alert, "cancelButtonIndex", alert_cancel_index);
    im(cls_alert, "buttonTitleAtIndex:", alert_title_at);
    im(cls_alert, "show", alert_show);
    im(cls_alert, "dismissWithClickedButtonIndex:animated:", alert_dismiss);
    PROPS(cls_alert, "setTitle:", "title", "setMessage:", "message", "setDelegate:", "delegate", "isVisible",
          "setCancelButtonIndex:", "setAlertViewStyle:");

    /* UIViewController */
    IMS(cls_vc, vc_init, "init", "initWithNibName:bundle:");
    im(cls_vc, "view", vc_view);
    im(cls_vc, "setView:", vc_set_view);
    im(cls_vc, "loadView", vc_load_view);
    im(cls_vc, "isViewLoaded", vc_is_loaded);
    im(cls_vc, "interfaceOrientation", vc_orientation);
    im(cls_vc, "shouldAutorotateToInterfaceOrientation:", vc_autorotate);
    im(cls_vc, "presentModalViewController:animated:", vc_present_modal);
    im(cls_vc, "dismissModalViewControllerAnimated:", vc_dismiss_modal);
    im(cls_vc, "modalViewController", vc_modal);
    im(cls_vc, "parentViewController", vc_parent);
    im(cls_vc, "navigationController", vc_nav);
    im(cls_vc, "tabBarController", vc_tab);
    IMS(cls_vc, ret_self, "viewDidLoad", "viewDidUnload", "viewWillAppear:", "viewDidAppear:", "viewWillDisappear:",
        "viewDidDisappear:", "didReceiveMemoryWarning", "willRotateToInterfaceOrientation:duration:",
        "didRotateFromInterfaceOrientation:", "willAnimateRotationToInterfaceOrientation:duration:");
    IMS(cls_vc, ret_zero, "navigationItem", "searchDisplayController", "splitViewController");
    PROPS(cls_vc, "setTitle:", "title", "setTabBarItem:", "tabBarItem", "setWantsFullScreenLayout:",
          "wantsFullScreenLayout", "setHidesBottomBarWhenPushed:", "hidesBottomBarWhenPushed",
          "setModalPresentationStyle:", "modalPresentationStyle", "setModalTransitionStyle:", "modalTransitionStyle",
          "setModalInPopover:", "setContentSizeForViewInPopover:");

    /* UINavigationController, UITabBarController */
    im(cls_nav, "initWithRootViewController:", nav_init_root);
    im(cls_nav, "pushViewController:animated:", nav_push);
    im(cls_nav, "popViewControllerAnimated:", nav_pop);
    im(cls_nav, "popToRootViewControllerAnimated:", nav_pop_root);
    im(cls_nav, "popToViewController:animated:", nav_pop_to);
    im(cls_nav, "viewControllers", nav_vcs);
    im(cls_nav, "setViewControllers:", nav_set_vcs);
    im(cls_nav, "setViewControllers:animated:", nav_set_vcs);
    im(cls_nav, "topViewController", nav_top);
    im(cls_nav, "visibleViewController", nav_visible);
    im(cls_nav, "navigationBar", ret_zero);
    im(cls_nav, "toolbar", ret_zero);
    PROPS(cls_nav, "setNavigationBarHidden:", "setNavigationBarHidden:animated:", "isNavigationBarHidden",
          "setToolbarHidden:", "setToolbarHidden:animated:", "isToolbarHidden", "setDelegate:", "delegate");
    im(cls_tab, "viewControllers", nav_vcs);
    im(cls_tab, "setViewControllers:", nav_set_vcs);
    im(cls_tab, "setViewControllers:animated:", nav_set_vcs);
    im(cls_tab, "selectedViewController", tab_selected_vc);
    im(cls_tab, "setSelectedViewController:", tab_set_vc);
    im(cls_tab, "selectedIndex", tab_selected_index);
    im(cls_tab, "setSelectedIndex:", tab_set_index);
    im(cls_tab, "tabBar", ret_zero);
    im(cls_tab, "moreNavigationController", ret_zero);
    PROPS(cls_tab, "setDelegate:", "delegate", "setCustomizableViewControllers:");

    /* UIPopoverController, UIImagePickerController */
    im(cls_popover, "initWithContentViewController:", pop_init);
    im(cls_popover, "presentPopoverFromRect:inView:permittedArrowDirections:animated:", pop_present);
    im(cls_popover, "dismissPopoverAnimated:", pop_dismiss);
    PROPS(cls_popover, "contentViewController", "isPopoverVisible", "setDelegate:", "delegate",
          "setPopoverContentSize:", "setPopoverContentSize:animated:", "setPassthroughViews:");
    cm(picker, "isSourceTypeAvailable:", picker_available);
    cm(picker, "availableMediaTypesForSourceType:", ret_zero);
    PROPS(picker, "setSourceType:", "sourceType", "setAllowsImageEditing:", "allowsImageEditing", "setAllowsEditing:",
          "allowsEditing", "setMediaTypes:", "mediaTypes");

    /* UIAccelerometer, UIAcceleration */
    cm(accel, "sharedAccelerometer", acc_shared);
    im(accel, "setDelegate:", acc_set_delegate);
    im(accel, "delegate", acc_delegate);
    im(accel, "setUpdateInterval:", acc_set_interval);
    im(accel, "updateInterval", acc_interval);
    im(cls_acceleration, "x", acn_x);
    im(cls_acceleration, "y", acn_y);
    im(cls_acceleration, "z", acn_z);
    im(cls_acceleration, "timestamp", acn_timestamp);

    /* UITouch, UIEvent, the touch set */
    im(cls_touch, "locationInView:", touch_location);
    im(cls_touch, "previousLocationInView:", touch_prev_location);
    im(cls_touch, "phase", touch_phase);
    im(cls_touch, "tapCount", touch_taps);
    im(cls_touch, "timestamp", touch_timestamp);
    im(cls_touch, "view", touch_view);
    im(cls_touch, "window", touch_window);
    im(cls_event, "allTouches", event_all);
    im(cls_event, "touchesForView:", event_for_view);
    im(cls_event, "touchesForWindow:", event_for_view);
    im(cls_event, "timestamp", event_timestamp);
    im(cls_event, "type", event_type);
    im(cls_event, "subtype", ret_zero);
    im(cls_touchset, "count", set_count);
    im(cls_touchset, "anyObject", set_any);
    im(cls_touchset, "allObjects", set_all);
    im(cls_touchset, "containsObject:", set_contains);
    im(cls_touchset, "member:", set_member);
    im(cls_touchset, "countByEnumeratingWithState:objects:count:", set_enumerate);
    im(cls_touchset, "objectEnumerator", set_enumerator);

    /* CALayer, CAEAGLLayer */
    im(cls_layer, "bounds", layer_bounds);
    im(cls_layer, "frame", layer_frame);
    im(cls_layer, "position", layer_position);
    im(cls_layer, "setBounds:", layer_set_bounds);
    im(cls_layer, "setFrame:", layer_set_frame);
    im(cls_layer, "renderInContext:", layer_render);
    IMS(cls_layer, ret_self, "setNeedsDisplay", "setNeedsLayout", "display", "layoutIfNeeded", "addSublayer:",
        "removeFromSuperlayer", "setPosition:", "setAnchorPoint:", "setTransform:", "setAffineTransform:",
        "removeAllAnimations");
    PROPS(cls_layer, "setOpaque:", "isOpaque", "setDelegate:", "delegate", "setHidden:", "isHidden", "setOpacity:",
          "opacity", "setContentsScale:", "contentsScale", "setMasksToBounds:", "masksToBounds", "setCornerRadius:",
          "cornerRadius", "setBorderWidth:", "setBorderColor:", "setBackgroundColor:", "setContents:", "contents",
          "setName:", "name", "setZPosition:");
    PROPS(cls_eagllayer, "setDrawableProperties:", "drawableProperties");

    /* EAGLContext */
    im(cls_context, "initWithAPI:", eagl_init_api);
    im(cls_context, "initWithAPI:sharegroup:", eagl_init_api);
    im(cls_context, "API", eagl_api);
    im(cls_context, "sharegroup", ret_zero);
    cm(cls_context, "setCurrentContext:", eagl_set_current);
    cm(cls_context, "currentContext", eagl_current);
    im(cls_context, "renderbufferStorage:fromDrawable:", eagl_storage);
    im(cls_context, "presentRenderbuffer:", eagl_present);

    device_object = rt_objc_alloc(device);
    screen_object = rt_objc_alloc(screen);
    accel_object = rt_objc_alloc(accel);
    event_object = rt_objc_alloc(cls_event);
    (void)cls_eagllayer;

    rt_data_resolver_add(resolve);
}

void rt_uikit_register(void) {
    rt_import_register("UIApplicationMain", imp_UIApplicationMain);
}
