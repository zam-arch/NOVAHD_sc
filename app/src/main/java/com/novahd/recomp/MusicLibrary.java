package com.novahd.recomp;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.provider.OpenableColumns;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.util.*;

final class MusicLibrary {
    private static final long TRACK_LIMIT = 128L * 1024 * 1024;
    private static final long LIBRARY_LIMIT = 1024L * 1024 * 1024;
    private static MusicLibrary instance;

    static synchronized MusicLibrary get(Context context) {
        if (instance == null) instance = new MusicLibrary(context.getApplicationContext());
        return instance;
    }

    static final class Track {
        final String id, title, artist, album;
        Track(String id, String title, String artist, String album) {
            this.id = id;
            this.title = title;
            this.artist = artist;
            this.album = album;
        }
        @Override public String toString() { return artist.isEmpty() ? title : title + " — " + artist; }
    }

    private final Context context;
    private final File folder;
    private final SharedPreferences preferences;
    private final Map<String, Track> tracks = new LinkedHashMap<>();
    private final Map<String, List<String>> lists = new LinkedHashMap<>();

    private MusicLibrary(Context context) {
        this.context = context;
        folder = new File(context.getFilesDir(), "user-music");
        preferences = context.getSharedPreferences("music_library", Context.MODE_PRIVATE);
        try {
            JSONArray saved = new JSONArray(preferences.getString("tracks", "[]"));
            for (int i = 0; i < saved.length(); i++) {
                JSONObject item = saved.getJSONObject(i);
                Track track = new Track(item.getString("id"), item.getString("title"), item.optString("artist"), item.optString("album"));
                if (track.id.matches("[0-9a-f-]{36}") && file(track).isFile()) tracks.put(track.id, track);
            }
            JSONObject playlists = new JSONObject(preferences.getString("lists", "{}"));
            for (Iterator<String> names = playlists.keys(); names.hasNext();) {
                String name = names.next();
                JSONArray entries = playlists.getJSONArray(name);
                List<String> members = new ArrayList<>();
                for (int i = 0; i < entries.length(); i++) {
                    String id = entries.getString(i);
                    if (tracks.containsKey(id)) members.add(id);
                }
                lists.put(name, members);
            }
        } catch (JSONException ignored) {
            // A damaged catalog does not make otherwise readable entries unplayable.
        }
    }

    File file(Track track) { return new File(folder, track.id + ".audio"); }

    synchronized List<Track> tracks(String list) {
        if (list == null) return new ArrayList<>(tracks.values());
        List<Track> result = new ArrayList<>();
        List<String> ids = lists.get(list);
        if (ids != null) for (String id : ids) result.add(tracks.get(id));
        return result;
    }

    synchronized List<String> playlists() { return new ArrayList<>(lists.keySet()); }

    synchronized byte[] catalog(int grouping) {
        List<String[]> rows = new ArrayList<>();
        if (grouping == 6) {
            for (String name : lists.keySet()) {
                for (Track track : tracks(name)) rows.add(catalogRow(name, track));
            }
        } else if (grouping == 0 || grouping == 1 || grouping == 2) {
            for (Track track : tracks.values()) {
                String group = grouping == 2 ? track.artist : grouping == 1 ? track.album : track.title;
                rows.add(catalogRow(group, track));
            }
        }
        return MusicCatalog.encode(rows);
    }

    private static String[] catalogRow(String group, Track track) {
        return new String[] {group, track.id, track.title, track.artist, track.album};
    }

    synchronized boolean create(String name) {
        if (name.isEmpty() || name.length() > 64 || lists.containsKey(name)) return false;
        lists.put(name, new ArrayList<>());
        save();
        return true;
    }

    synchronized void deleteList(String name) {
        lists.remove(name);
        save();
    }

    synchronized void add(String name, Track track) {
        List<String> ids = lists.get(name);
        if (ids != null && !ids.contains(track.id)) {
            ids.add(track.id);
            save();
        }
    }

    synchronized void remove(String name, Track track) {
        List<String> ids = lists.get(name);
        if (ids != null && ids.remove(track.id)) save();
    }

    synchronized boolean deleteTrack(Track track) {
        if (file(track).exists() && !file(track).delete()) return false;
        tracks.remove(track.id);
        for (List<String> ids : lists.values()) ids.remove(track.id);
        save();
        return true;
    }

    void importDocument(Uri uri) throws IOException {
        String scheme = uri.getScheme();
        if (!"content".equals(scheme)) throw new IOException("Selecciona un documento de audio.");
        String name = null;
        try (Cursor cursor = context.getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        }
        String type = context.getContentResolver().getType(uri);
        String lower = name == null ? "" : name.toLowerCase(Locale.ROOT);
        if (!lower.matches(".*\\.(mp3|ogg|oga|wav|aac|m4a)$")
                && (type == null || !type.matches("audio/(mpeg|mp3|ogg|wav|wave|x-wav|aac|aacp|mp4|x-m4a)")))
            throw new IOException("Formato no admitido. Usa MP3, OGG, WAV o AAC.");
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("No se pudo crear la biblioteca.");
        long occupied = 0;
        File[] stored = folder.listFiles();
        if (stored != null) for (File f : stored) occupied += f.length();
        long limit = Math.min(TRACK_LIMIT, LIBRARY_LIMIT - occupied);
        if (limit <= 0) throw new IOException("La biblioteca alcanzó su límite de 1 GiB.");
        String id = UUID.randomUUID().toString();
        File stage = new File(folder, id + ".part");
        try {
            try (InputStream input = context.getContentResolver().openInputStream(uri);
                 OutputStream output = new FileOutputStream(stage)) {
                if (input == null) throw new IOException("No se pudo abrir la canción.");
                byte[] buffer = new byte[65536];
                long bytes = 0;
                int read;
                while ((read = input.read(buffer)) != -1) {
                    bytes += read;
                    if (bytes > limit) throw new IOException("El archivo excede el espacio de la biblioteca.");
                    if (folder.getUsableSpace() < 8L * 1024 * 1024)
                        throw new IOException("No hay espacio suficiente para importar.");
                    output.write(buffer, 0, read);
                }
                if (bytes == 0) throw new IOException("El archivo está vacío.");
            }
            String title = name == null ? "Canción importada" : name;
            String artist = "", album = "";
            MediaMetadataRetriever metadata = new MediaMetadataRetriever();
            try (FileInputStream input = new FileInputStream(stage)) {
                metadata.setDataSource(input.getFD());
                String storedTitle = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE);
                if (storedTitle != null && !storedTitle.trim().isEmpty()) title = storedTitle;
                String storedArtist = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST);
                if (storedArtist != null) artist = storedArtist;
                String storedAlbum = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM);
                if (storedAlbum != null) album = storedAlbum;
                if ("yes".equals(metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO))
                        || !"yes".equals(metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO)))
                    throw new IOException("Android no reconoce el documento como una pista de audio.");
            } catch (RuntimeException error) {
                throw new IOException("Android no pudo leer esta pista de audio.", error);
            } finally {
                metadata.release();
            }
            Track track = new Track(id, title, artist, album);
            if (!stage.renameTo(file(track))) throw new IOException("No se pudo instalar la canción.");
            synchronized (this) {
                tracks.put(track.id, track);
                save();
            }
        } finally {
            if (stage.exists()) stage.delete();
        }
    }

    private void save() {
        JSONArray songs = new JSONArray();
        JSONObject playlists = new JSONObject();
        try {
            for (Track track : tracks.values()) {
                songs.put(new JSONObject().put("id", track.id).put("title", track.title).put("artist", track.artist).put("album", track.album));
            }
            for (Map.Entry<String, List<String>> list : lists.entrySet())
                playlists.put(list.getKey(), new JSONArray(list.getValue()));
        } catch (JSONException impossible) {
            throw new IllegalStateException(impossible);
        }
        preferences.edit().putString("tracks", songs.toString()).putString("lists", playlists.toString()).apply();
    }
}
