package com.novahd.recomp;

import java.io.*;
import java.net.URL;
import java.nio.file.Files;
import java.util.Properties;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;

final class CampaignDownload {
    interface Progress { void update(long received, long total); }

    private final File root, part, metadata;

    CampaignDownload(File root) {
        this.root = root;
        part = new File(root, "campaign-download.part");
        metadata = new File(root, "campaign-download.properties");
    }

    static URL https(String address) throws IOException {
        URL url = new URL(address);
        if (!"https".equals(url.getProtocol()) || url.getHost().isEmpty()
                || url.getUserInfo() != null || url.getRef() != null)
            throw new IOException("Introduce una URL HTTPS autorizada, sin credenciales ni fragmento.");
        return url;
    }

    void clear() throws IOException {
        Files.deleteIfExists(metadata.toPath());
        Files.deleteIfExists(part.toPath());
    }

    private Properties state() throws IOException {
        Properties properties = new Properties();
        if (metadata.isFile()) {
            try (InputStream input = new FileInputStream(metadata)) { properties.load(input); }
        }
        return properties;
    }

    private void save(String source, String etag, long size) throws IOException {
        Properties properties = new Properties();
        properties.setProperty("source", source);
        properties.setProperty("etag", etag);
        properties.setProperty("size", Long.toString(size));
        File temp = new File(root, "campaign-download.tmp");
        try (FileOutputStream output = new FileOutputStream(temp)) {
            properties.store(output, null);
            output.getFD().sync();
        }
        CampaignFiles.move(temp, metadata);
    }

    boolean hasPartial(String source) {
        try {
            Properties state = state();
            return part.length() > 0 && source.equals(state.getProperty("source"));
        } catch (IOException error) { return false; }
    }

    static final class Rejected extends IOException {
        Rejected(String message) { super(message); }
    }

    File fetch(String source, long expanded, CampaignFiles.Check stop, Progress progress) throws IOException {
        URL url = https(source);
        Properties state = state();
        String tag = state.getProperty("etag", "");
        long total = Long.parseLong(state.getProperty("size", "0"));
        boolean same = source.equals(state.getProperty("source"));
        long offset = same ? part.length() : 0;
        if (offset > 0 && offset == total) {
            progress.update(offset, total);
            return part;
        }
        if (tag.isEmpty() || total > CampaignFiles.ARCHIVE_LIMIT || offset >= total) offset = 0;
        if (offset == 0) clear();
        HttpsURLConnection connection = (HttpsURLConnection) url.openConnection();
        connection.setConnectTimeout(8000);
        connection.setReadTimeout(8000);
        connection.setInstanceFollowRedirects(false);
        connection.setRequestProperty("Accept-Encoding", "identity");
        if (offset > 0) {
            connection.setRequestProperty("Range", "bytes=" + offset + "-");
            connection.setRequestProperty("If-Range", tag);
        }
        try {
            stop.run();
            int response = connection.getResponseCode();
            long length = connection.getContentLengthLong();
            String newTag = connection.getHeaderField("ETag");
            if (response == 206 && offset > 0) {
                String expectedRange = "bytes " + offset + "-" + (total - 1) + "/" + total;
                if (length != total - offset || !expectedRange.equals(connection.getHeaderField("Content-Range"))
                        || !tag.equals(newTag)) {
                    clear();
                    throw new IOException("El servidor devolvió una parte incompatible. Reiniciando la descarga.");
                }
            } else if (response == 200) {
                if (length <= 0 || length > CampaignFiles.ARCHIVE_LIMIT)
                    throw new Rejected("El servidor debe indicar un tamaño válido para el archivo de campaña.");
                clear();
                offset = 0;
                total = length;
                String validator = newTag != null && newTag.startsWith("\"") && newTag.endsWith("\"") ? newTag : "";
                save(source, validator, total);
            } else if (response == 416 && offset > 0) {
                clear();
                throw new IOException("El servidor cambió el archivo. Reiniciando la descarga.");
            } else if (response == 408 || response == 429 || response >= 500) {
                throw new IOException("El servidor no respondió correctamente (HTTP " + response + ").");
            } else {
                throw new Rejected("El servidor rechazó la descarga (HTTP " + response
                        + "). Configura una URL HTTPS directa al archivo.");
            }
            CampaignFiles.requireSpace(root, length + expanded);
            progress.update(offset, total);
            try (InputStream input = connection.getInputStream();
                 FileOutputStream output = new FileOutputStream(part, offset > 0)) {
                byte[] buffer = new byte[65536];
                long received = offset;
                int n;
                while ((n = input.read(buffer)) != -1) {
                    stop.run();
                    if (n > total - received) throw new Rejected("El servidor envió más datos de los declarados.");
                    output.write(buffer, 0, n);
                    received += n;
                    progress.update(received, total);
                }
                if (received != total) throw new EOFException("Se interrumpió la descarga antes de completarse.");
                output.getFD().sync();
            }
            stop.run();
            return part;
        } catch (SSLException error) {
            throw new Rejected("No se pudo verificar la conexión HTTPS. Comprueba el certificado y la fecha del dispositivo.");
        } finally {
            connection.disconnect();
        }
    }
}
