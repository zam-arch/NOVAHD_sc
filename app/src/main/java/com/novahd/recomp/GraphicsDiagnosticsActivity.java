package com.novahd.recomp;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.novahd.compat.DeviceMemory;

public final class GraphicsDiagnosticsActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(20 * getResources().getDisplayMetrics().density);
        panel.setPadding(padding, padding, padding, padding);
        TextView details = new TextView(this);
        details.setTextIsSelectable(true);
        details.setText(R.string.graphics_probe_running);
        Button copy = new Button(this);
        copy.setText(R.string.graphics_copy);
        copy.setEnabled(false);
        panel.addView(details);
        panel.addView(copy);
        scroll.addView(panel);
        setContentView(scroll);
        copy.setOnClickListener(view -> getSystemService(ClipboardManager.class)
                .setPrimaryClip(ClipData.newPlainText(getString(R.string.device_diagnostics), details.getText())));
        new Thread(() -> {
            String graphics;
            try {
                graphics = GraphicsProbe.run();
            } catch (RuntimeException error) {
                graphics = "Prueba GLES no disponible (" + error.getClass().getSimpleName() + ").";
            }
            String result = DeviceMemory.report() + "\n\nPrueba GLES independiente\n" + graphics
                    + "\n\n" + recentNativeExit();
            runOnUiThread(() -> {
                if (isDestroyed() || isFinishing()) return;
                details.setText(result);
                copy.setEnabled(true);
            });
        }, "nova-gles-probe").start();
    }

    private String recentNativeExit() {
        if (Build.VERSION.SDK_INT >= 30) {
            ActivityManager manager = getSystemService(ActivityManager.class);
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(getPackageName(), 0, 16)) {
                if (getPackageName().equals(exit.getProcessName())
                        && exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE)
                    return "Último cierre nativo registrado por Android: estado " + exit.getStatus()
                            + ", " + android.text.format.DateFormat.format("yyyy-MM-dd HH:mm:ss", exit.getTimestamp());
            }
            return "Android no registra un cierre nativo en el historial reciente consultado.";
        }
        return "Historial de cierres nativos no disponible en esta versión de Android.";
    }
}
