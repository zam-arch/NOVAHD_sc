package com.novahd.recomp;

import android.app.NativeActivity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;

public class GameActivity extends NativeActivity {
    private GameDiagnosticsPanel diagnostics;
    private boolean resumed;
    private android.view.SurfaceHolder gameSurface;

    @Override public void surfaceCreated(android.view.SurfaceHolder holder) {
        super.surfaceCreated(holder);
        gameSurface = holder;
    }

    @Override public void surfaceDestroyed(android.view.SurfaceHolder holder) {
        gameSurface = null;
        super.surfaceDestroyed(holder);
    }

    String sampleNativeWindow() {
        return NativeWindowProbe.describe(gameSurface == null ? null : gameSurface.getSurface());
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Hide system bars for immersive gameplay
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        
        super.onCreate(savedInstanceState);
        if (getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.ENABLED, false)) diagnostics = new GameDiagnosticsPanel(this);
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        if (diagnostics != null && hasWindowFocus()) diagnostics.start();
    }

    @Override protected void onPause() {
        resumed = false;
        if (diagnostics != null) diagnostics.stop();
        super.onPause();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // Handle orientation changes without restarting
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (diagnostics != null) {
            if (hasFocus && resumed) diagnostics.start();
            else diagnostics.stop();
        }
        if (hasFocus) {
            View decorView = getWindow().getDecorView();
            decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
