package com.novahd.recomp;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

final class GameSettings {
    private final GameActivity game;
    private final SharedPreferences settings;
    private final LinearLayout content;
    private AlertDialog dialog;

    GameSettings(GameActivity game) {
        this.game = game;
        settings = game.getSharedPreferences(GameDiagnosticsPanel.PREFS, 0);
        content = new LinearLayout(game);
        content.setOrientation(LinearLayout.VERTICAL);
        int inset = Math.round(20 * game.getResources().getDisplayMetrics().density);
        content.setPadding(inset, 0, inset, inset);
    }

    void show() {
        label("Gráficos");
        check("Encuadre original 4:3 · desmarca para panorámica (al volver a abrir el juego)",
                GameActivity.REFERENCE_FRAME, true, () -> {});
        check("Desactivar face culling (experimental)", GameDiagnosticsPanel.CULLING, false, this::applyGraphics);
        check("Mostrar rendimiento y registrar traza", GameDiagnosticsPanel.ENABLED, false, this::applyGraphics);

        label("Audio");
        check("Mostrar música importada en el reproductor del juego", GameDiagnosticsPanel.MUSIC_CATALOG,
                false, () -> {
                    if (!settings.getBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, false)) game.music().restoreGameAudio();
                });
        button("Importar canciones y gestionar listas", () -> open(MusicActivity.class));
        button("Volver a la banda sonora original", () -> game.music().restoreGameAudio());
        label("Volumen de música personalizada");
        SeekBar volume = new SeekBar(game);
        volume.setMax(100);
        volume.setProgress(game.music().volume());
        volume.setContentDescription("Volumen de música personalizada");
        volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar control, int value, boolean fromUser) {
                if (fromUser) game.music().setVolume(value);
            }
            @Override public void onStartTrackingTouch(SeekBar control) {}
            @Override public void onStopTrackingTouch(SeekBar control) {}
        });
        content.addView(volume);

        label("Avanzado");
        button("Copiar última traza guardada", () -> {
            String trace = settings.getString(GameDiagnosticsPanel.TRACE, "");
            if (trace.isEmpty()) {
                Toast.makeText(game, "Activa la traza y vuelve al juego para registrar una muestra.", Toast.LENGTH_SHORT).show();
                return;
            }
            game.getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("NOVA HD GLES", trace));
            Toast.makeText(game, R.string.trace_copied, Toast.LENGTH_SHORT).show();
        });
        button("Información del dispositivo", () -> open(GraphicsDiagnosticsActivity.class));
        button("Herramientas de recuperación", () -> open(ResourceManagerActivity.class));
        button("Restaurar ajustes predeterminados", () -> {
            settings.edit().remove(GameActivity.REFERENCE_FRAME).remove(GameDiagnosticsPanel.CULLING)
                    .remove(GameDiagnosticsPanel.ENABLED).remove(GameDiagnosticsPanel.MUSIC_CATALOG).apply();
            game.music().restoreGameAudio();
            game.music().setVolume(45);
            applyGraphics();
            dialog.dismiss();
            new GameSettings(game).show();
        });
        ScrollView scroll = new ScrollView(game);
        scroll.addView(content);
        dialog = new AlertDialog.Builder(game).setTitle("Configuración avanzada")
                .setView(scroll).setPositiveButton("Volver al juego", null).create();
        dialog.show();
    }

    private void applyGraphics() {
        game.updateDiagnostics(settings.getBoolean(GameDiagnosticsPanel.ENABLED, false),
                settings.getInt(GameDiagnosticsPanel.CULLING, 0));
    }

    private void open(Class<?> activity) {
        dialog.dismiss();
        game.startActivity(new Intent(game, activity));
    }

    private void label(String title) {
        TextView text = new TextView(game);
        text.setText(title);
        text.setTextSize(16);
        content.addView(text);
    }

    private void button(String title, Runnable action) {
        Button button = new Button(game);
        button.setText(title);
        button.setOnClickListener(view -> action.run());
        content.addView(button);
    }

    private void check(String title, String key, boolean fallback, Runnable changed) {
        CheckBox control = new CheckBox(game);
        control.setText(title);
        boolean culling = key.equals(GameDiagnosticsPanel.CULLING);
        control.setChecked(culling ? settings.getInt(key, 0) == 1 : settings.getBoolean(key, fallback));
        control.setOnCheckedChangeListener((view, checked) -> {
            SharedPreferences.Editor editor = settings.edit();
            if (culling) editor.putInt(key, checked ? 1 : 0);
            else editor.putBoolean(key, checked);
            editor.apply();
            changed.run();
        });
        content.addView(control);
    }
}
