package com.novahd.diagnostic;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.novahd.compat.DeviceMemory;

public final class DiagnosticsActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        String report = DeviceMemory.report();
        ScrollView scroll = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(24 * getResources().getDisplayMetrics().density);
        content.setPadding(padding, padding, padding, padding);
        scroll.addView(content);

        TextView reading = new TextView(this);
        reading.setTextSize(20);
        reading.setText(report);
        reading.setTextIsSelectable(true);
        content.addView(reading);

        Button copy = new Button(this);
        copy.setText("Copiar diagnóstico");
        copy.setOnClickListener(view -> getSystemService(ClipboardManager.class)
                .setPrimaryClip(ClipData.newPlainText("Diagnóstico de memoria", report)));
        content.addView(copy);
        setContentView(scroll);
    }
}
