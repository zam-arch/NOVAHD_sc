#include <assert.h>
#include "../runtime/gles1.c"

static E format;
static unsigned stencil;
static int fail_d16, checks;

void rt_log(const char *fmt, ...) { (void)fmt; }

static void storage(E target, E next, I w, I h) {
    assert(target == GL_RENDERBUFFER_OES && w == 16 && h == 16);
    format = next;
}

static void attach(E target, E attachment, E type, unsigned buffer) {
    assert(target == 0x8d40 && type == GL_RENDERBUFFER_OES);
    if (attachment == 0x8d20) stencil = buffer;
}

static E check(E target) {
    assert(target == 0x8d40);
    checks++;
    if (format == 0x81a5) {
        assert(!stencil);
        if (!fail_d16) return GL_FRAMEBUFFER_COMPLETE_OES;
    } else assert(format == 0x88f0 && stencil == 9);
    return 0x8cd6;
}

int main(void) {
    present_ds = 9;
    assert(present_depth(storage, attach, check, 16, 16) == GL_FRAMEBUFFER_COMPLETE_OES);
    assert(checks == 2);
    fail_d16 = 1;
    assert(present_depth(storage, attach, check, 16, 16) == 0x8cd6);
    assert(checks == 4);
    puts("D24S8 failure detaches stencil, verifies D16, and propagates incomplete fallback");
}
