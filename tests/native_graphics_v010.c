#include <assert.h>
#include <math.h>
#define NOVA_FRAME_LOG
#include "../runtime/gles1.c"

static E face = 0x0901;
static int culling = 1;
static float color[4] = {0.2f, 0.3f, 0.4f, 0.5f};
static float matrices[3][16];
static E matrix_mode = 0x1701;
static int did_swap;
static I last_scissor[4];
void rt_platform_graphics_event(const char *details) { (void)details; }

void rt_log(const char *fmt, ...) { (void)fmt; }
void rt_platform_size(int *w, int *h) { *w = 2048; *h = 768; }
void rt_platform_swap(void) {
    assert(color[0] == 0 && color[3] == 1);
    assert(matrices[0][0] == 1 && matrices[1][0] == 1 && matrices[2][0] == 1);
    did_swap = 1;
}
static void enable(E cap) { if (cap == 0x0B44) culling = 1; }
static void disable(E cap) { if (cap == 0x0B44) culling = 0; }
static void front(E direction) { face = direction; }
static void integer(E prop, I *out) {
    if (prop == 0x0BA2 || prop == 0x0C10) {
        out[0] = 0; out[1] = 0; out[2] = 768; out[3] = 1024;
    } else *out = prop == 0x0BA0 ? (I)matrix_mode : 0;
}
static void floats(E prop, float *out) {
    if (prop == 0x0C22) memcpy(out, color, sizeof color);
    else if (prop == 0x0B00) memset(out, 0, sizeof color);
    else memcpy(out, matrices[prop - 0x0BA6], sizeof matrices[0]);
}
static void boolean(E prop, uint8_t *out) { (void)prop; memset(out, 1, 4); }
static void pointer(E prop, void **out) { (void)prop; *out = NULL; }
static void texenv(E a, E b, I *out) { (void)a; (void)b; *out = 0; }
static uint8_t enabled(E cap) { (void)cap; return 0; }
static void one(E a) { (void)a; }
static void two(E a, E b) { (void)a; (void)b; }
static void rectangle(I a, I b, I c, I d) { (void)a; (void)b; (void)c; (void)d; }
static void scissor_rectangle(I a, I b, I c, I d) { last_scissor[0] = a; last_scissor[1] = b; last_scissor[2] = c; last_scissor[3] = d; }
static void rgba(float a, float b, float c, float d) { (void)a; (void)b; (void)c; (void)d; }
static void mask(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)a; (void)b; (void)c; (void)d; }
static void env(E a, E b, I c) { (void)a; (void)b; (void)c; }
static void array(I a, E b, I c, const void *d) { (void)a; (void)b; (void)c; (void)d; }
static void normal(E a, I b, const void *c) { (void)a; (void)b; (void)c; }
static void draw(E a, I b, I c) { (void)a; (void)b; (void)c; }
static void mode(E value) { matrix_mode = value; }
static void load(const float *input) { memcpy(matrices[matrix_mode - 0x1700], input, sizeof matrices[0]); }
static void identity(void) { float values[16] = {1}; load(values); }
static void clear_color(float r, float g, float b, float a) { color[0] = r; color[1] = g; color[2] = b; color[3] = a; }

#define HOST(name, impl) do { P_##name.fn = (T_##name)impl; P_##name.tried = 1; } while (0)

int main(void) {
    struct presentation_rect ipad = fit_layer(1668, 768, 1024, 768);
    assert(ipad.x == 322 && ipad.y == 0 && ipad.w == 1024 && ipad.h == 768);
    struct presentation_rect wide = fit_layer(1668, 768, 1668, 768);
    assert(wide.x == 0 && wide.w == 1668);
    struct presentation_rect narrow = fit_layer(1024, 1024, 1024, 768);
    assert(narrow.y == 128 && narrow.h == 768);
    HOST(glEnable, enable); HOST(glDisable, disable); HOST(glFrontFace, front);
    HOST(glGetIntegerv, integer); HOST(glGetFloatv, floats); HOST(glGetBooleanv, boolean);
    HOST(glGetPointerv, pointer); HOST(glGetTexEnviv, texenv); HOST(glIsEnabled, enabled);
    HOST(glEnableClientState, one); HOST(glDisableClientState, one); HOST(glBindFramebufferOES, two);
    HOST(glBindTexture, two); HOST(glBindBuffer, two); HOST(glActiveTexture, one); HOST(glClientActiveTexture, one);
    HOST(glMatrixMode, mode); HOST(glLoadIdentity, identity); HOST(glLoadMatrixf, load);
    HOST(glViewport, rectangle); HOST(glScissor, scissor_rectangle); HOST(glColor4f, rgba); HOST(glColorMask, mask);
    HOST(glTexEnvi, env); HOST(glVertexPointer, array); HOST(glTexCoordPointer, array); HOST(glColorPointer, array);
    HOST(glNormalPointer, normal); HOST(glDrawArrays, draw); HOST(glClearColor, clear_color); HOST(glClear, one);
    *cap_slot(0x0B44) = 1;
    rt_gl_configure_diagnostics(0, 0); assert(cull_begin() == 0);
    rt_gl_configure_diagnostics(0, 1); assert(cull_begin() == 1 && !culling);
    cull_end(1); assert(culling);
    rt_gl_configure_diagnostics(0, 2); assert(cull_begin() == 0 && face == 0x0901);
    *cap_slot(0x0B44) = 0; assert(cull_begin() == 0);
    present_w = 768; present_h = 1024;
    for (int k = 0; k < 3; ++k) matrices[k][0] = 2.0f + k;
    present_blit();
    assert(did_swap && matrix_mode == 0x1701 && color[0] == 0.2f && color[3] == 0.5f);
    for (int k = 0; k < 3; ++k) assert(matrices[k][0] == 2.0f + k);
    drawable_w = 768;
    drawable_h = 1024;
    frame_3d = hud_on = 1;
    const int widths[] = {1024, 1366, 1536, 1664, 1706, 1792};
    for (unsigned k = 0; k < sizeof widths / sizeof widths[0]; ++k) {
        present_h = widths[k];
        rt_gl_wide_long = k ? present_h : 0;
        hit_cur_n = 0;
        struct place placement;
        wide_place(0.7f, 0.82f, 0.6f, 0.78f, &placement);
        assert(placement.sx == placement.sy);
        assert(k ? placement.sx > 1 : placement.sx == 1);
        struct place with_hud = placement;
        hud_on = 0;
        hit_cur_n = 0;
        wide_place(0.7f, 0.82f, 0.6f, 0.78f, &placement);
        assert(!memcmp(&with_hud, &placement, sizeof placement));
        hud_on = 1;
        if (k) {
            assert(hit_cur_n == 1);
            hit_last[0] = hit_cur[0]; hit_last_n = 1;
            float x = 768 - (645 * placement.sx + placement.ox);
            float y = present_h - (901 * placement.sy + placement.oy);
            float transform[4];
            rt_gl_wide_touch(y, x, transform);
            assert(fabsf(y * transform[0] + transform[1] - 123) < 0.01f);
            assert(fabsf(x * transform[2] + transform[3] - 123) < 0.01f);
        } else assert(placement.sx == 1);
    }
    present_h = rt_gl_wide_long = 1668;
    game_sc[0] = 656; game_sc[1] = 643; game_sc[2] = 105; game_sc[3] = 200;
    assert(map_scissor());
    struct place map, rim, caption, control;
    anchor_corner(&map, 1024, 768, 1, 1, 135);
    wide_place(643.0f * 2 / 1024 - 1, 843.0f * 2 / 1024 - 1,
               656.0f * 2 / 768 - 1, 761.0f * 2 / 768 - 1, &rim);
    assert(!memcmp(&map, &rim, sizeof map));
    wide_place(-1, 1, -0.10f, -0.05f, &caption);
    assert(caption.sx == 1 && caption.sy == 1);
    wide_place(0.62f, 0.94f, -0.95f, -0.61f, &control);
    assert(fabsf(control.sx - 1.40f) < 0.001f);
    rt_gl_configure_scale(75, 150);
    wide_place(0.62f, 0.94f, -0.95f, -0.61f, &control);
    assert(fabsf(control.sx - 1.50f) < 0.001f);
    wide_place(643.0f * 2 / 1024 - 1, 843.0f * 2 / 1024 - 1,
               656.0f * 2 / 768 - 1, 761.0f * 2 / 768 - 1, &rim);
    assert(rim.sx == 0.75f);
    present_fbo = 9; drawable_fb = bound_fb = 8;
    *cap_slot(0x0C11) = 1;
    sh_pj[sh_pj_sp][11] = -1; proj_dirty = wide_dirty = 1;
    wide_apply(0, 0, 0, 0, 0, NULL);
    I clear_scissor[4]; memcpy(clear_scissor, last_scissor, sizeof clear_scissor);
    wide_apply(2, 4, 0, 3, 0x1403, NULL);
    assert(!memcmp(clear_scissor, last_scissor, sizeof clear_scissor));
    assert(last_scissor[3] == 150 && !proj_scaled);
    *cap_slot(0x0C11) = 0;
    wide_apply(2, 4, 0, 3, 0x1403, NULL);
    assert(proj_scaled);
    puts("Map frame shares the captured scissor transform. Caption scale is independent.");
    puts("HUD fixture preserves scale proportions and inverse touch from 4:3 to 21:9.");
    puts("Blit restores captured matrices and clear color. Cull overrides are scoped and reversible.");
}
