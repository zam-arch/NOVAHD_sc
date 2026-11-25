package com.novahd.recomp;

import android.content.Context;
import android.net.Uri;
import android.os.SystemClock;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class CampaignManager {
    private static CampaignManager instance;

    static synchronized CampaignManager get(Context context) {
        if (instance == null) instance = new CampaignManager(context.getApplicationContext());
        return instance;
    }

    private interface Job { void run() throws IOException; }

    private final Context context;
    private final File root;
    private final CampaignDownload download;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private CampaignFiles files;
    private volatile boolean stop, discard;
    private long sampleTime, sampleBytes;

    volatile boolean busy, ready, network, canCancel;
    volatile String status = "Verificando campaña…";
    volatile long received, total, speed;

    private CampaignManager(Context context) {
        this.context = context;
        root = context.getFilesDir();
        download = new CampaignDownload(root);
        run(false, false, () -> {
            CampaignFiles.remove(new File(root, "campaign-import.zip"));
            try (BufferedReader manifest = new BufferedReader(new InputStreamReader(
                    context.getAssets().open("campaign.tsv"), StandardCharsets.UTF_8))) {
                files = new CampaignFiles(root, manifest);
            }
            files.recover();
            ready = files.verify(this::check);
            File external = context.getExternalFilesDir(null);
            if (!ready && external != null) {
                File previous = new File(external, "NovaHD.app");
                if (files.verify(previous, this::check)) {
                    status = "Migrando la campaña instalada…";
                    files.install(null, previous, this::check);
                    ready = true;
                }
            }
            if (!ready) installEmbedded();
            status = ready ? "Campaña verificada" : "Selecciona una copia legítima o configura una fuente HTTPS autorizada.";
        });
    }

    String source() {
        return context.getSharedPreferences("campaign", Context.MODE_PRIVATE).getString("source", "");
    }

    void setSource(String source) throws IOException {
        if (!source.isEmpty()) CampaignDownload.https(source);
        context.getSharedPreferences("campaign", Context.MODE_PRIVATE).edit().putString("source", source).apply();
    }

    boolean canInstall() { return files != null; }

    boolean hasPartial() { return download.hasPartial(source()); }

    void verifyForLaunch() {
        run(false, false, () -> {
            ready = false;
            status = "Verificando campaña…";
            ready = files.verify(this::check);
            if (!ready) installEmbedded();
            status = ready ? "Campaña verificada" : "Faltan archivos o están dañados. Importa o descarga la campaña para repararla.";
        });
    }

    void fromDocument(Uri document) {
        run(false, true, () -> {
            status = "Leyendo la copia seleccionada…";
            File archive = new File(root, "campaign-import.zip");
            try {
                try (InputStream input = context.getContentResolver().openInputStream(document)) {
                    if (input == null) throw new IOException("No se pudo abrir el archivo seleccionado.");
                    try (FileOutputStream output = new FileOutputStream(archive)) {
                        byte[] block = new byte[65536];
                        long bytes = 0;
                        int n;
                        while ((n = input.read(block)) != -1) {
                            check();
                            bytes += n;
                            if (bytes > CampaignFiles.ARCHIVE_LIMIT)
                                throw new IOException("El archivo supera el límite de 512 MiB.");
                            CampaignFiles.requireSpace(root, files.installSize() + n);
                            output.write(block, 0, n);
                        }
                    }
                }
                install(archive);
            } finally {
                CampaignFiles.remove(archive);
            }
        });
    }

    void fromNetwork() {
        String source = source();
        run(true, true, () -> {
            File archive = null;
            for (int attempt = 0; attempt < 3; attempt++) {
                check();
                sampleTime = 0;
                speed = 0;
                status = "Conectando con la fuente autorizada…";
                try {
                    archive = download.fetch(source, files.installSize(), this::check, this::progress);
                    break;
                } catch (CampaignDownload.Rejected | CampaignFiles.StorageException error) {
                    throw error;
                } catch (IOException error) {
                    check();
                    if (attempt == 2) {
                        throw new IOException("Descarga interrumpida. Pulsa Reanudar o reintenta. " + error.getMessage(), error);
                    }
                    status = "La conexión se interrumpió. Reintentando…";
                    SystemClock.sleep(1000L * (attempt + 1));
                }
            }
            try {
                install(archive);
            } catch (IOException error) {
                if (!stop && !(error instanceof CampaignFiles.StorageException)) download.clear();
                throw error;
            }
            download.clear();
        });
    }

    private void installEmbedded() throws IOException {
        try (InputStream probe = context.getAssets().open("NovaHD.app/Info.plist")) {
            if (probe.read() == -1) throw new IOException("El bundle incluido está incompleto.");
        } catch (FileNotFoundException absent) {
            return;
        }
        install(new File(context.getApplicationInfo().sourceDir));
    }

    private void install(File archive) throws IOException {
        status = "Verificando SHA-256 e instalando la campaña…";
        files.install(archive, null, this::check);
        ready = true;
        status = "Campaña verificada";
    }

    private void progress(long bytes, long size) {
        long now = SystemClock.elapsedRealtime();
        if (sampleTime == 0) {
            sampleTime = now;
            sampleBytes = bytes;
        } else if (now - sampleTime >= 500) {
            speed = (bytes - sampleBytes) * 1000 / (now - sampleTime);
            sampleTime = now;
            sampleBytes = bytes;
        }
        received = bytes;
        total = size;
        status = "Descargando campaña…";
    }

    void pause() { if (network) stop = true; }

    synchronized void cancel() {
        if (canCancel) {
            discard = true;
            stop = true;
        } else if (!busy) {
            run(false, false, () -> { download.clear(); status = "Descarga cancelada."; });
        }
    }

    private void check() throws IOException {
        if (stop) throw new IOException("Operación detenida.");
    }

    private synchronized void run(boolean network, boolean cancellable, Job job) {
        if (busy) return;
        busy = true;
        this.network = network;
        canCancel = cancellable;
        stop = false;
        discard = false;
        total = received = speed = 0;
        executor.execute(() -> {
            try {
                job.run();
            } catch (Exception error) {
                status = stop ? (discard ? "Operación cancelada." : "Descarga pausada.")
                        : error.getMessage() == null ? "No se pudo preparar la campaña." : error.getMessage();
            } finally {
                synchronized (this) {
                    if (discard && network) {
                        try { download.clear(); }
                        catch (IOException error) { status = "No se pudo borrar la descarga parcial."; }
                    }
                    this.network = false;
                    canCancel = false;
                    busy = false;
                }
            }
        });
    }
}
