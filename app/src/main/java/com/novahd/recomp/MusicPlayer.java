package com.novahd.recomp;

import android.app.Activity;
import android.app.Application;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

import java.io.FileInputStream;
import java.io.IOException;
import java.util.*;

final class MusicPlayer implements Application.ActivityLifecycleCallbacks {
    private final Application app;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final AudioManager audio;
    private final AudioAttributes attributes = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
    private final AudioFocusRequest focus;
    private final SharedPreferences preferences;
    private final List<MusicLibrary.Track> selection = new ArrayList<>(), queue = new ArrayList<>();
    private final Runnable background = this::pause;
    private MediaPlayer media;
    private int foreground, index = -1, volume;
    private boolean prepared, playing, requested, focused, shuffle, repeat, completed;
    private String message = "Selecciona una canción de tu biblioteca.";
    private Runnable listener;

    MusicPlayer(Application app) {
        this.app = app;
        audio = app.getSystemService(AudioManager.class);
        preferences = app.getSharedPreferences("music_player", Application.MODE_PRIVATE);
        volume = Math.max(0, Math.min(100, preferences.getInt("volume", 45)));
        shuffle = preferences.getBoolean("shuffle", false);
        repeat = preferences.getBoolean("repeat", false);
        focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attributes).setWillPauseWhenDucked(true)
                .setOnAudioFocusChangeListener(change -> {
                    if (change < 0) pause();
                }, handler).build();
        app.registerActivityLifecycleCallbacks(this);
    }

    boolean shuffle() { return shuffle; }
    boolean repeat() { return repeat; }
    int volume() { return volume; }
    boolean active() { return playing || requested; }
    boolean hasTrack() { return index >= 0; }
    String message() { return message; }

    void observe(Runnable listener) { this.listener = listener; }

    private void changed() { if (listener != null) listener.run(); }

    void setVolume(int value) {
        volume = value;
        preferences.edit().putInt("volume", value).apply();
        if (media != null) applyVolume();
    }

    void setRepeat(boolean enabled) {
        repeat = enabled;
        preferences.edit().putBoolean("repeat", enabled).apply();
    }

    void setShuffle(boolean enabled) {
        shuffle = enabled;
        preferences.edit().putBoolean("shuffle", enabled).apply();
        if (hasTrack()) reorder(queue.get(index));
    }

    private void reorder(MusicLibrary.Track current) {
        queue.clear();
        queue.addAll(selection);
        if (shuffle) {
            queue.remove(current);
            Collections.shuffle(queue);
            queue.add(0, current);
        }
        index = queue.indexOf(current);
    }

    void select(List<MusicLibrary.Track> songs, MusicLibrary.Track current) {
        selection.clear();
        selection.addAll(songs);
        reorder(current);
        load();
    }

    void remove(MusicLibrary.Track track) {
        MusicLibrary.Track current = hasTrack() ? queue.get(index) : null;
        selection.remove(track);
        queue.remove(track);
        if (current == track) stop();
        else index = queue.indexOf(current);
    }

    void skip(int direction) {
        if (!hasTrack()) return;
        int next = index + direction;
        if (next < 0 || next >= queue.size()) {
            if (!repeat) return;
            next = Math.floorMod(next, queue.size());
        }
        index = next;
        load();
    }

    void toggle() {
        if (active()) pause();
        else if (media == null && hasTrack()) load();
        else if (media != null) {
            requested = true;
            if (prepared) start();
            else changed();
        }
    }

    void stop() {
        release();
        selection.clear();
        queue.clear();
        index = -1;
        message = "Solo audio del juego.";
        changed();
    }

    private void load() {
        release();
        if (!hasTrack()) return;
        MusicLibrary.Track track = queue.get(index);
        MediaPlayer candidate = new MediaPlayer();
        media = candidate;
        message = "Abriendo " + track;
        requested = true;
        candidate.setAudioAttributes(attributes);
        candidate.setOnPreparedListener(player -> {
            if (player != media) return;
            prepared = true;
            applyVolume();
            if (requested) start();
            else changed();
        });
        candidate.setOnCompletionListener(player -> {
            if (player != media) return;
            playing = false;
            requested = false;
            completed = true;
            if (index + 1 < queue.size() || repeat) skip(1);
            else {
                abandonFocus();
                message = "Fin de la lista · " + track;
                changed();
            }
        });
        candidate.setOnErrorListener((player, what, extra) -> {
            if (player == media) fail("Error de reproducción (" + what + ", " + extra + "): " + track);
            return true;
        });
        try (FileInputStream file = new FileInputStream(MusicLibrary.get(app).file(track))) {
            candidate.setDataSource(file.getFD());
            candidate.prepareAsync();
        } catch (IOException | RuntimeException error) {
            fail("No se pudo abrir " + track);
        }
        changed();
    }

    private void start() {
        if (foreground == 0) {
            pause();
            return;
        }
        if (!focused && audio.requestAudioFocus(focus) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            requested = false;
            message = "Android no concedió el foco de audio. Pulsa Reproducir para reintentar.";
            changed();
            return;
        }
        focused = true;
        try {
            if (completed) {
                media.seekTo(0);
                completed = false;
            }
            media.start();
            playing = true;
            requested = false;
            message = "Reproduciendo · " + queue.get(index);
            changed();
        } catch (RuntimeException error) {
            fail("Android no pudo iniciar la reproducción.");
        }
    }

    private void applyVolume() {
        if (!prepared) return;
        float gain = volume / 100f;
        media.setVolume(gain, gain);
    }

    private void pause() {
        boolean wasActive = active();
        requested = false;
        if (playing && media != null) {
            try {
                media.pause();
            } catch (RuntimeException error) {
                fail("Android no pudo pausar la reproducción.");
                return;
            }
        }
        playing = false;
        abandonFocus();
        if (hasTrack() && wasActive) message = "En pausa · " + queue.get(index);
        changed();
    }

    private void abandonFocus() {
        if (focused) audio.abandonAudioFocusRequest(focus);
        focused = false;
    }

    private void release() {
        MediaPlayer old = media;
        media = null;
        prepared = playing = requested = completed = false;
        if (old != null) old.release();
        abandonFocus();
    }

    private void fail(String text) {
        release();
        message = text;
        changed();
    }

    @Override public void onActivityStarted(Activity activity) {
        foreground++;
        handler.removeCallbacks(background);
    }

    @Override public void onActivityStopped(Activity activity) {
        foreground = Math.max(0, foreground - 1);
        if (foreground == 0) handler.postDelayed(background, 250);
    }

    @Override public void onActivityCreated(Activity activity, Bundle state) {}
    @Override public void onActivityResumed(Activity activity) {}
    @Override public void onActivityPaused(Activity activity) {}
    @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
    @Override public void onActivityDestroyed(Activity activity) {}
}
