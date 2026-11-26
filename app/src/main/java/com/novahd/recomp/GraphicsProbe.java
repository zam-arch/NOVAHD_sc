package com.novahd.recomp;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES10;
import android.opengl.GLES11Ext;

import java.nio.ByteBuffer;

final class GraphicsProbe {
    private GraphicsProbe() {}

    static String run() {
        EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (display == EGL14.EGL_NO_DISPLAY || !EGL14.eglInitialize(display, null, 0, null, 0))
            return eglError("Inicialización");
        EGLContext context = EGL14.EGL_NO_CONTEXT;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        try {
            int[] attributes = {
                    EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES_BIT,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8,
                    EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8, EGL14.EGL_NONE
            };
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];
            if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0))
                return eglError("Selección EGL");
            if (count[0] == 0) return "EGL no ofrece una configuración GLES 1.x RGBA8888 para pbuffer.";
            if (!EGL14.eglBindAPI(EGL14.EGL_OPENGL_ES_API)) return eglError("Selección GLES");
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                    new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 1, EGL14.EGL_NONE}, 0);
            if (context == EGL14.EGL_NO_CONTEXT) return eglError("Creación del contexto GLES 1.x");
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[] {EGL14.EGL_WIDTH, 16, EGL14.EGL_HEIGHT, 16, EGL14.EGL_NONE}, 0);
            if (surface == EGL14.EGL_NO_SURFACE) return eglError("Creación de la superficie");
            if (!EGL14.eglMakeCurrent(display, surface, surface, context)) return eglError("Activación del contexto");

            String extensions = " " + GLES10.glGetString(GLES10.GL_EXTENSIONS) + " ";
            int[] max = new int[1];
            GLES10.glGetIntegerv(GLES10.GL_MAX_TEXTURE_SIZE, max, 0);
            StringBuilder report = new StringBuilder()
                    .append("GPU: ").append(GLES10.glGetString(GLES10.GL_RENDERER))
                    .append("\nProveedor: ").append(GLES10.glGetString(GLES10.GL_VENDOR))
                    .append("\nGLES/controlador: ").append(GLES10.glGetString(GLES10.GL_VERSION))
                    .append("\nEGL: ").append(EGL14.eglQueryString(display, EGL14.EGL_VERSION))
                    .append("\nTextura máxima: ").append(max[0]).append(" px")
                    .append("\nPVRTC nativo: ").append(extensions.contains(" GL_IMG_texture_compression_pvrtc ") ? "Sí" : "No")
                    .append("\nBGRA: ").append(extensions.contains(" GL_EXT_texture_format_BGRA8888 ")
                            || extensions.contains(" GL_APPLE_texture_format_BGRA8888 ") ? "Sí" : "No");
            if (extensions.contains(" GL_OES_framebuffer_object ")) {
                checkFramebuffer(report, false);
                if (extensions.contains(" GL_OES_packed_depth_stencil ")) checkFramebuffer(report, true);
                else report.append("\nProfundidad 24 / stencil 8: extensión no anunciada");
            } else {
                report.append("\nFramebuffer OES: extensión no anunciada");
            }
            return report.toString();
        } finally {
            if (context != EGL14.EGL_NO_CONTEXT || surface != EGL14.EGL_NO_SURFACE)
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
            EGL14.eglTerminate(display);
            EGL14.eglReleaseThread();
        }
    }

    private static void checkFramebuffer(StringBuilder report, boolean packed) {
        int framebuffer = GLES11Ext.GL_FRAMEBUFFER_OES;
        int renderbuffer = GLES11Ext.GL_RENDERBUFFER_OES;
        int[] texture = new int[1], fbo = new int[1], depth = new int[1];
        int size = 16;
        GLES10.glGenTextures(1, texture, 0);
        GLES10.glBindTexture(GLES10.GL_TEXTURE_2D, texture[0]);
        GLES10.glTexParameterf(GLES10.GL_TEXTURE_2D, GLES10.GL_TEXTURE_MIN_FILTER, GLES10.GL_NEAREST);
        GLES10.glTexImage2D(GLES10.GL_TEXTURE_2D, 0, GLES10.GL_RGBA, size, size,
                0, GLES10.GL_RGBA, GLES10.GL_UNSIGNED_BYTE, null);
        GLES11Ext.glGenFramebuffersOES(1, fbo, 0);
        GLES11Ext.glBindFramebufferOES(framebuffer, fbo[0]);
        GLES11Ext.glFramebufferTexture2DOES(framebuffer, GLES11Ext.GL_COLOR_ATTACHMENT0_OES,
                GLES10.GL_TEXTURE_2D, texture[0], 0);
        GLES11Ext.glGenRenderbuffersOES(1, depth, 0);
        GLES11Ext.glBindRenderbufferOES(renderbuffer, depth[0]);
        GLES11Ext.glRenderbufferStorageOES(renderbuffer, packed ? 0x88F0 : GLES11Ext.GL_DEPTH_COMPONENT16_OES, size, size);
        GLES11Ext.glFramebufferRenderbufferOES(framebuffer, GLES11Ext.GL_DEPTH_ATTACHMENT_OES, renderbuffer, depth[0]);
        if (packed)
            GLES11Ext.glFramebufferRenderbufferOES(framebuffer, GLES11Ext.GL_STENCIL_ATTACHMENT_OES, renderbuffer, depth[0]);
        int status = GLES11Ext.glCheckFramebufferStatusOES(framebuffer);
        report.append(packed ? "\nProfundidad 24 / stencil 8: " : "\nProfundidad 16: ");
        if (status == GLES11Ext.GL_FRAMEBUFFER_COMPLETE_OES) {
            ByteBuffer pixel = ByteBuffer.allocateDirect(4);
            GLES10.glClearColor(1, 0, 1, 1);
            GLES10.glClear(GLES10.GL_COLOR_BUFFER_BIT | GLES10.GL_DEPTH_BUFFER_BIT);
            GLES10.glReadPixels(0, 0, 1, 1, GLES10.GL_RGBA, GLES10.GL_UNSIGNED_BYTE, pixel);
            boolean expected = (pixel.get(0) & 255) == 255 && pixel.get(1) == 0
                    && (pixel.get(2) & 255) == 255 && (pixel.get(3) & 255) == 255;
            report.append(expected ? "completo, píxel correcto" : "completo, píxel inesperado");
        } else {
            report.append("incompleto ").append(hex(status));
        }
        report.append("\nError GLES de la prueba: ").append(hex(GLES10.glGetError()));
        GLES11Ext.glBindFramebufferOES(framebuffer, 0);
        GLES11Ext.glDeleteFramebuffersOES(1, fbo, 0);
        GLES11Ext.glDeleteRenderbuffersOES(1, depth, 0);
        GLES10.glDeleteTextures(1, texture, 0);
    }

    private static String eglError(String step) { return step + ": error EGL " + hex(EGL14.eglGetError()); }

    private static String hex(int code) { return "0x" + Integer.toHexString(code); }
}
