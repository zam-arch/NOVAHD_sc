package com.novahd.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.*;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MusicActivity extends Activity {
    private static final int PICK_AUDIO = 1;
    private static final ExecutorService IMPORTER = Executors.newSingleThreadExecutor();
    private MusicLibrary library;
    private MusicPlayer player;
    private Spinner playlists;
    private ListView songs;
    private TextView status, importStatus;
    private Button play, previous, next;
    private List<String> names = Collections.emptyList();
    private List<MusicLibrary.Track> visible = Collections.emptyList();
    private boolean importing;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        library = MusicLibrary.get(this);
        player = ((RecoveryApplication) getApplication()).music();
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int inset = Math.round(12 * getResources().getDisplayMetrics().density);
        content.setPadding(inset, inset, inset, inset);
        setContentView(content);

        status = new TextView(this);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(status);
        LinearLayout actions = row(content);
        button(actions, "Importar audio", this::chooseAudio);
        button(actions, "Nueva lista", this::newPlaylist);
        button(actions, "Solo audio del juego", player::stop);
        importStatus = new TextView(this);
        importStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(importStatus);

        LinearLayout selector = row(content);
        playlists = new Spinner(this);
        playlists.setContentDescription("Lista musical");
        selector.addView(playlists, new LinearLayout.LayoutParams(0, -2, 2));
        button(selector, "Eliminar lista", this::deletePlaylist);
        songs = new ListView(this);
        songs.setContentDescription("Canciones de la lista. Mantén pulsada una canción para administrarla.");
        content.addView(songs, new LinearLayout.LayoutParams(-1, 0, 1));
        songs.setOnItemClickListener((parent, view, position, id) -> player.select(visible, visible.get(position)));
        songs.setOnItemLongClickListener((parent, view, position, id) -> {
            trackActions(visible.get(position));
            return true;
        });
        playlists.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) { showSongs(); }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        LinearLayout playback = row(content);
        previous = button(playback, "Anterior", () -> player.skip(-1));
        play = button(playback, "Reproducir", player::toggle);
        next = button(playback, "Siguiente", () -> player.skip(1));

        LinearLayout options = row(content);
        CheckBox random = new CheckBox(this);
        random.setText("Aleatorio");
        random.setChecked(player.shuffle());
        random.setOnCheckedChangeListener((view, checked) -> player.setShuffle(checked));
        options.addView(random);
        CheckBox repeat = new CheckBox(this);
        repeat.setText("Repetir");
        repeat.setChecked(player.repeat());
        repeat.setOnCheckedChangeListener((view, checked) -> player.setRepeat(checked));
        options.addView(repeat);
        TextView gainLabel = new TextView(this);
        gainLabel.setText("Música");
        options.addView(gainLabel);
        SeekBar gain = new SeekBar(this);
        gain.setContentDescription("Volumen de música personalizada");
        gain.setMax(100);
        gain.setProgress(player.volume());
        gain.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                if (fromUser) player.setVolume(value);
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });
        options.addView(gain, new LinearLayout.LayoutParams(0, -2, 1));
        if (library.tracks(null).isEmpty())
            importStatus.setText("Importa MP3, OGG, WAV o AAC. Mantén pulsada una canción para añadirla a una lista.");
        if (state != null) rebuild(state.getString("playlist"));
        else rebuild(null);
    }

    @Override protected void onStart() {
        super.onStart();
        player.observe(this::refreshPlayback);
        rebuild(selected());
        refreshPlayback();
    }

    @Override protected void onStop() {
        player.observe(null);
        super.onStop();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("playlist", selected());
        super.onSaveInstanceState(state);
    }

    private void refreshPlayback() {
        status.setText(player.message());
        play.setText(player.active() ? "Pausar" : "Reproducir");
        play.setEnabled(player.hasTrack());
        previous.setEnabled(player.hasTrack());
        next.setEnabled(player.hasTrack());
    }

    private void rebuild(String selected) {
        names = library.playlists();
        List<String> options = new ArrayList<>();
        options.add("Toda la biblioteca");
        options.addAll(names);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, options);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        playlists.setAdapter(adapter);
        playlists.setSelection(names.indexOf(selected) + 1);
        showSongs();
    }

    private String selected() {
        int position = playlists.getSelectedItemPosition();
        return position > 0 && position <= names.size() ? names.get(position - 1) : null;
    }

    private void showSongs() {
        visible = library.tracks(selected());
        songs.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, visible));
    }

    private void chooseAudio() {
        if (importing) return;
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("audio/*");
        pick.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(pick, PICK_AUDIO);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_AUDIO || result != RESULT_OK || data == null) return;
        Set<Uri> documents = new LinkedHashSet<>();
        if (data.getData() != null) documents.add(data.getData());
        ClipData selection = data.getClipData();
        if (selection != null) for (int i = 0; i < selection.getItemCount(); i++) {
            Uri uri = selection.getItemAt(i).getUri();
            if (uri != null) documents.add(uri);
        }
        if (documents.isEmpty()) return;
        importing = true;
        importStatus.setText("Importando archivos de audio…");
        IMPORTER.execute(() -> {
            int succeeded = 0;
            String lastError = null;
            for (Uri uri : documents) {
                try {
                    library.importDocument(uri);
                    succeeded++;
                } catch (IOException | RuntimeException error) {
                    lastError = error instanceof IOException ? error.getMessage() : "No se pudo leer el documento.";
                }
            }
            String summary = "Importadas: " + succeeded + " de " + documents.size()
                    + (lastError == null ? "" : ". " + lastError);
            runOnUiThread(() -> {
                if (isDestroyed() || isFinishing()) return;
                importing = false;
                importStatus.setText(summary);
                rebuild(null);
            });
        });
    }

    private void newPlaylist() {
        EditText input = new EditText(this);
        input.setSingleLine();
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Nombre de la lista").setView(input)
                .setNegativeButton(android.R.string.cancel, null).setPositiveButton("Crear", null).create();
        dialog.setOnShowListener(view -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
            String name = input.getText().toString().trim();
            if (library.create(name)) {
                rebuild(name);
                dialog.dismiss();
            } else input.setError("Usa un nombre nuevo de 1 a 64 caracteres.");
        }));
        dialog.show();
    }

    private void deletePlaylist() {
        String current = selected();
        if (current != null) new AlertDialog.Builder(this).setMessage("¿Eliminar la lista «" + current + "»?")
                .setNegativeButton(android.R.string.cancel, null).setPositiveButton("Eliminar", (dialog, which) -> {
                    library.deleteList(current);
                    rebuild(null);
                }).show();
    }

    private void trackActions(MusicLibrary.Track track) {
        String current = selected();
        String[] options = current == null ? new String[]{"Añadir a una lista", "Eliminar archivo importado"}
                : new String[]{"Añadir a una lista", "Eliminar archivo importado", "Quitar de esta lista"};
        new AlertDialog.Builder(this).setTitle(track.toString()).setItems(options, (dialog, which) -> {
            if (which == 0) {
                if (names.isEmpty()) {
                    importStatus.setText("Crea una lista para añadir canciones.");
                    return;
                }
                List<String> choices = new ArrayList<>(names);
                new AlertDialog.Builder(this).setTitle("Añadir a…")
                        .setItems(choices.toArray(new String[0]), (picker, index) -> {
                            library.add(choices.get(index), track);
                            showSongs();
                            importStatus.setText("Añadida a «" + choices.get(index) + "».");
                        }).show();
            } else if (which == 1) {
                new AlertDialog.Builder(this).setMessage("¿Eliminar la copia de la biblioteca y quitarla de sus listas?")
                        .setNegativeButton(android.R.string.cancel, null).setPositiveButton("Eliminar", (confirm, index) -> {
                            if (library.deleteTrack(track)) {
                                player.remove(track);
                                showSongs();
                            } else importStatus.setText("No se pudo eliminar la copia importada.");
                        }).show();
            } else {
                library.remove(current, track);
                showSongs();
            }
        }).show();
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout row = new LinearLayout(this);
        parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
        return row;
    }

    private Button button(LinearLayout parent, String title, Runnable action) {
        Button control = new Button(this);
        control.setText(title);
        control.setAllCaps(false);
        control.setOnClickListener(view -> action.run());
        parent.addView(control, new LinearLayout.LayoutParams(0, -2, 1));
        return control;
    }
}
