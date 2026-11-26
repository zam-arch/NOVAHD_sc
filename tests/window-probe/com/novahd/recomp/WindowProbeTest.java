package com.novahd.recomp;

import android.view.Surface;

public final class WindowProbeTest {
    public static void main(String[] args) {
        String unloaded = NativeWindowProbe.describe(null);
        if (!unloaded.contains("sin superficie") || !unloaded.contains("fuera del alcance"))
            throw new AssertionError(unloaded);
        System.loadLibrary("novahd");
        String loaded = NativeWindowProbe.describe(new Surface());
        if (!loaded.contains("1600 x 768") || !loaded.contains("swap si / registro musical no"))
            throw new AssertionError(loaded);
        System.out.println("JNI window probe exercised with simulated window and partial engine exports");
    }
}
