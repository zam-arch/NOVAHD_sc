package com.novahd.recomp;

import android.content.SharedPreferences;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Spinner;

final class HudScaleSettings {
    static final String HUD = "hud_scale_percent", CONTROLS = "control_scale_percent";
    static final int DEFAULT_HUD = 135, DEFAULT_CONTROLS = 140;
    private static final int MIN = 75, MAX = 150, STEP = 5;
    private final Spinner hud, controls;

    HudScaleSettings(View content, SharedPreferences settings) {
        hud = content.findViewById(R.id.hudScale);
        controls = content.findViewById(R.id.controlScale);
        bind(hud, settings.getInt(HUD, DEFAULT_HUD), R.string.hud_scale);
        bind(controls, settings.getInt(CONTROLS, DEFAULT_CONTROLS), R.string.control_scale);
        content.findViewById(R.id.hudScaleGroup).setVisibility(BuildConfig.REBUILT_ENGINE ? View.VISIBLE : View.GONE);
        content.findViewById(R.id.resetHudScale).setOnClickListener(view -> {
            hud.setSelection(index(DEFAULT_HUD));
            controls.setSelection(index(DEFAULT_CONTROLS));
        });
    }

    private static int index(int percent) { return (Math.max(MIN, Math.min(MAX, percent)) - MIN) / STEP; }

    private static void bind(Spinner picker, int percent, int label) {
        String[] entries = new String[(MAX - MIN) / STEP + 1];
        for (int n = 0; n < entries.length; ++n)
            entries[n] = picker.getContext().getString(label, MIN + STEP * n);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(picker.getContext(),
                android.R.layout.simple_spinner_item, entries);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        picker.setAdapter(adapter);
        picker.setSelection(index(percent));
    }

    void save(SharedPreferences.Editor editor) {
        editor.putInt(HUD, MIN + STEP * hud.getSelectedItemPosition());
        editor.putInt(CONTROLS, MIN + STEP * controls.getSelectedItemPosition());
    }
}
