package com.novahd.recomp;

import android.view.Surface;

final class NativeWindowProbe {
    private static final boolean available;

    static {
        boolean loaded = false;
        try {
            System.loadLibrary("nova_probe");
            loaded = true;
        } catch (UnsatisfiedLinkError ignored) {
            // The diagnostic is optional even in the experimental build.
        }
        available = loaded;
    }

    static String describe(Surface surface) {
        if (!available) return "Sonda nativa no disponible";
        try {
            return sample(surface != null && surface.isValid() ? surface : null);
        } catch (IllegalArgumentException | IllegalStateException | UnsatisfiedLinkError unavailable) {
            return "Sonda nativa no disponible";
        }
    }

    static boolean configure(boolean trace, int culling, boolean reference) {
        try {
            System.loadLibrary("novahd");
            configureGraphics(trace, culling, reference);
            return true;
        } catch (UnsatisfiedLinkError unavailable) {
            return false;
        }
    }

    static native String readGraphics();
    private static native void configureGraphics(boolean trace, int culling, boolean reference);
    private static native String sample(Surface surface);
}
