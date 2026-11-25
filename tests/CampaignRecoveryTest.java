package com.novahd.recomp;

import java.io.*;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.cert.Certificate;
import java.util.HexFormat;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import javax.net.ssl.HttpsURLConnection;

public final class CampaignRecoveryTest {
    private static final byte[] PLIST = {1, 2, 3};
    private static final byte[] LEVEL = {4, 5, 6, 7};
    private static final CampaignFiles.Check CONTINUE = () -> {};
    private static final String TEST_URL = "https://fixture.invalid/campaign";
    private static int connections;

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("novahd-recovery-test").toFile();
        try {
            testFiles(root);
            testDownload(root);
            if (args.length == 2) {
                Path directory = Files.createDirectory(root.toPath().resolve("official"));
                try (BufferedReader manifest = Files.newBufferedReader(Path.of(args[0]))) {
                    CampaignFiles official = new CampaignFiles(directory.toFile(), manifest);
                    official.install(new File(args[1]), null, CONTINUE);
                    require(official.verify(CONTINUE), "44 official resources verified after extraction");
                    require(official.installSize() == 185220728L, "official campaign total");
                    Path level = directory.resolve("NovaHD.app/location_01_ship.pack");
                    Files.delete(level);
                    require(!official.verify(CONTINUE), "missing official campaign file detected");
                    official.install(new File(args[1]), null, CONTINUE);
                    require(official.verify(CONTINUE), "official campaign repair");
                    System.out.println("Official 0.4 resources: imported, detected missing pack, repaired and reverified all entries");
                }
            }
        } finally {
            CampaignFiles.remove(root);
        }
    }

    private static void testFiles(File root) throws Exception {
        String table = manifestRow("Info.plist", PLIST) + manifestRow("levels/one.pack", LEVEL);
        CampaignFiles files = new CampaignFiles(root, new BufferedReader(new StringReader(table)));
        require(!files.verify(CONTINUE), "first install detects missing files");
        File good = archive(root, "good", LEVEL);
        files.install(good, null, CONTINUE);
        require(files.verify(CONTINUE), "ZIP installed and verified");
        Path folder = root.toPath().resolve("NovaHD.app");
        Path pack = folder.resolve("levels/one.pack");
        require(!Files.exists(root.toPath().resolve("outside")), "unknown path was ignored");
        Files.write(pack, new byte[4]);
        require(!files.verify(CONTINUE), "same-size corruption detected");
        File damaged = archive(root, "bad", new byte[4]);
        rejects(() -> files.install(damaged, null, CONTINUE), "SHA-256");
        require(Files.readAllBytes(pack)[0] == 0, "failed repair preserved old bundle");
        rejects(() -> files.install(good, null, () -> { throw new IOException("cancelled"); }), "cancelled");
        files.install(good, null, CONTINUE);
        require(files.verify(CONTINUE), "repair passed");
        Files.move(folder, root.toPath().resolve("campaign-backup"));
        files.recover();
        require(files.verify(CONTINUE), "restart restored previous directory");
        rejects(() -> CampaignFiles.requireSpace(root, Long.MAX_VALUE), "Espacio insuficiente");
        System.out.println("Installer fixtures: first install, traversal ignored, full SHA-256, corrupt repair, cancel, rollback and low space");
    }

    private static void testDownload(File root) throws Exception {
        URL.setURLStreamHandlerFactory(protocol -> !"https".equals(protocol) ? null : new URLStreamHandler() {
            @Override protected URLConnection openConnection(URL url) {
                return new HttpsURLConnection(url) {
                    final int attempt = ++connections;
                    @Override public int getResponseCode() throws IOException {
                        if (attempt == 2) {
                            require("bytes=2-".equals(getRequestProperty("Range")), "resume offset");
                            require("\"fixture\"".equals(getRequestProperty("If-Range")), "resume validator");
                            return 206;
                        }
                        return attempt == 3 ? 404 : 200;
                    }
                    @Override public long getContentLengthLong() { return attempt == 2 ? 2 : 4; }
                    @Override public String getHeaderField(String name) {
                        return name.equals("ETag") ? "\"fixture\"" : name.equals("Content-Range") ? "bytes 2-3/4" : null;
                    }
                    @Override public InputStream getInputStream() {
                        return attempt == 2 ? new ByteArrayInputStream(LEVEL, 2, 2) : new ByteArrayInputStream(LEVEL, 0, 2);
                    }
                    @Override public void connect() {}
                    @Override public void disconnect() {}
                    @Override public boolean usingProxy() { return false; }
                    @Override public String getCipherSuite() { return "fixture"; }
                    @Override public Certificate[] getLocalCertificates() { return null; }
                    @Override public Certificate[] getServerCertificates() { return null; }
                };
            }
        });
        CampaignDownload first = new CampaignDownload(root);
        rejects(() -> first.fetch(TEST_URL, 4, CONTINUE, (received, total) -> {}), "interrumpió");
        require(first.hasPartial(TEST_URL), "partial survives failure");
        CampaignDownload restarted = new CampaignDownload(root);
        File finished = restarted.fetch(TEST_URL, 4, CONTINUE, (received, total) -> require(total == 4, "total known"));
        require(java.util.Arrays.equals(Files.readAllBytes(finished.toPath()), LEVEL), "resumed file content");
        restarted.clear();
        require(!restarted.hasPartial(TEST_URL), "cancel removed partial");
        rejects(() -> restarted.fetch(TEST_URL, 4, CONTINUE, (received, total) -> {}), "HTTP 404");
        rejects(() -> CampaignDownload.https("http://fixture.invalid"), "HTTPS");
        System.out.println("HTTP fixtures: interrupted response, persisted Range/If-Range, cancel, error 404 and cleartext rejection");
    }

    private static String manifestRow(String path, byte[] data) throws Exception {
        String hash = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));
        return hash + "\t" + data.length + "\t" + path + "\n";
    }

    private static File archive(File root, String label, byte[] level) throws IOException {
        File output = new File(root, label + ".zip");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(output), StandardCharsets.UTF_8)) {
            add(zip, "Payload/NovaHD.app/Info.plist", PLIST);
            add(zip, "Payload/NovaHD.app/levels/one.pack", level);
            add(zip, "Payload/NovaHD.app/../../outside", PLIST);
        }
        return output;
    }

    private static void add(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }

    private static void rejects(CampaignFiles.Check run, String message) throws IOException {
        try {
            run.run();
            throw new AssertionError("Expected failure: " + message);
        } catch (IOException error) {
            require(error.getMessage().contains(message), "Expected message: " + message + " / " + error);
        }
    }

    private static void require(boolean value, String description) {
        if (!value) throw new AssertionError(description);
    }
}
