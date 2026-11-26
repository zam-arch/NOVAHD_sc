package com.novahd.recomp;

import android.app.NativeActivity;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;

public class GameActivity extends NativeActivity {
    static final String REFERENCE_FRAME = "reference_4_3";
    private GameDiagnosticsPanel diagnostics;
    private boolean resumed;
    private boolean nativeTrace;
    private android.widget.PopupWindow settingsButton;
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
        
        android.content.SharedPreferences settings = getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE);
        boolean overlay = settings.getBoolean(GameDiagnosticsPanel.ENABLED, false);
        nativeTrace = BuildConfig.REBUILT_ENGINE
                && NativeWindowProbe.configure(overlay, settings.getInt(GameDiagnosticsPanel.CULLING, 0),
                    settings.getBoolean(REFERENCE_FRAME, true));
        if (BuildConfig.REBUILT_ENGINE) LauncherActivity.recordBootAttempt(this);
        super.onCreate(savedInstanceState);
        if (overlay) diagnostics = new GameDiagnosticsPanel(this, nativeTrace);
    }

    public void onNativeBootConfirmed() {
        runOnUiThread(() -> LauncherActivity.confirmBoot(this));
    }

    MusicPlayer music() { return ((RecoveryApplication) getApplication()).music(); }

    void updateDiagnostics(boolean enabled, int cull) {
        if (nativeTrace) NativeWindowProbe.updateGraphics(enabled, cull);
        if (diagnostics != null) diagnostics.stop();
        diagnostics = enabled ? new GameDiagnosticsPanel(this, nativeTrace) : null;
        if (diagnostics != null && resumed && hasWindowFocus()) diagnostics.start();
    }

    private void showSettingsButton() {
        if (settingsButton != null || !BuildConfig.REBUILT_ENGINE) return;
        android.widget.Button button = new android.widget.Button(this);
        button.setText("⚙");
        button.setContentDescription("Configuración avanzada");
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(0, 0, 0, 0);
        button.setAlpha(0.85f);
        button.setOnClickListener(view -> new GameSettings(this).show());
        int size = Math.round(48 * getResources().getDisplayMetrics().density);
        settingsButton = new android.widget.PopupWindow(button, size, size, false);
        settingsButton.setInputMethodMode(android.widget.PopupWindow.INPUT_METHOD_NOT_NEEDED);
        View anchor = getWindow().getDecorView();
        try {
            settingsButton.showAtLocation(anchor, android.view.Gravity.TOP | android.view.Gravity.CENTER_HORIZONTAL, 0, 0);
        } catch (android.view.WindowManager.BadTokenException ignored) {
            hideSettingsButton();
        }
    }

    private void hideSettingsButton() {
        if (settingsButton != null) settingsButton.dismiss();
        settingsButton = null;
    }

    // Called on the engine thread. Only the synchronized catalog is accessed here.
    public byte[] readGameMusicCatalog(int grouping) {
        if (!getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, false)) return null;
        return MusicLibrary.get(this).catalog(grouping);
    }

    public void commandGameMusic(int command, String[] ids) {
        if (!getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, false)) return;
        runOnUiThread(() -> ((RecoveryApplication) getApplication()).music().commandFromGame(command, ids));
    }

    public String readGameMusicState() {
        return getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, false)
                ? ((RecoveryApplication) getApplication()).music().guestSnapshot() : "0:";
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        if (hasWindowFocus()) {
            showSettingsButton();
            if (diagnostics != null) diagnostics.start();
        }
    }

    @Override protected void onPause() {
        resumed = false;
        hideSettingsButton();
        if (isFinishing()) music().stop();
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
        if (!hasFocus) hideSettingsButton();
        if (hasFocus) {
            if (resumed) showSettingsButton();
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
