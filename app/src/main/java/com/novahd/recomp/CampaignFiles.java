package com.novahd.recomp;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class CampaignFiles {
    static final long ARCHIVE_LIMIT = 512L * 1024 * 1024;
    static final long RESERVE = 16L * 1024 * 1024;
    private static final char[] HEX = "0123456789abcdef".toCharArray();
    private static final String[] PREFIXES = {"Payload/NovaHD.app/", "assets/NovaHD.app/", "NovaHD.app/", ""};

    interface Check { void run() throws IOException; }

    private static final class Entry {
        final String path, hash;
        final long size;
        Entry(String path, String hash, long size) {
            this.path = path;
            this.hash = hash;
            this.size = size;
        }
    }

    private final List<Entry> entries = new ArrayList<>();
    private final File root, installed, stage, backup;
    private long size;

    CampaignFiles(File root, BufferedReader manifest) throws IOException {
        this.root = root;
        installed = new File(root, "NovaHD.app");
        stage = new File(root, "campaign-stage");
        backup = new File(root, "campaign-backup");
        Set<String> paths = new HashSet<>();
        String line;
        while ((line = manifest.readLine()) != null) {
            String[] fields = line.split("\t", -1);
            if (fields.length != 3 || !fields[0].matches("[0-9a-f]{64}")
                    || !safe(fields[2]) || !paths.add(fields[2]))
                throw new IOException("El manifiesto de campaña no es válido.");
            long length = Long.parseLong(fields[1]);
            if (length < 0 || length > ARCHIVE_LIMIT - size)
                throw new IOException("El tamaño del manifiesto no es válido.");
            entries.add(new Entry(fields[2], fields[0], length));
            size += length;
        }
        if (!paths.contains("Info.plist") || entries.size() < 2)
            throw new IOException("Faltan entradas en el manifiesto de campaña.");
    }

    private static boolean safe(String path) {
        if (path.contains("\\") || path.contains(":") || path.chars().anyMatch(c -> c < 32)) return false;
        for (String part : path.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        return true;
    }

    void recover() throws IOException {
        if (backup.exists()) {
            if (!installed.exists()) move(backup, installed);
            else remove(backup);
        }
        remove(stage);
    }

    boolean verify(File folder, Check check) throws IOException {
        for (Entry entry : entries)
            if (!matches(new File(folder, entry.path), entry, check)) return false;
        return true;
    }

    boolean verify(Check check) throws IOException { return verify(installed, check); }

    void install(File archive, File legacy, Check check) throws IOException {
        recover();
        requireSpace(root, size);
        Files.createDirectory(stage.toPath());
        try {
            try (ZipFile zip = archive == null ? null : new ZipFile(archive)) {
                String prefix = zip == null ? "" : findPrefix(zip);
                Map<String, ZipEntry> archiveEntries = new HashMap<>();
                if (zip != null) {
                    Set<String> expected = new HashSet<>();
                    for (Entry entry : entries) expected.add(prefix + entry.path);
                    Enumeration<? extends ZipEntry> members = zip.entries();
                    while (members.hasMoreElements()) {
                        ZipEntry member = members.nextElement();
                        if (expected.contains(member.getName()) && archiveEntries.put(member.getName(), member) != null)
                            throw new IOException("Archivo duplicado: " + member.getName());
                    }
                }
                for (Entry entry : entries) {
                    check.run();
                    File previous = new File(legacy == null ? installed : legacy, entry.path);
                    File target = new File(stage, entry.path);
                    Files.createDirectories(target.getParentFile().toPath());
                    boolean reuse = matches(previous, entry, check);
                    ZipEntry member = archiveEntries.get(prefix + entry.path);
                    if (!reuse && (member == null || member.isDirectory() || member.getSize() != entry.size))
                        throw new IOException("Falta un recurso compatible: " + entry.path);
                    try (InputStream input = reuse ? new FileInputStream(previous) : zip.getInputStream(member);
                         FileOutputStream output = new FileOutputStream(target)) {
                        if (!entry.hash.equals(copyHash(input, output, entry.size, check)))
                            throw new IOException("El SHA-256 no coincide: " + entry.path);
                        output.getFD().sync();
                    }
                }
            }
            check.run();
            if (installed.exists()) move(installed, backup);
            try {
                move(stage, installed);
            } catch (IOException error) {
                if (backup.exists()) {
                    try { move(backup, installed); }
                    catch (IOException rollback) { error.addSuppressed(rollback); }
                }
                throw error;
            }
            remove(backup);
        } finally {
            remove(stage);
        }
    }

    private static String findPrefix(ZipFile zip) throws IOException {
        for (String prefix : PREFIXES)
            if (zip.getEntry(prefix + "Info.plist") != null) return prefix;
        throw new IOException("El archivo no contiene un bundle NovaHD.app compatible.");
    }

    private static boolean matches(File file, Entry entry, Check check) throws IOException {
        if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS) || file.length() != entry.size) return false;
        try (InputStream input = new FileInputStream(file)) {
            return entry.hash.equals(copyHash(input, null, entry.size, check));
        }
    }

    private static String copyHash(InputStream input, OutputStream output, long remaining, Check check)
            throws IOException {
        MessageDigest digest;
        try { digest = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
        byte[] block = new byte[65536];
        int read;
        while ((read = input.read(block)) != -1) {
            check.run();
            if (read > remaining) throw new IOException("Un recurso supera el tamaño del manifiesto.");
            digest.update(block, 0, read);
            if (output != null) output.write(block, 0, read);
            remaining -= read;
        }
        if (remaining != 0) throw new IOException("Un recurso está incompleto.");
        byte[] hash = digest.digest();
        char[] hex = new char[hash.length * 2];
        for (int i = 0; i < hash.length; i++) {
            hex[i * 2] = HEX[(hash[i] & 255) >>> 4];
            hex[i * 2 + 1] = HEX[hash[i] & 15];
        }
        return new String(hex);
    }

    static final class StorageException extends IOException {
        StorageException() { super("Espacio insuficiente para preparar la instalación de campaña."); }
    }

    static void requireSpace(File folder, long bytes) throws IOException {
        if (folder.getUsableSpace() - RESERVE < bytes) throw new StorageException();
    }

    long installSize() { return size; }

    static void move(File from, File to) throws IOException {
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE);
    }

    static void remove(File target) throws IOException {
        if (Files.isDirectory(target.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            File[] children = target.listFiles();
            if (children == null) throw new IOException("No se pudo limpiar " + target.getName());
            for (File child : children) remove(child);
        }
        Files.deleteIfExists(target.toPath());
    }
}
