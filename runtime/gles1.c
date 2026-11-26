/* OpenGL ES 1.1 imports, forwarded to the host's GL (an ANGLE ES1 context on Windows, libGLESv1_CM through EGL on
 * Android), and the iOS drawable mapped onto the EGL window surface.
 *
 * Shims. Every gl* import of the executable is a host function that looks its host entry point up once through
 * rt_gl_getproc (the OES name first; for the framebuffer-object calls the core GLES2 name as a fallback) and
 * calls it with the arguments translated from the iOS softfp ABI:
 *   GLenum/GLint/GLuint/GLsizei/GLbitfield/GLfixed   one argument word each, as is
 *   GLfloat                                          its bits in one argument word (rt_farg)
 *   GLboolean/GLubyte                                the low byte of a word
 *   pointers                                         game addresses -> rt_mem + addr
 * except the client-array pointers (glVertexPointer, glTexCoordPointer, glColorPointer, glNormalPointer) while a
 * buffer object is bound to GL_ARRAY_BUFFER, and the glDrawElements indices while one is bound to
 * GL_ELEMENT_ARRAY_BUFFER: those are offsets into the buffer and pass unchanged. The bindings are tracked here.
 * The GLfixed (x) variants go to the host's x entry point, or to the float one converted when it has none.
 *
 * Headless runs (rt_gl_getproc NULL, or a function the host lacks): a shim does nothing and returns 0, except
 * that glGen* hand out distinct names (the game keeps and compares them), glCheckFramebufferStatusOES reports
 * complete, and the state tracked here (bindings, the drawable) answers its queries as usual.
 *
 * glGetString: the host string is copied into game memory once per name (the game keeps the pointer; it checks
 * for NULL, so a headless run returns 0). GL_EXTENSIONS also lists the formats emulated here.
 * Textures: the game's textures are PVRTC; when the host does not list GL_IMG_texture_compression_pvrtc each
 * glCompressedTexImage2D level is decoded to RGBA8 in software and uploaded with glTexImage2D. GL_BGRA_EXT
 * pixel data (APPLE_texture_format_BGRA8888, RGBA internal format) is swapped to RGBA before upload.
 * glGetPointerv: the host writes a host pointer (8 bytes on 64-bit hosts) into a local, converted back to the
 * 32-bit game address (or the buffer offset) the game passed.
 *
 * The drawable. On iOS the game creates its own framebuffer object and colour renderbuffer, gives the renderbuffer
 * the layer's storage with -[EAGLContext renderbufferStorage:fromDrawable:] (UIKit then calls rt_gl_bind_drawable
 * with the renderbuffer bound at that moment, which it reads with glGetIntegerv(GL_RENDERBUFFER_BINDING_OES)), adds
 * a depth renderbuffer, and presents with -presentRenderbuffer: (rt_gl_present). With EGL the window surface is
 * framebuffer 0 and already has depth, so:
 *   - the renderbuffer passed to rt_gl_bind_drawable is the drawable renderbuffer; the framebuffer it gets attached
 *     to (glFramebufferRenderbufferOES, in either order with rt_gl_bind_drawable) is the drawable framebuffer;
 *   - glBindFramebufferOES(drawable framebuffer) binds 0 on the host; glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES)
 *     still answers the game's name;
 *   - storage and attachment calls for the drawable renderbuffer, and for renderbuffers attached to the drawable
 *     framebuffer as depth/stencil, are ignored; glGetRenderbufferParameterivOES on the drawable renderbuffer
 *     answers the drawable size (RGBA8), glGetFramebufferAttachmentParameterivOES on the drawable framebuffer the
 *     game's own attachments, and glCheckFramebufferStatusOES on it GL_FRAMEBUFFER_COMPLETE_OES;
 *   - every other (offscreen) framebuffer and renderbuffer passes through.
 * Since the game draws a portrait 768x1024 layer, "framebuffer 0" above is really a host FBO of that size, shown
 * rotated into the window at present time (see "the portrait drawable and its presentation" below).
 * GL calls come from the game's render thread only, so the tracked state needs no lock.
 */
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "rt.h"

#if defined(_WIN32) && !defined(_WIN64)
#define GLAPI __stdcall
#else
#define GLAPI
#endif

/* ---- GL constants used here ---------------------------------------------------------------------------------- */
#define GL_NO_ERROR 0
#define GL_RENDERBUFFER_BINDING_OES 0x8CA7
#define GL_FRAMEBUFFER_BINDING_OES 0x8CA6
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_FRAMEBUFFER_COMPLETE_OES 0x8CD5
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_OES 0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_OES 0x8CD1
#define GL_COLOR_ATTACHMENT0_OES 0x8CE0
#define GL_RENDERBUFFER_OES 0x8D41
#define GL_RENDERBUFFER_WIDTH_OES 0x8D42
#define GL_RENDERBUFFER_HEIGHT_OES 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT_OES 0x8D44
#define GL_RENDERBUFFER_RED_SIZE_OES 0x8D50
#define GL_RENDERBUFFER_GREEN_SIZE_OES 0x8D51
#define GL_RENDERBUFFER_BLUE_SIZE_OES 0x8D52
#define GL_RENDERBUFFER_ALPHA_SIZE_OES 0x8D53
#define GL_RENDERBUFFER_DEPTH_SIZE_OES 0x8D54
#define GL_RENDERBUFFER_STENCIL_SIZE_OES 0x8D55
#define GL_RGBA8_OES 0x8058
#define GL_RGB_SCALE 0x8573
#define GL_ALPHA_SCALE 0x0D1C

/* ---- host entry points ------------------------------------------------------------------------------------------ */
typedef struct {
    const char *names[2]; /* tried in order */
    void *fn;
    int tried;
} glproc;

static void *gl_lookup(glproc *p) {
    if (p->tried) return p->fn;
    if (!rt_gl_getproc) return NULL; /* headless (or not set yet): look again next time */
    p->tried = 1;
    for (int k = 0; k < 2 && p->names[k] && !p->fn; k++) p->fn = rt_gl_getproc(p->names[k]);
    if (!p->fn) rt_log("gl: host has no %s (ignored)", p->names[0]);
    return p->fn;
}

/* DECL(name, return, (parameters)) declares the host type T_name and its lookup record; DECL2 adds a second name */
#define DECL2(name, alt, ret, params) \
    typedef ret (GLAPI *T_##name) params; \
    static glproc P_##name = {{#name, alt}, NULL, 0};
#define DECL(name, ret, params) DECL2(name, NULL, ret, params)
#define F(name) ((T_##name)gl_lookup(&P_##name))

#define A(k) rt_arg(c, k)
#define FA(k) rt_farg(c, k)
#define B(k) ((uint8_t)rt_arg(c, k))
#define RET(v) rt_ret(c, (uint32_t)(v))
#define IMP(name) static void imp_##name(cpu_t *c)
#define REG(name) rt_import_register(#name, imp_##name)

typedef uint32_t E; /* GLenum, GLuint, GLbitfield */
typedef int32_t I;  /* GLint, GLsizei, GLfixed */

DECL(glActiveTexture, void, (E))
DECL(glAlphaFunc, void, (E, float))
DECL(glBindBuffer, void, (E, E))
DECL2(glBindFramebufferOES, "glBindFramebuffer", void, (E, E))
DECL2(glBindRenderbufferOES, "glBindRenderbuffer", void, (E, E))
DECL(glBindTexture, void, (E, E))
DECL(glBlendFunc, void, (E, E))
DECL(glBufferData, void, (E, intptr_t, const void *, E))
DECL(glBufferSubData, void, (E, intptr_t, intptr_t, const void *))
DECL2(glCheckFramebufferStatusOES, "glCheckFramebufferStatus", E, (E))
DECL(glClear, void, (E))
DECL(glClearColor, void, (float, float, float, float))
DECL(glClearDepthf, void, (float))
DECL(glClientActiveTexture, void, (E))
DECL(glClipPlanef, void, (E, const float *))
DECL(glColor4f, void, (float, float, float, float))
DECL(glColor4ub, void, (uint8_t, uint8_t, uint8_t, uint8_t))
DECL(glColor4x, void, (I, I, I, I))
DECL(glColorMask, void, (uint8_t, uint8_t, uint8_t, uint8_t))
DECL(glColorPointer, void, (I, E, I, const void *))
DECL(glCompressedTexImage2D, void, (E, I, E, I, I, I, I, const void *))
DECL(glCopyTexImage2D, void, (E, I, E, I, I, I, I, I))
DECL(glCullFace, void, (E))
DECL(glDeleteBuffers, void, (I, const E *))
DECL2(glDeleteFramebuffersOES, "glDeleteFramebuffers", void, (I, const E *))
DECL2(glDeleteRenderbuffersOES, "glDeleteRenderbuffers", void, (I, const E *))
DECL(glDeleteTextures, void, (I, const E *))
DECL(glDepthFunc, void, (E))
DECL(glDepthMask, void, (uint8_t))
DECL(glDepthRangef, void, (float, float))
DECL(glDisable, void, (E))
DECL(glDisableClientState, void, (E))
DECL(glDrawArrays, void, (E, I, I))
DECL(glDrawElements, void, (E, I, E, const void *))
DECL(glEnable, void, (E))
DECL(glEnableClientState, void, (E))
DECL(glFlush, void, (void))
DECL(glFogf, void, (E, float))
DECL(glFogfv, void, (E, const float *))
DECL2(glFramebufferRenderbufferOES, "glFramebufferRenderbuffer", void, (E, E, E, E))
DECL2(glFramebufferTexture2DOES, "glFramebufferTexture2D", void, (E, E, E, E, I))
DECL(glFrontFace, void, (E))
DECL(glFrustumf, void, (float, float, float, float, float, float))
DECL(glGenBuffers, void, (I, E *))
DECL2(glGenFramebuffersOES, "glGenFramebuffers", void, (I, E *))
DECL2(glGenRenderbuffersOES, "glGenRenderbuffers", void, (I, E *))
DECL(glGenTextures, void, (I, E *))
DECL(glGetBooleanv, void, (E, uint8_t *))
DECL(glGetError, E, (void))
DECL(glGetFloatv, void, (E, float *))
DECL2(glGetFramebufferAttachmentParameterivOES, "glGetFramebufferAttachmentParameteriv", void, (E, E, E, I *))
DECL(glGetIntegerv, void, (E, I *))
DECL(glGetPointerv, void, (E, void **))
DECL2(glGetRenderbufferParameterivOES, "glGetRenderbufferParameteriv", void, (E, E, I *))
DECL(glGetString, const char *, (E))
DECL(glGetTexEnviv, void, (E, E, I *))
DECL(glHint, void, (E, E))
DECL(glIsEnabled, uint8_t, (E))
DECL(glLightModelfv, void, (E, const float *))
DECL(glLightf, void, (E, E, float))
DECL(glLightfv, void, (E, E, const float *))
DECL(glLineWidth, void, (float))
DECL(glLoadIdentity, void, (void))
DECL(glLoadMatrixf, void, (const float *))
DECL(glMaterialf, void, (E, E, float))
DECL(glMaterialfv, void, (E, E, const float *))
DECL(glMatrixMode, void, (E))
DECL(glMultMatrixf, void, (const float *))
DECL(glNormal3f, void, (float, float, float))
DECL(glNormalPointer, void, (E, I, const void *))
DECL(glOrthof, void, (float, float, float, float, float, float))
DECL(glOrthox, void, (I, I, I, I, I, I))
DECL(glPixelStorei, void, (E, I))
DECL(glPointParameterf, void, (E, float))
DECL(glPointSize, void, (float))
DECL(glPolygonOffset, void, (float, float))
DECL(glPopMatrix, void, (void))
DECL(glPushMatrix, void, (void))
DECL(glReadPixels, void, (I, I, I, I, E, E, void *))
DECL2(glRenderbufferStorageOES, "glRenderbufferStorage", void, (E, E, I, I))
DECL(glRotatef, void, (float, float, float, float))
DECL(glRotatex, void, (I, I, I, I))
DECL(glScalef, void, (float, float, float))
DECL(glScalex, void, (I, I, I))
DECL(glScissor, void, (I, I, I, I))
DECL(glShadeModel, void, (E))
DECL(glStencilFunc, void, (E, I, E))
DECL(glStencilMask, void, (E))
DECL(glStencilOp, void, (E, E, E))
DECL(glTexCoordPointer, void, (I, E, I, const void *))
DECL(glTexEnvf, void, (E, E, float))
DECL(glTexEnvfv, void, (E, E, const float *))
DECL(glTexEnvi, void, (E, E, I))
DECL(glTexEnvx, void, (E, E, I))
DECL(glTexImage2D, void, (E, I, I, I, I, I, E, E, const void *))
DECL(glTexParameterf, void, (E, E, float))
DECL(glTexParameteri, void, (E, E, I))
DECL(glTexSubImage2D, void, (E, I, I, I, I, I, E, E, const void *))
DECL(glTranslatef, void, (float, float, float))
DECL(glTranslatex, void, (I, I, I))
DECL(glVertexPointer, void, (I, E, I, const void *))
DECL(glViewport, void, (I, I, I, I))

static float fx(int32_t v) { return (float)v / 65536.0f; } /* GLfixed -> GLfloat */

/* ---- tracked state ------------------------------------------------------------------------------------------- */
static uint32_t array_buffer, element_buffer;     /* GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER bindings */
static uint32_t bound_fb, bound_rb;               /* the game's view of the bindings */
static uint32_t drawable_rb, drawable_fb;         /* the window (0 until known) */
static int drawable_w, drawable_h;
static uint32_t fake_names = 0x10000;             /* glGen* names in headless runs */

#define MAX_ATTACH 32
/* drawable-framebuffer depth/stencil renderbuffers (ignored), and the colour renderbuffer attached to each
 * framebuffer seen so far (for a drawable bound after its framebuffer was set up) */
static uint32_t drawable_extra_rb[MAX_ATTACH];
static uint32_t drawable_extra_att[MAX_ATTACH];
static struct { uint32_t fb, rb; } color_att[MAX_ATTACH];

static int is_drawable_extra(uint32_t rb) {
    if (!rb) return 0;
    for (int k = 0; k < MAX_ATTACH; k++) if (drawable_extra_rb[k] == rb) return 1;
    return 0;
}
static void add_drawable_extra(uint32_t attachment, uint32_t rb) {
    int free_slot = -1;
    for (int k = 0; k < MAX_ATTACH; k++) {
        if (drawable_extra_att[k] == attachment && drawable_extra_rb[k]) { drawable_extra_rb[k] = rb; return; }
        if (!drawable_extra_rb[k] && free_slot < 0) free_slot = k;
    }
    if (free_slot >= 0) { drawable_extra_rb[free_slot] = rb; drawable_extra_att[free_slot] = attachment; }
    else rt_log("gl: too many drawable attachments");
}
static uint32_t drawable_extra_for(uint32_t attachment) {
    for (int k = 0; k < MAX_ATTACH; k++)
        if (drawable_extra_rb[k] && drawable_extra_att[k] == attachment) return drawable_extra_rb[k];
    return 0;
}
static void set_color_att(uint32_t fb, uint32_t rb) {
    int free_slot = -1;
    for (int k = 0; k < MAX_ATTACH; k++) {
        if (color_att[k].fb == fb) { color_att[k].rb = rb; if (!rb) color_att[k].fb = 0; return; }
        if (!color_att[k].fb && free_slot < 0) free_slot = k;
    }
    if (rb && free_slot >= 0) { color_att[free_slot].fb = fb; color_att[free_slot].rb = rb; }
}
static int is_drawable_fb(uint32_t fb) { return fb && fb == drawable_fb; }
static uint32_t present_fbo;                      /* host framebuffer behind the portrait drawable (0: the window itself) */
static uint32_t host_fb(uint32_t fb) { return is_drawable_fb(fb) ? present_fbo : fb; }

static void host_bind_fb(E target, uint32_t fb) {
    T_glBindFramebufferOES f = F(glBindFramebufferOES);
    if (f) f(target ? target : 0x8D40 /* GL_FRAMEBUFFER_OES */, host_fb(fb));
}

/* that framebuffer becomes the window */
static void make_drawable_fb(uint32_t fb) {
    if (drawable_fb == fb) return;
    drawable_fb = fb;
    memset(drawable_extra_rb, 0, sizeof drawable_extra_rb);
    if (bound_fb == fb) host_bind_fb(0, fb);
}

/* ---- the matrices of the game -------------------------------------------------------------------------------------
 * Kept here as the game sets them (modelview and projection stacks), because the widescreen code needs them for every
 * draw and some drivers (the Android emulator) answer glGetFloatv for a matrix with the identity. Column-major. */
enum { MV_DEPTH = 32, PJ_DEPTH = 4 };
static float sh_mv[MV_DEPTH][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
static float sh_pj[PJ_DEPTH][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
static int sh_mv_sp, sh_pj_sp;
static int mat_mode = 0x1700, proj_scaled;
static float wide_k; /* drawable_h / present_h once the wide layer exists */
static float *sh_cur(void) { return mat_mode == 0x1700 ? sh_mv[sh_mv_sp] : mat_mode == 0x1701 ? sh_pj[sh_pj_sp] : NULL; }
static void sh_identity(float *m) { for (int k = 0; k < 16; k++) m[k] = (k % 5) ? 0.0f : 1.0f; }
static void sh_mult(const float *b) { /* current = current * b */
    float *a = sh_cur(), r[16];
    if (!a) return;
    for (int c = 0; c < 4; c++)
        for (int w = 0; w < 4; w++)
            r[4 * c + w] = a[w] * b[4 * c] + a[4 + w] * b[4 * c + 1] + a[8 + w] * b[4 * c + 2] + a[12 + w] * b[4 * c + 3];
    memcpy(a, r, sizeof r);
}
static void sh_load(const float *b) { float *a = sh_cur(); if (a) memcpy(a, b, 16 * sizeof(float)); }
static void sh_push(void) {
    if (mat_mode == 0x1700 && sh_mv_sp < MV_DEPTH - 1) { memcpy(sh_mv[sh_mv_sp + 1], sh_mv[sh_mv_sp], sizeof sh_mv[0]); sh_mv_sp++; }
    if (mat_mode == 0x1701 && sh_pj_sp < PJ_DEPTH - 1) { memcpy(sh_pj[sh_pj_sp + 1], sh_pj[sh_pj_sp], sizeof sh_pj[0]); sh_pj_sp++; }
}
static void sh_pop(void) {
    if (mat_mode == 0x1700 && sh_mv_sp > 0) sh_mv_sp--;
    if (mat_mode == 0x1701 && sh_pj_sp > 0) sh_pj_sp--;
}
static void sh_frustum(float l, float r, float b, float t, float n, float f, int ortho) {
    float m[16] = {0};
    if (r == l || t == b || f == n) return;
    if (ortho) {
        m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
        m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); m[15] = 1;
    } else {
        m[0] = 2 * n / (r - l); m[5] = 2 * n / (t - b); m[8] = (r + l) / (r - l); m[9] = (t + b) / (t - b);
        m[10] = -(f + n) / (f - n); m[11] = -1; m[14] = -2 * f * n / (f - n);
    }
    sh_mult(m);
}
static void sh_rotate(float deg, float x, float y, float z) {
    float len = sqrtf(x * x + y * y + z * z), m[16];
    if (len < 1e-12f) return;
    x /= len; y /= len; z /= len;
    float a = deg * 0.017453292519943295f, c = cosf(a), s = sinf(a), t = 1 - c;
    sh_identity(m);
    m[0] = x * x * t + c;     m[4] = x * y * t - z * s; m[8] = x * z * t + y * s;
    m[1] = y * x * t + z * s; m[5] = y * y * t + c;     m[9] = y * z * t - x * s;
    m[2] = z * x * t - y * s; m[6] = z * y * t + x * s; m[10] = z * z * t + c;
    sh_mult(m);
}
static void sh_scale(float x, float y, float z) { float m[16]; sh_identity(m); m[0] = x; m[5] = y; m[10] = z; sh_mult(m); }
static void sh_translate(float x, float y, float z) { float m[16]; sh_identity(m); m[12] = x; m[13] = y; m[14] = z; sh_mult(m); }
/* the projection the driver must hold: the game one, squeezed along the long axis while the 3D scene is drawn wide */
static void sh_proj_for_gl(float *m) {
    memcpy(m, sh_pj[sh_pj_sp], 16 * sizeof(float));
    if (proj_scaled && wide_k > 0) { m[1] *= wide_k; m[5] *= wide_k; m[9] *= wide_k; m[13] *= wide_k; }
}

/* ---- the portrait drawable and its presentation ----------------------------------------------------------------------
 * The iPad's EAGL layer is 768x1024 (portrait) and the game rotates its own projection for landscape, so the screen
 * shows the layer turned a quarter turn. The game therefore draws into a 768x1024 host framebuffer object (a texture
 * and a depth/stencil renderbuffer), and -presentRenderbuffer: draws that texture into the window surface turned 90
 * degrees counter-clockwise (portrait (px, py) -> landscape (py, W - px), which is the inverse of the touch
 * conversion in uikit.c), then swaps. The game's GL state is saved around the blit and put back. */
static uint32_t present_tex, present_ds;
static int wide_dirty, proj_dirty = 1;
static int wide_apply(int kind, E mode, I first, I count, E itype, const void *idx);
/* a placement on the layer: Y' = Y * sy + oy along its long axis, X' = X * sx + ox along the short one */
struct place { float sy, oy, sx, ox; };
/* the controls of the HUD are drawn larger on a wide screen (a phone is far smaller than the iPad they were sized
 * for); where each enlarged element ended up in the last frame, for the touches on it */
#define HUD_SCALE 1.6f
enum { HIT_MAX = 512 };
/* in screen terms: y0..y1 across, x0..x1 down, t as rt_gl_wide_touch gives it */
static struct hit { float y0, y1, x0, x1, t[4]; int moved; } hit_cur[HIT_MAX], hit_last[HIT_MAX];
static int hit_cur_n, hit_last_n;
static int frame_3d; /* a 3D scene was drawn in this frame: the 2D elements after it are the HUD */
/* the HUD is up (and not the pause menu or a cutscene over the scene) while its pause button is drawn in the top left
 * corner: seen in this frame, and in the one before */
static int hud_seen, hud_on, hud_gone;
static int present_w, present_h;
static int present_failed;
static E packed_status, depth_status;
#ifdef NOVA_FRAME_LOG
static atomic_int trace_enabled;
static atomic_int cull_override;
void rt_gl_configure_diagnostics(int enabled, int cull) {
    atomic_store(&trace_enabled, enabled != 0);
    atomic_store(&cull_override, cull >= 0 && cull <= 2 ? cull : 0);
}
#endif

/* Shared by creation and resize. D16 cannot retain a stencil attachment. */
static E present_depth(T_glRenderbufferStorageOES storage, T_glFramebufferRenderbufferOES attach,
                       T_glCheckFramebufferStatusOES check, I width, I height) {
    storage(GL_RENDERBUFFER_OES, 0x88F0 /* DEPTH24_STENCIL8_OES */, width, height);
    attach(0x8D40, 0x8D00 /* DEPTH_ATTACHMENT */, GL_RENDERBUFFER_OES, present_ds);
    attach(0x8D40, 0x8D20 /* STENCIL_ATTACHMENT */, GL_RENDERBUFFER_OES, present_ds);
    E status = check(0x8D40);
    packed_status = status;
    if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
        rt_log("gl: packed depth/stencil framebuffer incomplete (%#x); trying D16", status);
        attach(0x8D40, 0x8D20, GL_RENDERBUFFER_OES, 0);
        storage(GL_RENDERBUFFER_OES, 0x81A5 /* DEPTH_COMPONENT16 */, width, height);
        status = check(0x8D40);
    }
    depth_status = status;
    return status;
}

static void present_setup(void) {
    if (present_fbo || present_failed || !drawable_w || !drawable_h || !rt_gl_getproc) return;
    T_glGenFramebuffersOES gf = F(glGenFramebuffersOES);
    T_glBindFramebufferOES bf = F(glBindFramebufferOES);
    T_glFramebufferTexture2DOES ft = F(glFramebufferTexture2DOES);
    T_glGenTextures gt = F(glGenTextures);
    T_glBindTexture bt = F(glBindTexture);
    T_glTexImage2D ti = F(glTexImage2D);
    T_glTexParameteri tp = F(glTexParameteri);
    T_glGenRenderbuffersOES gr = F(glGenRenderbuffersOES);
    T_glBindRenderbufferOES br = F(glBindRenderbufferOES);
    T_glRenderbufferStorageOES rs = F(glRenderbufferStorageOES);
    T_glFramebufferRenderbufferOES fr = F(glFramebufferRenderbufferOES);
    T_glCheckFramebufferStatusOES cs = F(glCheckFramebufferStatusOES);
    T_glGetIntegerv gi = F(glGetIntegerv);
    if (!gf || !bf || !ft || !gt || !bt || !ti || !tp || !gr || !br || !rs || !fr || !cs || !gi) { present_failed = 1; return; }
    I old_tex = 0, old_rb = 0, old_fb = 0;
    gi(0x8069 /* GL_TEXTURE_BINDING_2D */, &old_tex);
    gi(GL_RENDERBUFFER_BINDING_OES, &old_rb);
    gi(GL_FRAMEBUFFER_BINDING_OES, &old_fb);
    present_w = drawable_w;
    present_h = rt_gl_wide_long > drawable_h ? rt_gl_wide_long : drawable_h; /* widescreen: a longer layer */
    gt(1, &present_tex);
    bt(0x0DE1, present_tex);
    ti(0x0DE1, 0, 0x1908 /* RGBA */, present_w, present_h, 0, 0x1908, 0x1401, NULL);
    tp(0x0DE1, 0x2801 /* MIN_FILTER */, 0x2601 /* LINEAR */);
    tp(0x0DE1, 0x2800 /* MAG_FILTER */, 0x2601);
    tp(0x0DE1, 0x2802 /* WRAP_S */, 0x812F /* CLAMP_TO_EDGE */);
    tp(0x0DE1, 0x2803 /* WRAP_T */, 0x812F);
    uint32_t fbo = 0;
    gf(1, &fbo);
    bf(0x8D40, fbo);
    ft(0x8D40, GL_COLOR_ATTACHMENT0_OES, 0x0DE1, present_tex, 0);
    gr(1, &present_ds);
    br(GL_RENDERBUFFER_OES, present_ds);
    E st = present_depth(rs, fr, cs, present_w, present_h);
    if (st != GL_FRAMEBUFFER_COMPLETE_OES) {
        rt_log("gl: the presentation framebuffer is incomplete (%#x): drawing straight into the window", st);
        present_failed = 1;
    } else {
        present_fbo = fbo;
        if (rt_verbose) rt_log("gl: portrait drawable %dx%d in framebuffer %u (texture %u)", present_w, present_h, fbo, present_tex);
    }
    bt(0x0DE1, (uint32_t)old_tex);
    br(GL_RENDERBUFFER_OES, (uint32_t)old_rb);
    bf(0x8D40, is_drawable_fb(bound_fb) ? host_fb(bound_fb) : (uint32_t)old_fb);
}

typedef struct { I size, type, stride, buffer; void *ptr; uint8_t enabled; } arr_state;

/* draw the portrait texture into the window, turned a quarter turn, with the game's GL state saved and restored */
/* What the game enabled, kept here: glIsEnabled is answered without asking the driver (the Android emulator's GL
 * layer can crash on it, and the blit below needs these every frame). GL_TEXTURE_2D is per texture unit. */
static int act_unit;
static uint8_t tex2d_on[32];
static struct { E cap; uint8_t on; } cap_sh[64];
static int cap_n;
static uint8_t *cap_slot(E cap) {
    static uint8_t spare;
    for (int k = 0; k < cap_n; k++) if (cap_sh[k].cap == cap) return &cap_sh[k].on;
    if (cap_n == 64) return &spare;
    cap_sh[cap_n].cap = cap;
    cap_sh[cap_n].on = cap == 0x0BD0 /* DITHER */ || cap == 0x809D /* MULTISAMPLE */; /* on at the start */
    return &cap_sh[cap_n++].on;
}

static void present_blit(void) {
    T_glGetIntegerv gi = F(glGetIntegerv);
    T_glGetFloatv gfv = F(glGetFloatv);
    T_glGetBooleanv gbv = F(glGetBooleanv);
    T_glGetPointerv gp = F(glGetPointerv);
    T_glGetTexEnviv gte = F(glGetTexEnviv);
    T_glIsEnabled ie = F(glIsEnabled);
    T_glEnable en = F(glEnable);
    T_glDisable di = F(glDisable);
    T_glEnableClientState ecs = F(glEnableClientState);
    T_glDisableClientState dcs = F(glDisableClientState);
    T_glBindFramebufferOES bf = F(glBindFramebufferOES);
    T_glBindTexture bt = F(glBindTexture);
    T_glBindBuffer bb = F(glBindBuffer);
    T_glActiveTexture at = F(glActiveTexture);
    T_glClientActiveTexture cat = F(glClientActiveTexture);
    T_glMatrixMode mm = F(glMatrixMode);
    T_glLoadIdentity li = F(glLoadIdentity);
    T_glLoadMatrixf lm = F(glLoadMatrixf);
    T_glViewport vp = F(glViewport);
    T_glScissor sc = F(glScissor);
    T_glColor4f c4 = F(glColor4f);
    T_glColorMask cm = F(glColorMask);
    T_glTexEnvi te = F(glTexEnvi);
    T_glVertexPointer vptr = F(glVertexPointer);
    T_glTexCoordPointer tptr = F(glTexCoordPointer);
    T_glColorPointer cptr = F(glColorPointer);
    T_glNormalPointer nptr = F(glNormalPointer);
    T_glDrawArrays da = F(glDrawArrays);
    T_glClearColor cc = F(glClearColor);
    T_glClear cl = F(glClear);
    if (!gi || !gfv || !gbv || !gp || !gte || !ie || !en || !di || !ecs || !dcs || !bf || !bt || !bb || !at || !cat || !mm ||
        !li || !lm || !vp || !sc || !c4 || !cm || !te || !vptr || !tptr || !cptr || !nptr || !da || !cc || !cl) return;

    /* save */
    static const E caps[] = {0x0BE2 /* BLEND */, 0x0B71 /* DEPTH_TEST */, 0x0B44 /* CULL_FACE */, 0x0BC0 /* ALPHA_TEST */,
                             0x0B50 /* LIGHTING */, 0x0B60 /* FOG */, 0x0B90 /* STENCIL_TEST */, 0x0C11 /* SCISSOR_TEST */,
                             0x0BF2 /* COLOR_LOGIC_OP */};
    enum { NCAPS = sizeof caps / sizeof caps[0] };
    uint8_t cap_on[NCAPS];
    for (int k = 0; k < NCAPS; k++) cap_on[k] = *cap_slot(caps[k]);
    I vpt[4] = {0}, scb[4] = {0}, old_tex[2] = {0, 0}, old_active = 0, old_client = 0, old_mm = 0, old_env[2] = {0, 0};
    float mv[16], pj[16], tx[16], color[4], clear_color[4];
    uint8_t cmask[4], tex_on[2], tcarr_on[2];
    gi(0x0BA2 /* VIEWPORT */, vpt);
    gi(0x0C10 /* SCISSOR_BOX */, scb);
    gi(0x84E0 /* ACTIVE_TEXTURE */, &old_active);
    gi(0x84E1 /* CLIENT_ACTIVE_TEXTURE */, &old_client);
    gi(0x0BA0 /* MATRIX_MODE */, &old_mm);
    gfv(0x0BA6 /* MODELVIEW_MATRIX */, mv);
    gfv(0x0BA7 /* PROJECTION_MATRIX */, pj);
    gfv(0x0B00 /* CURRENT_COLOR */, color);
    gfv(0x0C22 /* COLOR_CLEAR_VALUE */, clear_color);
    gbv(0x0C23 /* COLOR_WRITEMASK */, cmask);
    for (int u = 0; u < 2; u++) {
        at(0x84C0 + u);
        cat(0x84C0 + u);
        gi(0x8069, &old_tex[u]);
        gte(0x2300 /* TEXTURE_ENV */, 0x2200 /* TEXTURE_ENV_MODE */, &old_env[u]);
        tex_on[u] = tex2d_on[u];
        tcarr_on[u] = ie(0x8078 /* TEXTURE_COORD_ARRAY */);
    }
    at(0x84C0);
    cat(0x84C0);
    mm(0x1702 /* TEXTURE */);
    gfv(0x0BA8 /* TEXTURE_MATRIX */, tx);
    arr_state av = {0}, an = {0}, ac = {0}, ax = {0};
    gi(0x807A, &av.size); gi(0x807B, &av.type); gi(0x807C, &av.stride); gi(0x8896, &av.buffer); gp(0x808E, &av.ptr); av.enabled = ie(0x8074);
    gi(0x807E, &an.type); gi(0x807F, &an.stride); gi(0x8897, &an.buffer); gp(0x808F, &an.ptr); an.enabled = ie(0x8075);
    gi(0x8081, &ac.size); gi(0x8082, &ac.type); gi(0x8083, &ac.stride); gi(0x8898, &ac.buffer); gp(0x8090, &ac.ptr); ac.enabled = ie(0x8076);
    gi(0x8088, &ax.size); gi(0x8089, &ax.type); gi(0x808A, &ax.stride); gi(0x889A, &ax.buffer); gp(0x8092, &ax.ptr); ax.enabled = tcarr_on[0];

    /* draw */
    int ww = 0, wh = 0;
    rt_platform_size(&ww, &wh);
    bf(0x8D40, 0);
    for (int k = 0; k < NCAPS; k++) di(caps[k]);
    at(0x84C1); cat(0x84C1); di(0x0DE1); dcs(0x8078);
    at(0x84C0); cat(0x84C0);
    cm(1, 1, 1, 1);
    c4(1, 1, 1, 1);
    mm(0x1700); li();
    mm(0x1701); li();
    mm(0x1702); li();
    int wide = ww * present_w > wh * present_h; /* the layer's aspect, centred, black bars */
    int vw = wide ? wh * present_h / present_w : ww, vh = wide ? wh : ww * present_w / present_h;
    vp((ww - vw) / 2, (wh - vh) / 2, vw, vh);
    cc(0, 0, 0, 1);
    cl(0x4000);
    en(0x0DE1);
    bt(0x0DE1, present_tex);
    te(0x2300, 0x2200, 0x1E01 /* REPLACE */);
    bb(GL_ARRAY_BUFFER, 0);
    float quad[] = { /* x, y, s, t: the portrait texture turned a quarter turn counter-clockwise */
        1, -1, 0, 0,   1, 1, 1, 0,   -1, -1, 0, 1,   -1, 1, 1, 1 };
    ecs(0x8074);
    ecs(0x8078);
    dcs(0x8076);
    dcs(0x8075);
    vptr(2, 0x1406 /* FLOAT */, 16, quad);
    tptr(2, 0x1406, 16, quad + 2);
    da(0x0005 /* TRIANGLE_STRIP */, 0, 4);

    /* the frame is shown by the caller's swap; put the game's state back */
    rt_platform_swap();
    wide_dirty = 1;
    bf(0x8D40, host_fb(bound_fb));
    bt(0x0DE1, 0);
    bb(GL_ARRAY_BUFFER, (uint32_t)av.buffer); vptr(av.size, av.type, av.stride, av.ptr); (av.enabled ? ecs : dcs)(0x8074);
    bb(GL_ARRAY_BUFFER, (uint32_t)an.buffer); nptr(an.type, an.stride, an.ptr); (an.enabled ? ecs : dcs)(0x8075);
    bb(GL_ARRAY_BUFFER, (uint32_t)ac.buffer); cptr(ac.size, ac.type, ac.stride, ac.ptr); (ac.enabled ? ecs : dcs)(0x8076);
    bb(GL_ARRAY_BUFFER, (uint32_t)ax.buffer); tptr(ax.size, ax.type, ax.stride, ax.ptr);
    bb(GL_ARRAY_BUFFER, array_buffer);
    for (int u = 1; u >= 0; u--) {
        at(0x84C0 + u);
        cat(0x84C0 + u);
        bt(0x0DE1, (uint32_t)old_tex[u]);
        te(0x2300, 0x2200, old_env[u]);
        (tex_on[u] ? en : di)(0x0DE1);
        (tcarr_on[u] ? ecs : dcs)(0x8078);
    }
    mm(0x1702); lm(tx);
    mm(0x1701); lm(pj);
    mm(0x1700); lm(mv);
    mm((E)old_mm);
    at((E)old_active);
    cat((E)old_client);
    cm(cmask[0], cmask[1], cmask[2], cmask[3]);
    c4(color[0], color[1], color[2], color[3]);
    cc(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    for (int k = 0; k < NCAPS; k++) (cap_on[k] ? en : di)(caps[k]);
    vp(vpt[0], vpt[1], vpt[2], vpt[3]);
    sc(scb[0], scb[1], scb[2], scb[3]);
}

void rt_gl_bind_drawable(uint32_t renderbuffer, int width, int height) {
    drawable_rb = renderbuffer;
    drawable_w = width;
    drawable_h = height;
    if (rt_verbose) rt_log("gl: drawable renderbuffer %u (%dx%d)", renderbuffer, width, height);
    for (int k = 0; k < MAX_ATTACH; k++)
        if (renderbuffer && color_att[k].fb && color_att[k].rb == renderbuffer) make_drawable_fb(color_att[k].fb);
    present_setup();
    if (present_fbo && is_drawable_fb(bound_fb)) host_bind_fb(0, bound_fb);
}

/* the window changed shape after the layer was made (Android reports a portrait window first, then turns it): give the
 * layer its new long side between two frames. The game redraws every frame, so the lost picture does not matter. */
static void present_resize(void) {
    int want = rt_gl_wide_long > drawable_h ? rt_gl_wide_long : drawable_h;
    T_glGetIntegerv gi = F(glGetIntegerv);
    T_glBindTexture bt = F(glBindTexture);
    T_glTexImage2D ti = F(glTexImage2D);
    T_glBindRenderbufferOES br = F(glBindRenderbufferOES);
    T_glRenderbufferStorageOES rs = F(glRenderbufferStorageOES);
    T_glCheckFramebufferStatusOES cs = F(glCheckFramebufferStatusOES);
    T_glBindFramebufferOES bf = F(glBindFramebufferOES);
    T_glFramebufferRenderbufferOES fr = F(glFramebufferRenderbufferOES);
    if (!fr || !present_fbo || !drawable_h || want == present_h || !gi || !bt || !ti || !br || !rs || !cs || !bf) return;
    I old_tex = 0, old_rb = 0;
    gi(0x8069, &old_tex);
    gi(GL_RENDERBUFFER_BINDING_OES, &old_rb);
    bt(0x0DE1, present_tex);
    ti(0x0DE1, 0, 0x1908, present_w, want, 0, 0x1908, 0x1401, NULL);
    br(GL_RENDERBUFFER_OES, present_ds);
    bf(0x8D40, present_fbo);
    E status = present_depth(rs, fr, cs, present_w, want);
    if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
        rt_log("gl: presentation resize failed (%#x); attempting previous size", status);
        ti(0x0DE1, 0, 0x1908, present_w, present_h, 0, 0x1908, 0x1401, NULL);
        status = present_depth(rs, fr, cs, present_w, present_h);
        if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
            rt_log("gl: presentation framebuffer unrecoverable (%#x)", status);
            present_failed = 1;
            present_fbo = 0;
        }
        want = present_h;
    }
    bf(0x8D40, host_fb(bound_fb));
    bt(0x0DE1, (uint32_t)old_tex);
    br(GL_RENDERBUFFER_OES, (uint32_t)old_rb);
    rt_log("gl: the layer is now %dx%d", present_w, want);
    present_h = want;
    wide_dirty = 1;
    proj_dirty = 1;
    wide_k = (float)drawable_h / present_h;
    T_glMatrixMode mm = F(glMatrixMode);
    T_glLoadMatrixf lm = F(glLoadMatrixf);
    if (proj_scaled && mm && lm) { float m[16]; sh_proj_for_gl(m); mm(0x1701); lm(m); mm((E)mat_mode); }
}

#ifdef NOVA_FRAME_LOG
void rt_gl_measure_render(int *width, int *height) {
    *width = present_fbo ? present_w : 0;
    *height = present_fbo ? present_h : 0;
}
#endif

#ifdef NOVA_FRAME_LOG
static void trace_gl_frame(void);
#endif

void rt_gl_present(void) {
#ifdef NOVA_FRAME_LOG
    if (atomic_load(&trace_enabled)) trace_gl_frame();
#endif
    if (present_fbo) { present_blit(); present_resize(); } /* the blit swaps */
    else rt_platform_swap();
    memcpy(hit_last, hit_cur, sizeof hit_cur[0] * (size_t)hit_cur_n);
    hit_last_n = hit_cur_n;
    hit_cur_n = 0;
    /* a few frames of grace: an effect drawn over the pause button for a moment must not shrink the controls
     * under the player's thumbs */
    hud_gone = hud_seen ? 0 : (hud_gone < 4 ? hud_gone + 1 : 4);
    hud_on = hud_gone < 4;
    hud_seen = 0;
    frame_3d = 0;
}

/* ---- pointers ---------------------------------------------------------------------------------------------- */
static const void *array_ptr(uint32_t v) { return array_buffer ? (const void *)(uintptr_t)v : G(v); }
static const void *index_ptr(uint32_t v) { return element_buffer ? (const void *)(uintptr_t)v : G(v); }

/* glGen*: host names, or distinct made-up names when headless */
static void gen_names(void (GLAPI *f)(I, E *), int32_t n, uint32_t out) {
    if (n <= 0 || !out) return;
    if (f) { f(n, (E *)G(out)); return; }
    for (int32_t k = 0; k < n; k++) rt_st32(out + 4u * (uint32_t)k, fake_names++);
}

/* ---- buffers --------------------------------------------------------------------------------------------- */
IMP(glBindBuffer) {
    E target = A(0), buf = A(1);
    if (target == GL_ARRAY_BUFFER) array_buffer = buf;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) element_buffer = buf;
    T_glBindBuffer f = F(glBindBuffer); if (f) f(target, buf);
}
IMP(glGenBuffers) { gen_names(F(glGenBuffers), (I)A(0), A(1)); }
IMP(glDeleteBuffers) {
    I n = (I)A(0); uint32_t p = A(1);
    for (I k = 0; k < n && p; k++) {
        uint32_t b = rt_ld32(p + 4u * (uint32_t)k);
        if (b && b == array_buffer) array_buffer = 0;
        if (b && b == element_buffer) element_buffer = 0;
    }
    T_glDeleteBuffers f = F(glDeleteBuffers); if (f && n > 0) f(n, (const E *)G(p));
}
IMP(glBufferData) { T_glBufferData f = F(glBufferData); if (f) f(A(0), (intptr_t)(I)A(1), G(A(2)), A(3)); }
IMP(glBufferSubData) { T_glBufferSubData f = F(glBufferSubData); if (f) f(A(0), (intptr_t)(I)A(1), (intptr_t)(I)A(2), G(A(3))); }

/* ---- client arrays and drawing ---------------------------------------------------------------------------------- */
static struct { I size, stride; E type; const uint8_t *ptr; } vtx; /* the vertex array; ptr NULL: in a buffer object */
IMP(glVertexPointer) {
    T_glVertexPointer f = F(glVertexPointer); if (f) f((I)A(0), A(1), (I)A(2), array_ptr(A(3)));
    vtx.size = (I)A(0); vtx.type = A(1); vtx.stride = (I)A(2);
    vtx.ptr = array_buffer ? NULL : (const uint8_t *)G(A(3));
}
IMP(glTexCoordPointer) { T_glTexCoordPointer f = F(glTexCoordPointer); if (f) f((I)A(0), A(1), (I)A(2), array_ptr(A(3))); }
IMP(glColorPointer) { T_glColorPointer f = F(glColorPointer); if (f) f((I)A(0), A(1), (I)A(2), array_ptr(A(3))); }
IMP(glNormalPointer) { T_glNormalPointer f = F(glNormalPointer); if (f) f(A(0), (I)A(1), array_ptr(A(2))); }
#ifdef NOVA_FRAME_LOG
static E guest_front_face = 0x0901; /* GL_CCW default */
static int cull_begin(void) {
    int mode = atomic_load(&cull_override);
    if (!mode || !*cap_slot(0x0B44)) return 0;
    T_glDisable disable = F(glDisable);
    T_glFrontFace front = F(glFrontFace);
    if (mode == 1 && disable && F(glEnable)) disable(0x0B44);
    else if (mode == 2 && front) front(guest_front_face == 0x0901 ? 0x0900 : 0x0901);
    else return 0;
    return mode;
}

static void cull_end(int mode) {
    if (mode == 1) F(glEnable)(0x0B44);
    else if (mode == 2) F(glFrontFace)(guest_front_face);
}
#endif

IMP(glDrawElements) {
    T_glDrawElements draw = F(glDrawElements);
    const void *indices = index_ptr(A(3));
#ifdef NOVA_FRAME_LOG
    int override = cull_begin();
#endif
    if (!wide_apply(2, A(0), 0, (I)A(1), A(2), indices) && draw)
        draw(A(0), (I)A(1), A(2), indices);
#ifdef NOVA_FRAME_LOG
    cull_end(override);
#endif
}

IMP(glDrawArrays) {
    T_glDrawArrays draw = F(glDrawArrays);
#ifdef NOVA_FRAME_LOG
    int override = cull_begin();
#endif
    if (!wide_apply(1, A(0), (I)A(1), (I)A(2), 0, NULL) && draw)
        draw(A(0), (I)A(1), (I)A(2));
#ifdef NOVA_FRAME_LOG
    cull_end(override);
#endif
}
IMP(glEnableClientState) { T_glEnableClientState f = F(glEnableClientState); if (f) f(A(0)); }
IMP(glDisableClientState) { T_glDisableClientState f = F(glDisableClientState); if (f) f(A(0)); }
/* A texture unit the host does not have is not passed on: a driver answers GL_INVALID_ENUM and stays on the unit it
 * was on, but the Android emulator's GL layer crashes at the next glIsEnabled of a client array. */
static int unit_bad;
static int unit_ok(E unit) {
    static I max_units;
    T_glGetIntegerv gi = F(glGetIntegerv);
    if (!max_units) { if (gi) gi(0x84E2, &max_units); if (max_units < 1) max_units = 2; }
    return unit >= 0x84C0 && unit < 0x84C0 + (E)max_units;
}
IMP(glClientActiveTexture) { T_glClientActiveTexture f = F(glClientActiveTexture); unit_bad = !unit_ok(A(0)); if (f && !unit_bad) f(A(0)); }
IMP(glGetPointerv) {
    uint32_t out = A(1);
    T_glGetPointerv f = F(glGetPointerv);
    if (!out) return;
    if (!f) { rt_st32(out, 0); return; }
    void *p = NULL;
    f(A(0), &p);
    uintptr_t v = (uintptr_t)p, base = (uintptr_t)rt_mem;
    /* a game pointer comes back as rt_mem + addr; a buffer offset as itself */
    rt_st32(out, (v >= base && v - base <= 0xffffffffu) ? (uint32_t)(v - base) : (uint32_t)v);
}

/* ---- widescreen -------------------------------------------------------------------------------------------------------
 * The game only knows the iPad's 4:3 screen. With rt_gl_wide_long set (the long side of a wider layer, by the host,
 * before launch) the presentation framebuffer is 768 x rt_gl_wide_long and what the game draws into its drawable is
 * placed just before each draw or clear:
 *   - under a perspective projection (the 3D scene) the viewport and scissor are stretched over the whole layer and the
 *     projection is pre-multiplied by a scale of 1024/long along the layer's long axis, so the scene keeps its
 *     proportions and shows more to the left and right;
 *   - under any other projection (menus, HUD) the viewport and scissor are moved to the middle 768x1024.
 * A pre-multiplied scale survives the game's later glMultMatrix/glRotate calls; glLoadMatrix/glLoadIdentity drop it and
 * it is applied again. Framebuffers of the game's own are left alone. */
int rt_gl_wide_long;
static I game_vp[4] = {0, 0, 768, 1024}, game_sc[4] = {0, 0, 768, 1024};
static int proj_persp;
static int proj_stack[4], proj_sp;
#define PROJ_TOUCHED() do { if (mat_mode == 0x1701) proj_dirty = 1; } while (0)

/* where a 2D element goes by its centre across the screen (0..1): -1 the left edge, 0 the middle, 1 the right edge */
static float wide_s(float c) {
    float d = fabsf(c - 0.5f), s = d <= 0.08f ? 0 : d >= 0.20f ? 1 : (d - 0.08f) / 0.12f;
    return c < 0.5f ? -s : s;
}

/* a position across the wide screen (0..long side) -> the game's own 0..1024, for touches on moved 2D elements */
float rt_gl_wide_game_x(float d) {
    float g = drawable_h ? (float)drawable_h : 1024.0f, m = (rt_gl_wide_long - g) / 2, lo = 0, hi = g;
    if (rt_gl_wide_long <= g) return d;
    for (int k = 0; k < 24; k++) {
        float x = (lo + hi) / 2;
        if (x + m + m * wide_s(x / g) < d) lo = x; else hi = x;
    }
    return (lo + hi) / 2;
}

/* a touch at (px across 0..long side, py down 0..768) of the wide screen -> the game's own point:
 * x = px * t[0] + t[1], y = py * t[2] + t[3]. Asked when a touch begins and kept for that touch. */
void rt_gl_wide_touch(float px, float py, float *t) {
    float g = drawable_h ? (float)drawable_h : 1024.0f, best = 1e30f;
    t[0] = 1; t[1] = 0; t[2] = 1; t[3] = 0;
    if (rt_gl_wide_long <= g) return;
    const struct hit *h = NULL;
    /* an enlarged element: the smallest one under the finger. One at the regular place of its zone wins over one
     * that was moved up from the bottom edge (a marker passing over the fire button must not take its touches). */
    for (int k = 0; k < hit_last_n && k < HIT_MAX; k++) {
        const struct hit *c = &hit_last[k];
        float area = (c->y1 - c->y0) * (c->x1 - c->x0) + (c->moved ? 1e9f : 0);
        if (px >= c->y0 && px <= c->y1 && py >= c->x0 && py <= c->x1 && area < best) { best = area; h = c; }
    }
    if (h) { memcpy(t, h->t, sizeof h->t); return; }
    t[1] = rt_gl_wide_game_x(px) - px;
}

/* the extent of a draw along the layer's long axis in normalised device coordinates; 0 when it cannot be read */
enum { EXT_MAX = 8190 };
static float ext_y[EXT_MAX];    /* per element of the last draw_extent: its position along the long axis */
static float ext_x[EXT_MAX];    /* and along the short axis */
static float ext_xa, ext_xb;    /* the whole draw along the short axis */
static uint32_t ext_i[EXT_MAX]; /* and its vertex index */
static int draw_extent(int kind, I first, I count, E itype, const void *idx, float *lo, float *hi) {
    if (!vtx.ptr || count <= 0 || count > EXT_MAX || vtx.size < 2 || first < 0) return 0;
    if (kind == 2 && (element_buffer || !idx || (itype != 0x1401 && itype != 0x1403))) return 0;
    int esz = vtx.type == 0x1406 || vtx.type == 0x140C ? 4 : vtx.type == 0x1402 ? 2 : vtx.type == 0x1400 ? 1 : 0;
    if (!esz) return 0;
    int stride = vtx.stride ? vtx.stride : esz * vtx.size;
    const float *mv = sh_mv[sh_mv_sp], *pj = sh_pj[sh_pj_sp];
    float rx[4], ry[4], rw[4], xa = 1e30f, xb = -1e30f; /* rows 0, 1 and 3 of projection * modelview */
    for (int c = 0; c < 4; c++) {
        rx[c] = pj[0] * mv[4 * c] + pj[4] * mv[4 * c + 1] + pj[8] * mv[4 * c + 2] + pj[12] * mv[4 * c + 3];
        ry[c] = pj[1] * mv[4 * c] + pj[5] * mv[4 * c + 1] + pj[9] * mv[4 * c + 2] + pj[13] * mv[4 * c + 3];
        rw[c] = pj[3] * mv[4 * c] + pj[7] * mv[4 * c + 1] + pj[11] * mv[4 * c + 2] + pj[15] * mv[4 * c + 3];
    }
    float a = 1e30f, b = -1e30f;
    for (I k = 0; k < count; k++) {
        uint32_t i = kind == 2 ? (itype == 0x1401 ? ((const uint8_t *)idx)[k] : ((const uint16_t *)idx)[k]) : (uint32_t)(first + k);
        const uint8_t *p = vtx.ptr + (size_t)i * (size_t)stride;
        float v[4] = {0, 0, 0, 1};
        for (int c = 0; c < vtx.size && c < 4; c++) {
            if (vtx.type == 0x1406) { memcpy(&v[c], p + 4 * c, 4); }
            else if (vtx.type == 0x140C) { int32_t t; memcpy(&t, p + 4 * c, 4); v[c] = t / 65536.0f; }
            else if (vtx.type == 0x1402) { int16_t t; memcpy(&t, p + 2 * c, 2); v[c] = t; }
            else v[c] = (int8_t)p[c];
        }
        float w = rw[0] * v[0] + rw[1] * v[1] + rw[2] * v[2] + rw[3] * v[3];
        if (fabsf(w) < 1e-6f) return 0;
        float y = (ry[0] * v[0] + ry[1] * v[1] + ry[2] * v[2] + ry[3] * v[3]) / w;
        if (y < a) a = y;
        if (y > b) b = y;
        float x = (rx[0] * v[0] + rx[1] * v[1] + rx[2] * v[2] + rx[3] * v[3]) / w;
        if (x < xa) xa = x;
        if (x > xb) xb = x;
        ext_y[k] = y; ext_x[k] = x; ext_i[k] = i;
    }
    ext_xa = xa; ext_xb = xb;
    *lo = a; *hi = b;
    return 1;
}

/* a 2D element spanning lo..hi (normalised device coordinates of the game's viewport): its placement on the layer */
static void wide_place(float lo, float hi, float xa, float xb, struct place *p) {
    float g = (float)drawable_h, w = drawable_w ? (float)drawable_w : 768.0f;
    float a = (game_vp[1] + (lo + 1) / 2 * game_vp[3]) / g, b = (game_vp[1] + (hi + 1) / 2 * game_vp[3]) / g;
    /* down the screen (0 the top, 1 the bottom): the layer's short axis runs the other way */
    float va = 1 - (game_vp[0] + (xb + 1) / 2 * game_vp[2]) / w, vb = 1 - (game_vp[0] + (xa + 1) / 2 * game_vp[2]) / w;
    int m = (present_h - drawable_h) / 2;
    /* stretched: what spans the screen (backgrounds, bars, fades) and tall pieces of a tiled background; a line of
     * text that happens to reach one edge is not */
    int both = a <= 0.02f && b >= 0.98f, one = a <= 0.02f || b >= 0.98f;
    float tall = vb - va, side = wide_s((a + b) / 2), s = HUD_SCALE;
    p->sx = 1; p->ox = 0;
    if (both || (one && b - a >= 0.45f && tall >= 0.25f)) { p->sy = present_h / g; p->oy = 0; return; }
    p->sy = 1; p->oy = (float)(m + (int)(m * side)); /* smaller: same size, moved towards its edge */
    /* the HUD's controls at the left and right: enlarged from their edge of the screen (as far as its width allows).
     * Down the screen the upper ones grow from the top; the lower ones (stick, fire) grow about the line at 0.8 of
     * the height, which lands on the bottom edge, so they come down towards the thumbs. What would still pass the
     * bottom is moved up to fit. */
    if (s * g > present_h) s = present_h / g;
    /* the layer shows turned half round: its long axis runs from the right of the screen to the left */
    if (frame_3d && a > 0.80f && vb < 0.12f && b - a > 0.03f) hud_seen = 1;
    if (frame_3d && hud_on && fabsf(side) >= 1 && b - a < 0.35f && tall < 0.35f && s > 1.05f) {
        p->sy = p->sx = s;
        p->oy = side < 0 ? 0 : present_h - s * g;
        float base;
        if ((va + vb) / 2 < 0.45f) { p->ox = base = w * (1 - s); if (s * vb > 1) p->ox += w * (s * vb - 1); }
        else { p->ox = base = -0.2f * w * s; if (vb > 0.8f) p->ox += w * s * (vb - 0.8f); }
        if (hit_cur_n < HIT_MAX) {
            struct hit *h = &hit_cur[hit_cur_n++];
            h->y0 = present_h - (b * g * s + p->oy) - 12; h->y1 = present_h - (a * g * s + p->oy) + 12;
            h->x0 = w - ((1 - va) * w * s + p->ox) - 12; h->x1 = w - ((1 - vb) * w * s + p->ox) + 12;
            h->t[0] = 1 / s; h->t[1] = g - (present_h - p->oy) / s;
            h->t[2] = 1 / s; h->t[3] = w - (w - p->ox) / s;
            h->moved = p->ox != base;
        }
    }
}

static void wide_set(const struct place *p) {
    static struct place last;
    T_glViewport vp = F(glViewport);
    T_glScissor sc = F(glScissor);
    if (!wide_dirty && !memcmp(p, &last, sizeof last)) return;
    wide_dirty = 0;
    last = *p;
    vp((I)lroundf(game_vp[0] * p->sx + p->ox), (I)lroundf(game_vp[1] * p->sy + p->oy),
       (I)lroundf(game_vp[2] * p->sx), (I)lroundf(game_vp[3] * p->sy));
    sc((I)lroundf(game_sc[0] * p->sx + p->ox), (I)lroundf(game_sc[1] * p->sy + p->oy),
       (I)lroundf(game_sc[2] * p->sx), (I)lroundf(game_sc[3] * p->sy));
}

/* A batch of triangles holding several separate 2D elements (the HUD's buttons in one draw): the triangles are grouped
 * into elements by their extent across the screen (touching or nearly touching ones belong together), and each group
 * is drawn with its own placement. Groups do not overlap, so drawing them one after another keeps the picture. Uses ext_y/ext_i from draw_extent. Returns 0 (nothing drawn) for a batch that is one element. */
static int wide_split(I count) {
    enum { MAXG = 48 };
    static uint16_t order[EXT_MAX / 3], out[EXT_MAX];
    static uint8_t group[EXT_MAX / 3];
    static float ta[EXT_MAX / 3], tb[EXT_MAX / 3];
    T_glDrawElements de = F(glDrawElements);
    T_glBindBuffer bb = F(glBindBuffer);
    int nt = count / 3;
    if (nt < 2 || !de || !bb) return 0;
    for (int t = 0; t < nt; t++) {
        float a = ext_y[3 * t], b = a;
        for (int k = 1; k < 3; k++) { float y = ext_y[3 * t + k]; if (y < a) a = y; if (y > b) b = y; }
        if (ext_i[3 * t] > 0xffff || ext_i[3 * t + 1] > 0xffff || ext_i[3 * t + 2] > 0xffff) return 0;
        ta[t] = a; tb[t] = b; order[t] = (uint16_t)t;
    }
    for (int i = 1; i < nt; i++) { /* by left edge */
        uint16_t t = order[i]; int j = i;
        while (j > 0 && ta[order[j - 1]] > ta[t]) { order[j] = order[j - 1]; j--; }
        order[j] = t;
    }
    float glo[MAXG], ghi[MAXG], gxa[MAXG], gxb[MAXG];
    static uint8_t group1[EXT_MAX / 3];
    static uint16_t ord2[EXT_MAX / 3];
    static float xa[EXT_MAX / 3], xb[EXT_MAX / 3];
    int ng1 = 0, ng = 0;
    float hi1 = 0;
    for (int i = 0; i < nt; i++) { /* across the screen */
        int t = order[i];
        if (ng1 && ta[t] <= hi1 + 0.06f) { if (tb[t] > hi1) hi1 = tb[t]; }
        else { if (ng1 == MAXG) return 0; hi1 = tb[t]; ng1++; }
        group1[t] = (uint8_t)(ng1 - 1);
        float a = ext_x[3 * t], b = a;
        for (int k = 1; k < 3; k++) { float x = ext_x[3 * t + k]; if (x < a) a = x; if (x > b) b = x; }
        xa[t] = a; xb[t] = b;
    }
    for (int g = 0; g < ng1; g++) { /* and each of those down the screen: the stick is not part of the map above it */
        int n = 0;
        for (int t = 0; t < nt; t++) {
            if (group1[t] != g) continue;
            int j = n++;
            while (j > 0 && xa[ord2[j - 1]] > xa[t]) { ord2[j] = ord2[j - 1]; j--; }
            ord2[j] = (uint16_t)t;
        }
        for (int i = 0; i < n; i++) {
            int t = ord2[i];
            if (i && xa[t] <= gxb[ng - 1] + 0.08f) { if (xb[t] > gxb[ng - 1]) gxb[ng - 1] = xb[t]; }
            else { if (ng == MAXG) return 0; gxa[ng] = xa[t]; gxb[ng] = xb[t]; glo[ng] = 1e30f; ghi[ng] = -1e30f; ng++; }
            if (ta[t] < glo[ng - 1]) glo[ng - 1] = ta[t];
            if (tb[t] > ghi[ng - 1]) ghi[ng - 1] = tb[t];
            group[t] = (uint8_t)(ng - 1);
        }
    }
    if (ng < 2) return 0;
    if (element_buffer) bb(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (int g = 0; g < ng; g++) {
        int n = 0;
        struct place p;
        for (int t = 0; t < nt; t++)
            if (group[t] == g) for (int k = 0; k < 3; k++) out[n++] = (uint16_t)ext_i[3 * t + k];
        wide_place(glo[g], ghi[g], gxa[g], gxb[g], &p);
        wide_set(&p);
        de(0x0004, n, 0x1403, out);
    }
    if (element_buffer) bb(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    return 1;
}

/* before a clear or draw; kind: 0 clear, 1 arrays, 2 elements. Returns 1 when it has done the draw itself. */
static int wide_apply(int kind, E mode, I first, I count, E itype, const void *idx) {
    if (!rt_gl_wide_long || !present_fbo || !drawable_h) return 0;
    T_glMatrixMode mm = F(glMatrixMode);
    T_glLoadMatrixf lm = F(glLoadMatrixf);
    if (!mm || !lm || !F(glViewport) || !F(glScissor)) return 0;
    int on = is_drawable_fb(bound_fb);
    float m[16];
    wide_k = (float)drawable_h / present_h;
    if (proj_dirty) {
        const float *p = sh_pj[sh_pj_sp];
        proj_persp = fabsf(p[3]) + fabsf(p[7]) + fabsf(p[11]) > 1e-6f;
        proj_dirty = 0;
        wide_dirty = 1;
    }
    int want = on && proj_persp;
    /* what is drawn clipped to a small scissor rectangle (the minimap's terrain: one large mesh and its markers) is
     * one element, placed by that rectangle like the frame drawn around it */
    int clip = on && *cap_slot(0x0C11) && game_sc[2] > 0 && game_sc[3] > 0 && game_vp[2] > 0 && game_vp[3] > 0 &&
               game_sc[2] * 10 < drawable_w * 6 && game_sc[3] * 10 < drawable_h * 6;
    if (want != proj_scaled) {
        proj_scaled = want;
        sh_proj_for_gl(m);
        mm(0x1701);
        lm(m);
        mm((E)mat_mode);
        wide_dirty = 1;
    }
    /* placement along the long axis: y' = y * num / drawable_h + off */
    struct place p = {1, 0, 1, 0};
    if (on && want && kind) frame_3d = 1;
    if (clip && (!want || kind == 0))
        wide_place((game_sc[1] - game_vp[1]) * 2.0f / game_vp[3] - 1, (game_sc[1] + game_sc[3] - game_vp[1]) * 2.0f / game_vp[3] - 1,
                   (game_sc[0] - game_vp[0]) * 2.0f / game_vp[2] - 1, (game_sc[0] + game_sc[2] - game_vp[0]) * 2.0f / game_vp[2] - 1, &p);
    else if (on && (want || kind == 0)) p.sy = (float)present_h / drawable_h; /* the 3D scene and clears: the whole layer */
    else if (on) {
        float lo, hi;
        p.oy = (float)((present_h - drawable_h) / 2);
        if (game_vp[3] > 0 && draw_extent(kind, first, count, itype, idx, &lo, &hi)) {
            if (mode == 0x0004 && wide_split(count)) return 1;
            wide_place(lo, hi, ext_xa, ext_xb, &p);
        }
    }
    wide_set(&p);
    return 0;
}

#ifdef NOVA_FRAME_LOG
static void trace_gl_frame(void) {
    static int prior_hud = -1, prior_seen = -1;
    static unsigned tick;
    if (++tick % 15 && prior_hud == hud_on && prior_seen == hud_seen) return;
    prior_hud = hud_on;
    prior_seen = hud_seen;
    T_glGetIntegerv gi = F(glGetIntegerv);
    T_glGetFloatv gf = F(glGetFloatv);
    T_glGetBooleanv gb = F(glGetBooleanv);
    if (!gi || !gf || !gb) return;
    I viewport[4], scissor[4], fbo, cull, front, depth_func, depth_bits, stencil_bits;
    float projection[16], modelview[16], depth_range[2];
    uint8_t depth_mask;
    gi(0x0BA2, viewport);
    gi(0x0C10, scissor);
    gi(GL_FRAMEBUFFER_BINDING_OES, &fbo);
    gi(0x0B45, &cull);
    gi(0x0B46, &front);
    gi(0x0B74, &depth_func);
    gi(0x0D56, &depth_bits);
    gi(0x0D57, &stencil_bits);
    gb(0x0B72, &depth_mask);
    gf(0x0BA6, modelview);
    gf(0x0BA7, projection);
    gf(0x0B70, depth_range);
    char line[480];
    snprintf(line, sizeof line,
             "HUD %d/%d hit %d | logical %dx%d FBO %d (%dx%d) packed=%x final=%x\n"
             "VP iOS %d,%d,%d,%d GL %d,%d,%d,%d | SC %d %d,%d,%d,%d\n"
             "P %g,%g,%g,%g MV %g,%g,%g,%g | cull %d/%x/%x mode=%d\n"
             "depth %d/%d/%x range %.2f:%.2f stencil %d bits %d/%d",
             hud_on, hud_seen, hit_cur_n, drawable_w, drawable_h, fbo, present_w, present_h, packed_status, depth_status,
             game_vp[0], game_vp[1], game_vp[2], game_vp[3], viewport[0], viewport[1], viewport[2], viewport[3],
             *cap_slot(0x0C11), scissor[0], scissor[1], scissor[2], scissor[3],
             projection[0], projection[5], projection[11], projection[15],
             modelview[0], modelview[5], modelview[10], modelview[15],
             *cap_slot(0x0B44), cull, front, atomic_load(&cull_override), *cap_slot(0x0B71), depth_mask,
             depth_func, depth_range[0], depth_range[1], *cap_slot(0x0B90), depth_bits, stencil_bits);
    rt_platform_graphics_sample(line);
    rt_log("graphics %s", line);
}
#endif

/* ---- framebuffers and renderbuffers (the drawable) ----------------------------------------------------------------- */
IMP(glGenFramebuffersOES) { gen_names(F(glGenFramebuffersOES), (I)A(0), A(1)); }
IMP(glGenRenderbuffersOES) { gen_names(F(glGenRenderbuffersOES), (I)A(0), A(1)); }
IMP(glBindFramebufferOES) {
    bound_fb = A(1);
    wide_dirty = 1;
    host_bind_fb(A(0), bound_fb);
}
IMP(glBindRenderbufferOES) {
    bound_rb = A(1);
    T_glBindRenderbufferOES f = F(glBindRenderbufferOES); if (f) f(A(0), bound_rb);
}
IMP(glDeleteFramebuffersOES) {
    I n = (I)A(0); uint32_t p = A(1);
    for (I k = 0; k < n && p; k++) {
        uint32_t fb = rt_ld32(p + 4u * (uint32_t)k);
        if (!fb) continue;
        if (fb == bound_fb) bound_fb = 0;
        if (fb == drawable_fb) { drawable_fb = 0; memset(drawable_extra_rb, 0, sizeof drawable_extra_rb); }
        set_color_att(fb, 0);
    }
    T_glDeleteFramebuffersOES f = F(glDeleteFramebuffersOES); if (f && n > 0) f(n, (const E *)G(p));
}
IMP(glDeleteRenderbuffersOES) {
    I n = (I)A(0); uint32_t p = A(1);
    for (I k = 0; k < n && p; k++) {
        uint32_t rb = rt_ld32(p + 4u * (uint32_t)k);
        if (!rb) continue;
        if (rb == bound_rb) bound_rb = 0;
        if (rb == drawable_rb) drawable_rb = 0;
        for (int j = 0; j < MAX_ATTACH; j++) {
            if (drawable_extra_rb[j] == rb) drawable_extra_rb[j] = 0;
            if (color_att[j].fb && color_att[j].rb == rb) color_att[j].fb = color_att[j].rb = 0;
        }
    }
    T_glDeleteRenderbuffersOES f = F(glDeleteRenderbuffersOES); if (f && n > 0) f(n, (const E *)G(p));
}
IMP(glRenderbufferStorageOES) {
    /* the window's colour buffer, and the depth buffer the EGL surface already has */
    if (bound_rb && (bound_rb == drawable_rb || is_drawable_extra(bound_rb))) return;
    T_glRenderbufferStorageOES f = F(glRenderbufferStorageOES); if (f) f(A(0), A(1), (I)A(2), (I)A(3));
}
IMP(glFramebufferRenderbufferOES) {
    E target = A(0), attachment = A(1), rbtarget = A(2), rb = A(3);
    if (attachment == GL_COLOR_ATTACHMENT0_OES && bound_fb) set_color_att(bound_fb, rb);
    if (attachment == GL_COLOR_ATTACHMENT0_OES && rb && rb == drawable_rb && bound_fb) {
        make_drawable_fb(bound_fb);
        return;
    }
    if (is_drawable_fb(bound_fb)) {
        if (attachment != GL_COLOR_ATTACHMENT0_OES) add_drawable_extra(attachment, rb);
        return; /* the window surface's own buffers stand in */
    }
    T_glFramebufferRenderbufferOES f = F(glFramebufferRenderbufferOES); if (f) f(target, attachment, rbtarget, rb);
}
IMP(glFramebufferTexture2DOES) {
    if (is_drawable_fb(bound_fb)) { rt_log("gl: texture attached to the window framebuffer (ignored)"); return; }
    if (A(1) == GL_COLOR_ATTACHMENT0_OES && bound_fb) set_color_att(bound_fb, 0);
    T_glFramebufferTexture2DOES f = F(glFramebufferTexture2DOES); if (f) f(A(0), A(1), A(2), A(3), (I)A(4));
}
IMP(glCheckFramebufferStatusOES) {
    T_glCheckFramebufferStatusOES f = F(glCheckFramebufferStatusOES);
    if (is_drawable_fb(bound_fb) || !f) { RET(GL_FRAMEBUFFER_COMPLETE_OES); return; }
    RET(f(A(0)));
}
IMP(glGetRenderbufferParameterivOES) {
    E pname = A(1); uint32_t out = A(2);
    if (!out) return;
    if (bound_rb && bound_rb == drawable_rb) {
        I v = 0;
        switch (pname) {
        case GL_RENDERBUFFER_WIDTH_OES: v = drawable_w; break;
        case GL_RENDERBUFFER_HEIGHT_OES: v = drawable_h; break;
        case GL_RENDERBUFFER_INTERNAL_FORMAT_OES: v = GL_RGBA8_OES; break;
        case GL_RENDERBUFFER_RED_SIZE_OES: case GL_RENDERBUFFER_GREEN_SIZE_OES:
        case GL_RENDERBUFFER_BLUE_SIZE_OES: case GL_RENDERBUFFER_ALPHA_SIZE_OES: v = 8; break;
        case GL_RENDERBUFFER_DEPTH_SIZE_OES: case GL_RENDERBUFFER_STENCIL_SIZE_OES: v = 0; break;
        default: break;
        }
        rt_st32(out, (uint32_t)v);
        return;
    }
    T_glGetRenderbufferParameterivOES f = F(glGetRenderbufferParameterivOES);
    if (f) f(A(0), pname, (I *)G(out));
    else rt_st32(out, 0);
}
IMP(glGetFramebufferAttachmentParameterivOES) {
    E attachment = A(1), pname = A(2); uint32_t out = A(3);
    if (!out) return;
    if (is_drawable_fb(bound_fb)) {
        uint32_t rb = attachment == GL_COLOR_ATTACHMENT0_OES ? drawable_rb : drawable_extra_for(attachment);
        uint32_t v = 0;
        if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_OES) v = rb ? GL_RENDERBUFFER_OES : 0;
        else if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_OES) v = rb;
        rt_st32(out, v);
        return;
    }
    T_glGetFramebufferAttachmentParameterivOES f = F(glGetFramebufferAttachmentParameterivOES);
    if (f) f(A(0), attachment, pname, (I *)G(out));
    else rt_st32(out, 0);
}

/* ---- queries -------------------------------------------------------------------------------------------------- */
IMP(glGetIntegerv) {
    E pname = A(0); uint32_t out = A(1);
    if (!out) return;
    switch (pname) { /* bindings: the game's names (the host would say 0 for the window) */
    case GL_RENDERBUFFER_BINDING_OES: rt_st32(out, bound_rb); return;
    case GL_FRAMEBUFFER_BINDING_OES: rt_st32(out, bound_fb); return;
    case GL_ARRAY_BUFFER_BINDING: rt_st32(out, array_buffer); return;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: rt_st32(out, element_buffer); return;
    default: break;
    }
    T_glGetIntegerv f = F(glGetIntegerv);
    if (f) f(pname, (I *)G(out));
    else rt_st32(out, 0);
}
/* headless: the first value reads as 0 instead of whatever the game's buffer held */
IMP(glGetFloatv) {
    T_glGetFloatv f = F(glGetFloatv); uint32_t out = A(1);
    if (!out) return;
    if (f) f(A(0), (float *)G(out)); else rt_st32(out, 0);
}
IMP(glGetBooleanv) {
    T_glGetBooleanv f = F(glGetBooleanv); uint32_t out = A(1);
    if (!out) return;
    if (f) f(A(0), (uint8_t *)G(out)); else rt_mem[out] = 0;
}
IMP(glGetError) { T_glGetError f = F(glGetError); RET(f ? f() : GL_NO_ERROR); }
IMP(glIsEnabled) {
    T_glIsEnabled f = F(glIsEnabled);
    E cap = A(0);
    if (cap == 0x0DE1) RET(tex2d_on[act_unit]);
    else if (cap >= 0x8074 && cap <= 0x8078) RET(f && !(unit_bad && cap == 0x8078) ? f(cap) : 0); /* client arrays */
    else RET(*cap_slot(cap));
}

static struct { uint32_t name, addr; } strings[16];
IMP(glGetString) {
    E name = A(0);
    for (int k = 0; k < 16; k++) if (strings[k].addr && strings[k].name == name) { RET(strings[k].addr); return; }
    T_glGetString f = F(glGetString);
    const char *s = f ? f(name) : NULL;
    if (!s) { RET(0); return; }
    uint32_t a;
    if (name == 0x1F03 /* GL_EXTENSIONS */) {
        /* the formats emulated below (PVRTC decoded in software, BGRA swizzled) are listed as supported: the game
         * matches this list against its table of extension names (0x2d8008) */
        static const char *const extra[] = {"GL_IMG_texture_compression_pvrtc", "GL_APPLE_texture_format_BGRA8888",
                                            "GL_EXT_texture_format_BGRA8888"};
        size_t len = strlen(s), cap = len + 1;
        for (size_t k = 0; k < sizeof extra / sizeof extra[0]; k++) cap += strlen(extra[k]) + 1;
        char *buf = malloc(cap);
        if (!buf) { RET(0); return; }
        memcpy(buf, s, len + 1);
        for (size_t k = 0; k < sizeof extra / sizeof extra[0]; k++) {
            size_t n = strlen(extra[k]);
            int have = 0;
            for (const char *p = s; !have && (p = strstr(p, extra[k])) != NULL; p += n)
                have = (p == s || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0);
            if (have) continue;
            if (len && buf[len - 1] != ' ') buf[len++] = ' ';
            memcpy(buf + len, extra[k], n + 1);
            len += n;
        }
        a = rt_static_string(buf);
        free(buf);
    } else {
        a = rt_static_string(s);
    }
    if (rt_verbose) rt_log("gl: glGetString(0x%x) = %s", name, s);
    for (int k = 0; k < 16; k++) if (!strings[k].addr) { strings[k].name = name; strings[k].addr = a; break; }
    RET(a);
}

/* ---- PVRTC (software fallback) ------------------------------------------------------------------------------------
 * The game's textures are PVRTC1 (GL_IMG_texture_compression_pvrtc, 0x8C00-0x8C03: the loader at 0x2dcea0 picks
 * them and calls glCompressedTexImage2D, then checks glGetError). Only PowerVR GPUs decode that format; elsewhere
 * (ANGLE, Adreno, Mali) each level is decoded here to RGBA8 and given to glTexImage2D instead.
 *
 * Format (as in the PowerVR SDK's PVRTDecompress): 64-bit blocks of 4x4 (4bpp) or 8x4 (2bpp) pixels, at least 2x2
 * blocks, stored in Morton order (y in the low bit; for non-square sizes the larger axis keeps its extra high bits
 * on top). Block = modulation word (bytes 0-3), then colour word (bytes 4-7). Colour word: bit 0 mode; bits 1-15
 * colour A (bit 15 set: RGB 5-5-4, else ARGB 3-4-4-3); bits 16-31 colour B (bit 31 set: RGB 5-5-5, else ARGB
 * 3-4-4-4). Colours A and B are upscaled bilinearly between block centres (wrapping at the edges), then mixed per
 * pixel by the modulation weight (out of 8):
 *   4bpp: 2 bits per pixel, weights {0,3,5,8}; mode 1 = punch-through {0,4,4 with alpha 0,8}.
 *   2bpp: mode 0: 1 bit per pixel, weights {0,8}; mode 1: 2-bit weights on a checkerboard, the other pixels
 *         averaged from their neighbours (modulation bit 0 / bit 20 choose H+V, H only or V only). */
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03
#define GL_EXTENSIONS 0x1F03

typedef struct { int r, g, b, a; } pvr_col; /* r, g, b 5 bits; a 4 bits */

static pvr_col pvr_colour_a(uint32_t w) {
    pvr_col k;
    if (w & 0x8000) {
        k.r = (int)((w & 0x7c00) >> 10);
        k.g = (int)((w & 0x3e0) >> 5);
        k.b = (int)(w & 0x1e) | (int)((w & 0x1e) >> 4);
        k.a = 0xf;
    } else {
        k.r = (int)((w & 0xf00) >> 7) | (int)((w & 0xf00) >> 11);
        k.g = (int)((w & 0xf0) >> 3) | (int)((w & 0xf0) >> 7);
        k.b = (int)((w & 0xe) << 1) | (int)((w & 0xe) >> 2);
        k.a = (int)((w & 0x7000) >> 11);
    }
    return k;
}
static pvr_col pvr_colour_b(uint32_t w) {
    pvr_col k;
    if (w & 0x80000000u) {
        k.r = (int)((w & 0x7c000000) >> 26);
        k.g = (int)((w & 0x3e00000) >> 21);
        k.b = (int)((w & 0x1f0000) >> 16);
        k.a = 0xf;
    } else {
        k.r = (int)((w & 0xf000000) >> 23) | (int)((w & 0xf000000) >> 27);
        k.g = (int)((w & 0xf00000) >> 19) | (int)((w & 0xf00000) >> 23);
        k.b = (int)((w & 0xf0000) >> 15) | (int)((w & 0xf0000) >> 19);
        k.a = (int)((w & 0x70000000) >> 27);
    }
    return k;
}

static uint32_t pvr_twiddle(uint32_t xs, uint32_t ys, uint32_t x, uint32_t y) {
    uint32_t minax = xs < ys ? xs : ys, out = 0, bit = 1, dst = 1;
    int shift = 0;
    while (bit < minax) {
        if (y & bit) out |= dst;
        if (x & bit) out |= dst << 1;
        bit <<= 1; dst <<= 2; shift++;
    }
    out |= ((xs > ys ? x : y) >> shift) << (2 * shift);
    return out;
}

typedef struct {
    const uint8_t *data;
    int nbx, nby, bw; /* blocks across/down, block width in pixels (height is 4) */
    int bpp4;
} pvr_img;

static void pvr_block(const pvr_img *im, int bx, int by, uint32_t *mod, uint32_t *col) {
    uint32_t x = (uint32_t)((bx % im->nbx + im->nbx) % im->nbx);
    uint32_t y = (uint32_t)((by % im->nby + im->nby) % im->nby);
    const uint8_t *p = im->data + 8u * pvr_twiddle((uint32_t)im->nbx, (uint32_t)im->nby, x, y);
    *mod = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    *col = (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
}

static const int pvr_rep[4] = {0, 3, 5, 8};

/* 2bpp: the weight stored for a pixel (wrapping); asked only for pixels that store one */
static int pvr_stored2(const pvr_img *im, int px, int py) {
    int W = im->nbx * 8, H = im->nby * 4;
    px = (px % W + W) % W; py = (py % H + H) % H;
    uint32_t mod, col;
    pvr_block(im, px / 8, py / 4, &mod, &col);
    int x = px % 8, y = py % 4;
    if (!(col & 1)) return (mod >> (y * 8 + x)) & 1 ? 8 : 0;
    /* bits 0 and 20 are flags: those pixels repeat their other bit */
    if (mod & 1) { if (mod & (1u << 21)) mod |= 1u << 20; else mod &= ~(1u << 20); }
    if (mod & 2) mod |= 1; else mod &= ~1u;
    return pvr_rep[(mod >> (2 * ((y * 8 + x) / 2))) & 3];
}

/* modulation weight 0..8 of a pixel; *punch set for a punch-through (alpha 0) pixel */
static int pvr_weight(const pvr_img *im, int px, int py, int *punch) {
    uint32_t mod, col;
    int bw = im->bw;
    pvr_block(im, px / bw, py / 4, &mod, &col);
    int x = px % bw, y = py % 4;
    *punch = 0;
    if (im->bpp4) {
        int v = (int)((mod >> (2 * (y * 4 + x))) & 3);
        if (!(col & 1)) return pvr_rep[v];
        if (v == 2) *punch = 1;
        return v == 0 ? 0 : v == 3 ? 8 : 4;
    }
    if (!(col & 1)) return (mod >> (y * 8 + x)) & 1 ? 8 : 0;
    if (((x ^ y) & 1) == 0) return pvr_stored2(im, px, py);
    if (!(mod & 1)) /* horizontal and vertical */
        return (pvr_stored2(im, px, py - 1) + pvr_stored2(im, px, py + 1) + pvr_stored2(im, px - 1, py) +
                pvr_stored2(im, px + 1, py) + 2) / 4;
    if (mod & (1u << 20)) /* vertical only */
        return (pvr_stored2(im, px, py - 1) + pvr_stored2(im, px, py + 1) + 1) / 2;
    return (pvr_stored2(im, px - 1, py) + pvr_stored2(im, px + 1, py) + 1) / 2; /* horizontal only */
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static int is_pvrtc(E format) { return format >= GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG && format <= GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG; }

/* decode a w x h level into out (w*h RGBA8); 0 if the data is too short */
static int pvr_decode(const uint8_t *data, uint32_t size, int w, int h, E format, uint8_t *out) {
    pvr_img im;
    im.data = data;
    im.bpp4 = format == GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG || format == GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
    im.bw = im.bpp4 ? 4 : 8;
    im.nbx = (w + im.bw - 1) / im.bw; if (im.nbx < 2) im.nbx = 2;
    im.nby = (h + 3) / 4; if (im.nby < 2) im.nby = 2;
    if (!data || size < (uint32_t)im.nbx * (uint32_t)im.nby * 8u) return 0;
    int opaque = format == GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG || format == GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG;
    int bw = im.bw, bh = 4;
    for (int py = 0; py < h; py++) {
        int fy = py - bh / 2, by = floordiv(fy, bh), wy = fy - by * bh;
        for (int px = 0; px < w; px++) {
            int fx = px - bw / 2, bx = floordiv(fx, bw), wx = fx - bx * bw;
            uint32_t m, c[4];
            pvr_block(&im, bx, by, &m, &c[0]);         /* P (top left) */
            pvr_block(&im, bx + 1, by, &m, &c[1]);     /* Q (top right) */
            pvr_block(&im, bx, by + 1, &m, &c[2]);     /* R (bottom left) */
            pvr_block(&im, bx + 1, by + 1, &m, &c[3]); /* S (bottom right) */
            int f[4] = {(bw - wx) * (bh - wy), wx * (bh - wy), (bw - wx) * wy, wx * wy};
            int ca[4] = {0, 0, 0, 0}, cb[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                pvr_col a = pvr_colour_a(c[k]), b = pvr_colour_b(c[k]);
                ca[0] += a.r * f[k]; ca[1] += a.g * f[k]; ca[2] += a.b * f[k]; ca[3] += a.a * f[k];
                cb[0] += b.r * f[k]; cb[1] += b.g * f[k]; cb[2] += b.b * f[k]; cb[3] += b.a * f[k];
            }
            /* to 8 bits: the sums are 5-bit (alpha 4-bit) values times bw*bh (16 or 32) */
            for (int k = 0; k < 4; k++) {
                if (im.bpp4) {
                    ca[k] = k < 3 ? (ca[k] >> 6) + (ca[k] >> 1) : (ca[k] >> 4) + ca[k];
                    cb[k] = k < 3 ? (cb[k] >> 6) + (cb[k] >> 1) : (cb[k] >> 4) + cb[k];
                } else {
                    ca[k] = k < 3 ? (ca[k] >> 7) + (ca[k] >> 2) : (ca[k] >> 5) + (ca[k] >> 1);
                    cb[k] = k < 3 ? (cb[k] >> 7) + (cb[k] >> 2) : (cb[k] >> 5) + (cb[k] >> 1);
                }
            }
            int punch, wt = pvr_weight(&im, px, py, &punch);
            uint8_t *o = out + 4 * ((size_t)py * (size_t)w + (size_t)px);
            for (int k = 0; k < 4; k++) o[k] = (uint8_t)((ca[k] * (8 - wt) + cb[k] * wt) / 8);
            if (punch) o[3] = 0;
            if (opaque) o[3] = 255;
        }
    }
    return 1;
}

/* does the host GL list that extension? 1 yes, 0 no, -1 cannot tell yet (no current context) */
static int host_ext_state(const char *ext) {
    T_glGetString f = F(glGetString);
    const char *s = f ? f(GL_EXTENSIONS) : NULL;
    if (!s) return -1;
    size_t n = strlen(ext);
    for (const char *p = s; (p = strstr(p, ext)) != NULL; p += n)
        if ((p == s || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return 1;
    return 0;
}
static int host_has_pvrtc(void) {
    static int known = -1;
    if (known < 0) {
        known = host_ext_state("GL_IMG_texture_compression_pvrtc");
        if (known == 0) rt_log("gl: host GL has no PVRTC; decoding PVRTC textures in software");
    }
    return known == 1;
}

/* glTexImage2D/glTexSubImage2D with format GL_BGRA_EXT and an RGBA internal format (APPLE_texture_format_BGRA8888;
 * ANGLE wants BGRA for both, many Android drivers have no BGRA at all): swapped to RGBA in a host copy. Rows of
 * 4-byte pixels are always 4-aligned, so GL_UNPACK_ALIGNMENT does not matter. */
#define GL_BGRA_EXT 0x80E1
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
static uint8_t *bgra_swizzle(const uint8_t *src, I w, I h) {
    if (!src || w <= 0 || h <= 0) return NULL;
    size_t n = (size_t)w * (size_t)h;
    uint8_t *t = malloc(n * 4);
    if (!t) return NULL;
    for (size_t k = 0; k < n; k++) {
        t[4 * k] = src[4 * k + 2]; t[4 * k + 1] = src[4 * k + 1];
        t[4 * k + 2] = src[4 * k]; t[4 * k + 3] = src[4 * k + 3];
    }
    return t;
}

/* ---- textures ------------------------------------------------------------------------------------------------- */
IMP(glActiveTexture) { T_glActiveTexture f = F(glActiveTexture); if (f && unit_ok(A(0))) { act_unit = (int)(A(0) - 0x84C0) & 31; f(A(0)); } }
IMP(glBindTexture) { T_glBindTexture f = F(glBindTexture); if (f) f(A(0), A(1)); }
IMP(glGenTextures) { gen_names(F(glGenTextures), (I)A(0), A(1)); }
IMP(glDeleteTextures) { T_glDeleteTextures f = F(glDeleteTextures); if (f && (I)A(0) > 0) f((I)A(0), (const E *)G(A(1))); }
IMP(glTexImage2D) {
    T_glTexImage2D f = F(glTexImage2D);
    if (!f) return;
    E internal = A(2), format = A(6), type = A(7);
    I w = (I)A(3), h = (I)A(4);
    const void *pixels = G(A(8));
    uint8_t *tmp = NULL;
    if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
        internal = format = GL_RGBA;
        pixels = tmp = bgra_swizzle(pixels, w, h);
    }
    f(A(0), (I)A(1), (I)internal, w, h, (I)A(5), format, type, pixels);
    free(tmp);
}
IMP(glTexSubImage2D) {
    T_glTexSubImage2D f = F(glTexSubImage2D);
    if (!f) return;
    E format = A(6), type = A(7);
    I w = (I)A(4), h = (I)A(5);
    const void *pixels = G(A(8));
    uint8_t *tmp = NULL;
    if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) { /* the texture was made RGBA above */
        format = GL_RGBA;
        pixels = tmp = bgra_swizzle(pixels, w, h);
    }
    f(A(0), (I)A(1), (I)A(2), (I)A(3), w, h, format, type, pixels);
    free(tmp);
}
IMP(glCompressedTexImage2D) {
    E target = A(0), format = A(2);
    I level = (I)A(1), w = (I)A(3), h = (I)A(4), border = (I)A(5), size = (I)A(6);
    const void *data = G(A(7));
    if (is_pvrtc(format) && !host_has_pvrtc()) {
        T_glTexImage2D t = F(glTexImage2D);
        if (!t || w <= 0 || h <= 0) return;
        uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
        if (!rgba) return;
        if (pvr_decode(data, (uint32_t)size, w, h, format, rgba))
            t(target, level, GL_RGBA, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        else
            rt_log("gl: PVRTC level %d (%dx%d, format 0x%x) has only %d bytes (ignored)", level, w, h, format, size);
        free(rgba);
        return;
    }
    T_glCompressedTexImage2D f = F(glCompressedTexImage2D);
    if (f) f(target, level, format, w, h, border, size, data);
}
IMP(glCopyTexImage2D) {
    T_glCopyTexImage2D f = F(glCopyTexImage2D);
    if (f) f(A(0), (I)A(1), A(2), (I)A(3), (I)A(4), (I)A(5), (I)A(6), (I)A(7));
}
IMP(glTexParameterf) { T_glTexParameterf f = F(glTexParameterf); if (f) f(A(0), A(1), FA(2)); }
IMP(glTexParameteri) { T_glTexParameteri f = F(glTexParameteri); if (f) f(A(0), A(1), (I)A(2)); }
IMP(glTexEnvf) { T_glTexEnvf f = F(glTexEnvf); if (f) f(A(0), A(1), FA(2)); }
IMP(glTexEnvfv) { T_glTexEnvfv f = F(glTexEnvfv); if (f && A(2)) f(A(0), A(1), (const float *)G(A(2))); }
IMP(glTexEnvi) { T_glTexEnvi f = F(glTexEnvi); if (f) f(A(0), A(1), (I)A(2)); }
IMP(glTexEnvx) {
    E target = A(0), pname = A(1); I v = (I)A(2);
    T_glTexEnvx f = F(glTexEnvx);
    if (f) { f(target, pname, v); return; }
    /* scales are fixed-point numbers; every other parameter of glTexEnvx is an enum given as an integer */
    if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE) { T_glTexEnvf g = F(glTexEnvf); if (g) g(target, pname, fx(v)); }
    else { T_glTexEnvi g = F(glTexEnvi); if (g) g(target, pname, v); }
}
IMP(glPixelStorei) { T_glPixelStorei f = F(glPixelStorei); if (f) f(A(0), (I)A(1)); }
IMP(glReadPixels) {
    T_glReadPixels f = F(glReadPixels);
    if (f) f((I)A(0), (I)A(1), (I)A(2), (I)A(3), A(4), A(5), G(A(6)));
}

/* ---- state ---------------------------------------------------------------------------------------------------- */
IMP(glEnable) { T_glEnable f = F(glEnable); if (A(0) == 0x0DE1) tex2d_on[act_unit] = 1; else *cap_slot(A(0)) = 1; if (f) f(A(0)); }
IMP(glDisable) { T_glDisable f = F(glDisable); if (A(0) == 0x0DE1) tex2d_on[act_unit] = 0; else *cap_slot(A(0)) = 0; if (f) f(A(0)); }
IMP(glAlphaFunc) { T_glAlphaFunc f = F(glAlphaFunc); if (f) f(A(0), FA(1)); }
IMP(glBlendFunc) { T_glBlendFunc f = F(glBlendFunc); if (f) f(A(0), A(1)); }
IMP(glClear) { T_glClear f = F(glClear); wide_apply(0, 0, 0, 0, 0, NULL); if (f) f(A(0)); }
IMP(glClearColor) { T_glClearColor f = F(glClearColor); if (f) f(FA(0), FA(1), FA(2), FA(3)); }
IMP(glClearDepthf) { T_glClearDepthf f = F(glClearDepthf); if (f) f(FA(0)); }
IMP(glColorMask) { T_glColorMask f = F(glColorMask); if (f) f(B(0), B(1), B(2), B(3)); }
IMP(glCullFace) { T_glCullFace f = F(glCullFace); if (f) f(A(0)); }
IMP(glDepthFunc) { T_glDepthFunc f = F(glDepthFunc); if (f) f(A(0)); }
IMP(glDepthMask) { T_glDepthMask f = F(glDepthMask); if (f) f(B(0)); }
IMP(glDepthRangef) { T_glDepthRangef f = F(glDepthRangef); if (f) f(FA(0), FA(1)); }
IMP(glFlush) { T_glFlush f = F(glFlush); if (f) f(); }
IMP(glFrontFace) {
    T_glFrontFace f = F(glFrontFace);
    E face = A(0);
#ifdef NOVA_FRAME_LOG
    if (face == 0x0900 || face == 0x0901) guest_front_face = face;
#endif
    if (f) f(face);
}
IMP(glHint) { T_glHint f = F(glHint); if (f) f(A(0), A(1)); }
IMP(glLineWidth) { T_glLineWidth f = F(glLineWidth); if (f) f(FA(0)); }
IMP(glPointSize) { T_glPointSize f = F(glPointSize); if (f) f(FA(0)); }
IMP(glPointParameterf) { T_glPointParameterf f = F(glPointParameterf); if (f) f(A(0), FA(1)); }
IMP(glPolygonOffset) { T_glPolygonOffset f = F(glPolygonOffset); if (f) f(FA(0), FA(1)); }
IMP(glScissor) { T_glScissor f = F(glScissor); for (int k = 0; k < 4; k++) game_sc[k] = (I)A(k); wide_dirty = 1; if (f) f((I)A(0), (I)A(1), (I)A(2), (I)A(3)); }
IMP(glViewport) { T_glViewport f = F(glViewport); for (int k = 0; k < 4; k++) game_vp[k] = (I)A(k); wide_dirty = 1; if (f) f((I)A(0), (I)A(1), (I)A(2), (I)A(3)); }
IMP(glShadeModel) { T_glShadeModel f = F(glShadeModel); if (f) f(A(0)); }
IMP(glStencilFunc) { T_glStencilFunc f = F(glStencilFunc); if (f) f(A(0), (I)A(1), A(2)); }
IMP(glStencilMask) { T_glStencilMask f = F(glStencilMask); if (f) f(A(0)); }
IMP(glStencilOp) { T_glStencilOp f = F(glStencilOp); if (f) f(A(0), A(1), A(2)); }
IMP(glClipPlanef) { T_glClipPlanef f = F(glClipPlanef); if (f && A(1)) f(A(0), (const float *)G(A(1))); }

/* ---- fixed-function: colour, lighting, fog --------------------------------------------------------------------------- */
IMP(glColor4f) { T_glColor4f f = F(glColor4f); if (f) f(FA(0), FA(1), FA(2), FA(3)); }
IMP(glColor4ub) { T_glColor4ub f = F(glColor4ub); if (f) f(B(0), B(1), B(2), B(3)); }
IMP(glColor4x) {
    T_glColor4x f = F(glColor4x);
    if (f) { f((I)A(0), (I)A(1), (I)A(2), (I)A(3)); return; }
    T_glColor4f g = F(glColor4f); if (g) g(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)), fx((I)A(3)));
}
IMP(glNormal3f) { T_glNormal3f f = F(glNormal3f); if (f) f(FA(0), FA(1), FA(2)); }
IMP(glFogf) { T_glFogf f = F(glFogf); if (f) f(A(0), FA(1)); }
IMP(glFogfv) { T_glFogfv f = F(glFogfv); if (f && A(1)) f(A(0), (const float *)G(A(1))); }
IMP(glLightf) { T_glLightf f = F(glLightf); if (f) f(A(0), A(1), FA(2)); }
IMP(glLightfv) { T_glLightfv f = F(glLightfv); if (f && A(2)) f(A(0), A(1), (const float *)G(A(2))); }
IMP(glLightModelfv) { T_glLightModelfv f = F(glLightModelfv); if (f && A(1)) f(A(0), (const float *)G(A(1))); }
IMP(glMaterialf) { T_glMaterialf f = F(glMaterialf); if (f) f(A(0), A(1), FA(2)); }
IMP(glMaterialfv) { T_glMaterialfv f = F(glMaterialfv); if (f && A(2)) f(A(0), A(1), (const float *)G(A(2))); }

/* ---- matrices ----------------------------------------------------------------------------------------------- */
IMP(glMatrixMode) { T_glMatrixMode f = F(glMatrixMode); mat_mode = (int)A(0); if (f) f(A(0)); }
IMP(glLoadIdentity) { T_glLoadIdentity f = F(glLoadIdentity); if (mat_mode == 0x1701) proj_scaled = 0, proj_dirty = 1; { float *m = sh_cur(); if (m) sh_identity(m); } if (f) f(); }
IMP(glPushMatrix) { T_glPushMatrix f = F(glPushMatrix); if (mat_mode == 0x1701 && proj_sp < 4) proj_stack[proj_sp++] = proj_scaled; sh_push(); if (f) f(); }
IMP(glPopMatrix) { T_glPopMatrix f = F(glPopMatrix); if (mat_mode == 0x1701) { if (proj_sp > 0) proj_scaled = proj_stack[--proj_sp]; proj_dirty = 1; } sh_pop(); if (f) f(); }
IMP(glLoadMatrixf) { T_glLoadMatrixf f = F(glLoadMatrixf); if (mat_mode == 0x1701) proj_scaled = 0, proj_dirty = 1; if (A(0)) sh_load((const float *)G(A(0))); if (f && A(0)) f((const float *)G(A(0))); }
IMP(glMultMatrixf) { T_glMultMatrixf f = F(glMultMatrixf); PROJ_TOUCHED(); if (A(0)) sh_mult((const float *)G(A(0))); if (f && A(0)) f((const float *)G(A(0))); }
IMP(glFrustumf) { T_glFrustumf f = F(glFrustumf); PROJ_TOUCHED(); sh_frustum(FA(0), FA(1), FA(2), FA(3), FA(4), FA(5), 0); if (f) f(FA(0), FA(1), FA(2), FA(3), FA(4), FA(5)); }
IMP(glOrthof) { T_glOrthof f = F(glOrthof); PROJ_TOUCHED(); sh_frustum(FA(0), FA(1), FA(2), FA(3), FA(4), FA(5), 1); if (f) f(FA(0), FA(1), FA(2), FA(3), FA(4), FA(5)); }
IMP(glRotatef) { T_glRotatef f = F(glRotatef); PROJ_TOUCHED(); sh_rotate(FA(0), FA(1), FA(2), FA(3)); if (f) f(FA(0), FA(1), FA(2), FA(3)); }
IMP(glScalef) { T_glScalef f = F(glScalef); PROJ_TOUCHED(); sh_scale(FA(0), FA(1), FA(2)); if (f) f(FA(0), FA(1), FA(2)); }
IMP(glTranslatef) { T_glTranslatef f = F(glTranslatef); PROJ_TOUCHED(); sh_translate(FA(0), FA(1), FA(2)); if (f) f(FA(0), FA(1), FA(2)); }
IMP(glOrthox) {
    PROJ_TOUCHED();
    sh_frustum(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)), fx((I)A(3)), fx((I)A(4)), fx((I)A(5)), 1);
    T_glOrthox f = F(glOrthox);
    if (f) { f((I)A(0), (I)A(1), (I)A(2), (I)A(3), (I)A(4), (I)A(5)); return; }
    T_glOrthof g = F(glOrthof);
    if (g) g(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)), fx((I)A(3)), fx((I)A(4)), fx((I)A(5)));
}
IMP(glRotatex) {
    PROJ_TOUCHED();
    sh_rotate(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)), fx((I)A(3)));
    T_glRotatex f = F(glRotatex);
    if (f) { f((I)A(0), (I)A(1), (I)A(2), (I)A(3)); return; }
    T_glRotatef g = F(glRotatef); if (g) g(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)), fx((I)A(3)));
}
IMP(glScalex) {
    PROJ_TOUCHED();
    sh_scale(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)));
    T_glScalex f = F(glScalex);
    if (f) { f((I)A(0), (I)A(1), (I)A(2)); return; }
    T_glScalef g = F(glScalef); if (g) g(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)));
}
IMP(glTranslatex) {
    PROJ_TOUCHED();
    sh_translate(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)));
    T_glTranslatex f = F(glTranslatex);
    if (f) { f((I)A(0), (I)A(1), (I)A(2)); return; }
    T_glTranslatef g = F(glTranslatef); if (g) g(fx((I)A(0)), fx((I)A(1)), fx((I)A(2)));
}

/* ---- registration --------------------------------------------------------------------------------------------- */
void *(*rt_gl_getproc)(const char *name) = NULL;

void rt_gl_register(void) {
    REG(glActiveTexture); REG(glAlphaFunc); REG(glBindBuffer); REG(glBindFramebufferOES);
    REG(glBindRenderbufferOES); REG(glBindTexture); REG(glBlendFunc); REG(glBufferData); REG(glBufferSubData);
    REG(glCheckFramebufferStatusOES); REG(glClear); REG(glClearColor); REG(glClearDepthf);
    REG(glClientActiveTexture); REG(glClipPlanef); REG(glColor4f); REG(glColor4ub); REG(glColor4x);
    REG(glColorMask); REG(glColorPointer); REG(glCompressedTexImage2D); REG(glCopyTexImage2D); REG(glCullFace);
    REG(glDeleteBuffers); REG(glDeleteFramebuffersOES); REG(glDeleteRenderbuffersOES); REG(glDeleteTextures);
    REG(glDepthFunc); REG(glDepthMask); REG(glDepthRangef); REG(glDisable); REG(glDisableClientState);
    REG(glDrawArrays); REG(glDrawElements); REG(glEnable); REG(glEnableClientState); REG(glFlush); REG(glFogf);
    REG(glFogfv); REG(glFramebufferRenderbufferOES); REG(glFramebufferTexture2DOES); REG(glFrontFace);
    REG(glFrustumf); REG(glGenBuffers); REG(glGenFramebuffersOES); REG(glGenRenderbuffersOES); REG(glGenTextures);
    REG(glGetBooleanv); REG(glGetError); REG(glGetFloatv); REG(glGetFramebufferAttachmentParameterivOES);
    REG(glGetIntegerv); REG(glGetPointerv); REG(glGetRenderbufferParameterivOES); REG(glGetString); REG(glHint);
    REG(glIsEnabled); REG(glLightModelfv); REG(glLightf); REG(glLightfv); REG(glLineWidth); REG(glLoadIdentity);
    REG(glLoadMatrixf); REG(glMaterialf); REG(glMaterialfv); REG(glMatrixMode); REG(glMultMatrixf);
    REG(glNormal3f); REG(glNormalPointer); REG(glOrthof); REG(glOrthox); REG(glPixelStorei);
    REG(glPointParameterf); REG(glPointSize); REG(glPolygonOffset); REG(glPopMatrix); REG(glPushMatrix);
    REG(glReadPixels); REG(glRenderbufferStorageOES); REG(glRotatef); REG(glRotatex); REG(glScalef);
    REG(glScalex); REG(glScissor); REG(glShadeModel); REG(glStencilFunc); REG(glStencilMask); REG(glStencilOp);
    REG(glTexCoordPointer); REG(glTexEnvf); REG(glTexEnvfv); REG(glTexEnvi); REG(glTexEnvx); REG(glTexImage2D);
    REG(glTexParameterf); REG(glTexParameteri); REG(glTexSubImage2D); REG(glTranslatef); REG(glTranslatex);
    REG(glVertexPointer); REG(glViewport);
}
