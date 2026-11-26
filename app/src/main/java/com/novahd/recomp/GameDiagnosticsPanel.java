package com.novahd.recomp;

import android.graphics.Color;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.PopupWindow;
import android.widget.TextView;

import com.novahd.compat.DeviceMemory;

final class GameDiagnosticsPanel {
    static final String PREFS = "game_diagnostics";
    static final String ENABLED = "enabled";
    static final String CULLING = "culling";
    static final String MUSIC_CATALOG = "music_catalog";
    static final String TRACE = "last_trace";
    private final GameActivity activity;
    private final boolean nativeTrace;
    private final java.util.ArrayDeque<String> history = new java.util.ArrayDeque<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private PopupWindow popup;
    private TextView text;

    GameDiagnosticsPanel(GameActivity activity, boolean nativeTrace) {
        this.activity = activity;
        this.nativeTrace = nativeTrace;
    }

    void start() {
        if (popup != null) return;
        View anchor = activity.getWindow().getDecorView();
        if (!anchor.isAttachedToWindow()) return;
        text = new TextView(activity);
        text.setTextColor(Color.WHITE);
        text.setTextSize(11);
        int padding = Math.round(6 * activity.getResources().getDisplayMetrics().density);
        text.setPadding(padding, padding, padding, padding);
        text.setBackgroundColor(0xC0000000);
        popup = new PopupWindow(text, WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT, false);
        popup.setTouchable(false);
        popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        try {
            popup.showAtLocation(anchor, Gravity.TOP | Gravity.END, padding, padding);
            sample.run();
        } catch (WindowManager.BadTokenException ignored) {
            stop();
        }
    }

    void stop() {
        handler.removeCallbacks(sample);
        if (popup != null) popup.dismiss();
        popup = null;
        text = null;
    }

    @SuppressWarnings("deprecation")
    private final Runnable sample = new Runnable() {
        @Override public void run() {
            if (text == null) return;
            Display.Mode mode = activity.getWindowManager().getDefaultDisplay().getMode();
            long pss = Debug.getPss();
            text.setText(activity.getString(R.string.graphics_overlay_summary,
                    mode.getPhysicalWidth(), mode.getPhysicalHeight(), pss, DeviceMemory.pageBytes()));
            if (nativeTrace) {
                String graphics = NativeWindowProbe.readGraphics().trim();
                if (!graphics.isEmpty()) {
                    String device = text.getText().toString();
                    text.append("\n" + graphics.split("\n", 2)[0]);
                    if (history.size() == 30) history.removeFirst();
                    history.addLast(android.os.SystemClock.elapsedRealtime() + " ms | " + device + "\n" + graphics);
                    activity.getSharedPreferences(PREFS, 0).edit().putString(TRACE, String.join("\n\n", history)).apply();
                }
            } else if (BuildConfig.WINDOW_PROBE) text.append("\n" + activity.sampleNativeWindow());
            handler.postDelayed(this, 1000);
        }
    };
}
