package com.novahd.recomp;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.animation.AccelerateDecelerateInterpolator;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.snackbar.Snackbar;
import com.google.gson.Gson;
import com.google.gson.reflect.TypeToken;
import com.novahd.recomp.model.DownloadInfo;
import com.novahd.recomp.model.ResourceManifest;
import com.novahd.recomp.model.ResourceState;
import com.novahd.recomp.service.DownloadService;
import com.novahd.recomp.util.CryptoUtils;
import com.novahd.recomp.util.FileUtils;
import com.novahd.recomp.util.NetworkUtils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class ResourceManagerActivity extends AppCompatActivity 
        implements DownloadService.DownloadCallback, View.OnClickListener {
    private static final String TAG = "ResourceManagerActivity";
    private static final int REQUEST_CODE_IMPORT = 1001;
    private static final int REQUEST_PERMISSIONS = 1002;
    private static final int REQUEST_NOTIFICATION_PERMISSION = 1003;

    // UI Elements
    private ImageView logoImage;
    private TextView titleText, subtitleText, versionText;
    private TextView statusTitle, statusDetail;
    private LinearLayout progressContainer;
    private TextView progressText, speedText, etaText;
    private LinearProgressIndicator progressBar;
    private MaterialButton primaryButton, pauseButton, cancelButton;
    private MaterialButton importButton, repairButton, settingsButton;
    private LinearLayout secondaryButtons, alternativeButtons;
    private TextView offlineIndicator, creditsText;
    private MaterialCardView statusCard;

    // State
    private ResourceState currentState = ResourceState.UNKNOWN;
    private ResourceManifest manifest;
    private List<DownloadInfo> downloadQueue = new ArrayList<>();
    private DownloadService downloadService;
    private boolean serviceBound = false;
    private ExecutorService executor = Executors.newSingleThreadExecutor();
    private Handler mainHandler = new Handler(Looper.getMainLooper());
    private Gson gson = new Gson();
    private boolean isGameReady = false;

    // Animation
    private Animation pulseAnimation;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Edge-to-edge
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        WindowInsetsControllerCompat insetsController = 
            WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        if (insetsController != null) {
            insetsController.setSystemBarsBehavior(
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }

        setContentView(R.layout.activity_resource_manager);
        initViews();
        initAnimations();
        checkPermissions();
        loadManifest();
        startResourceCheck();
    }

    private void initViews() {
        logoImage = findViewById(R.id.logoImage);
        titleText = findViewById(R.id.titleText);
        subtitleText = findViewById(R.id.subtitleText);
        versionText = findViewById(R.id.versionText);
        statusTitle = findViewById(R.id.statusTitle);
        statusDetail = findViewById(R.id.statusDetail);
        progressContainer = findViewById(R.id.progressContainer);
        progressText = findViewById(R.id.progressText);
        speedText = findViewById(R.id.speedText);
        etaText = findViewById(R.id.etaText);
        progressBar = findViewById(R.id.progressBar);
        primaryButton = findViewById(R.id.primaryButton);
        pauseButton = findViewById(R.id.pauseButton);
        cancelButton = findViewById(R.id.cancelButton);
        secondaryButtons = findViewById(R.id.secondaryButtons);
        importButton = findViewById(R.id.importButton);
        repairButton = findViewById(R.id.repairButton);
        settingsButton = findViewById(R.id.settingsButton);
        alternativeButtons = findViewById(R.id.alternativeButtons);
        offlineIndicator = findViewById(R.id.offlineIndicator);
        creditsText = findViewById(R.id.creditsText);
        statusCard = findViewById(R.id.statusCard);

        primaryButton.setOnClickListener(this);
        pauseButton.setOnClickListener(this);
        cancelButton.setOnClickListener(this);
        importButton.setOnClickListener(this);
        repairButton.setOnClickListener(this);
        settingsButton.setOnClickListener(this);
        logoImage.setOnClickListener(v -> showAboutDialog());

        // Start pulse animation on logo
        logoImage.startAnimation(pulseAnimation);
    }

    private void initAnimations() {
        pulseAnimation = new AlphaAnimation(0.7f, 1.0f);
        pulseAnimation.setDuration(1500);
        pulseAnimation.setInterpolator(new AccelerateDecelerateInterpolator());
        pulseAnimation.setRepeatCount(Animation.INFINITE);
        pulseAnimation.setRepeatMode(Animation.REVERSE);
    }

    private void checkPermissions() {
        List<String> needed = new ArrayList<>();
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.POST_NOTIFICATIONS);
            }
        }
        
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.Q) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED) {
                needed.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
            }
        }
        
        if (!needed.isEmpty()) {
            ActivityCompat.requestPermissions(this,
                needed.toArray(new String[0]), REQUEST_PERMISSIONS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSIONS) {
            // Permissions handled, continue
        }
    }

    private void loadManifest() {
        // Load embedded manifest as fallback
        try (InputStream is = getAssets().open("manifest.json")) {
            byte[] buffer = new byte[is.available()];
            is.read(buffer);
            String json = new String(buffer, "UTF-8");
            manifest = gson.fromJson(json, ResourceManifest.class);
            Log.d(TAG, "Loaded embedded manifest: " + manifest.version);
        } catch (IOException e) {
            Log.w(TAG, "No embedded manifest, will fetch from network");
            manifest = new ResourceManifest();
        }
    }

    private void startResourceCheck() {
        setState(ResourceState.CHECKING);
        executor.execute(this::checkResources);
    }

    private void checkResources() {
        if (manifest == null || manifest.files == null) {
            // Fetch manifest from network
            fetchManifestFromNetwork();
            return;
        }

        File resourceDir = FileUtils.getResourceDir(this);
        File bundleDir = FileUtils.getBundleDir(this);
        
        long totalRequired = manifest.getTotalRequiredSize();
        long freeSpace = FileUtils.getFreeSpace(resourceDir);
        
        runOnUiThread(() -> {
            statusDetail.setText(String.format("Espacio libre: %.2f GB / Requerido: %.2f GB",
                freeSpace / (1024.0 * 1024 * 1024),
                totalRequired / (1024.0 * 1024 * 1024)));
            statusDetail.setVisibility(View.VISIBLE);
        });

        if (!FileUtils.hasEnoughSpace(resourceDir, totalRequired + 500 * 1024 * 1024)) {
            runOnUiThread(() -> setState(ResourceState.ERROR));
            showError("Espacio insuficiente", 
                String.format("Se requieren %.2f GB libres. Disponible: %.2f GB",
                    (totalRequired + 500 * 1024 * 1024) / (1024.0 * 1024 * 1024),
                    freeSpace / (1024.0 * 1024 * 1024)));
            return;
        }

        boolean allPresent = true;
        boolean anyCorrupted = false;
        List<ResourceManifest.ResourceFile> missingFiles = new ArrayList<>();
        List<ResourceManifest.ResourceFile> corruptedFiles = new ArrayList<>();

        for (ResourceManifest.ResourceFile rf : manifest.files) {
            if (!rf.required) continue;
            File targetFile = new File(bundleDir, rf.path);
            if (!targetFile.exists()) {
                allPresent = false;
                missingFiles.add(rf);
            } else if (rf.sha256 != null && !rf.sha256.isEmpty()) {
                if (!CryptoUtils.verifySha256(targetFile, rf.sha256)) {
                    allPresent = false;
                    anyCorrupted = true;
                    corruptedFiles.add(rf);
                }
            }
        }

        runOnUiThread(() -> {
            if (allPresent) {
                isGameReady = true;
                setState(ResourceState.READY);
            } else if (anyCorrupted) {
                setState(ResourceState.CORRUPTED);
                prepareDownloadQueue(corruptedFiles);
            } else {
                setState(ResourceState.MISSING);
                prepareDownloadQueue(missingFiles);
            }
        });
    }

    private void fetchManifestFromNetwork() {
        ResourceManagerApplication app = ResourceManagerApplication.getInstance();
        String manifestUrl = app.getConfig().getManifestUrl();
        
        NetworkUtils.fetchManifest(manifestUrl, new NetworkUtils.ManifestCallback() {
            @Override
            public void onSuccess(String json) {
                manifest = gson.fromJson(json, ResourceManifest.class);
                // Save to assets for offline use
                saveManifestToCache(json);
                runOnUiThread(() -> checkResources());
            }

            @Override
            public void onError(String error) {
                runOnUiThread(() -> {
                    setState(ResourceState.ERROR);
                    showError("Error de red", "No se pudo obtener el manifiesto: " + error 
                        + "\nComprueba tu conexión o importa una copia local.");
                });
            }
        });
    }

    private void saveManifestToCache(String json) {
        try (FileOutputStream fos = openFileOutput("manifest.json", MODE_PRIVATE)) {
            fos.write(json.getBytes("UTF-8"));
        } catch (IOException e) {
            Log.w(TAG, "Failed to cache manifest", e);
        }
    }

    private void prepareDownloadQueue(List<ResourceManifest.ResourceFile> files) {
        downloadQueue.clear();
        String baseUrl = ResourceManagerApplication.getInstance().getConfig().getDownloadUrl();
        File bundleDir = FileUtils.getBundleDir(this);
        
        for (ResourceManifest.ResourceFile rf : files) {
            DownloadInfo info = new DownloadInfo();
            info.fileId = rf.path;
            info.url = rf.getFullUrl(baseUrl);
            info.destinationPath = new File(bundleDir, rf.path).getAbsolutePath();
            info.totalBytes = rf.size;
            info.expectedSha256 = rf.sha256;
            info.status = DownloadInfo.STATUS_PENDING;
            downloadQueue.add(info);
        }
    }

    private void setState(ResourceState state) {
        currentState = state;
        runOnUiThread(() -> updateUI());
    }

    private void updateUI() {
        statusTitle.setText(currentState.getDisplayName());
        
        switch (currentState) {
            case CHECKING:
            case VERIFYING:
            case EXTRACTING:
            case REPAIRING:
            case IMPORTING:
                showProgressUI(true);
                showPrimaryButton(false);
                showSecondaryButtons(false);
                showAlternativeButtons(true);
                break;
                
            case DOWNLOADING:
                showProgressUI(true);
                showPrimaryButton(false);
                showSecondaryButtons(true);
                showAlternativeButtons(false);
                break;
                
            case PAUSED:
                showProgressUI(true);
                primaryButton.setText(R.string.btn_resume);
                primaryButton.setVisibility(View.VISIBLE);
                showSecondaryButtons(false);
                showAlternativeButtons(true);
                break;
                
            case READY:
                showProgressUI(false);
                primaryButton.setText(R.string.btn_play);
                primaryButton.setVisibility(View.VISIBLE);
                isGameReady = true;
                offlineIndicator.setVisibility(View.VISIBLE);
                logoImage.clearAnimation();
                showAlternativeButtons(true);
                break;
                
            case MISSING:
            case CORRUPTED:
                showProgressUI(false);
                primaryButton.setText(R.string.btn_download);
                primaryButton.setVisibility(View.VISIBLE);
                showAlternativeButtons(true);
                break;
                
            case ERROR:
                showProgressUI(false);
                primaryButton.setText(R.string.btn_retry);
                primaryButton.setVisibility(View.VISIBLE);
                showAlternativeButtons(true);
                break;
                
            default:
                showProgressUI(false);
                showPrimaryButton(false);
                showSecondaryButtons(false);
                showAlternativeButtons(false);
        }
    }

    private void showProgressUI(boolean show) {
        progressContainer.setVisibility(show ? View.VISIBLE : View.GONE);
        statusDetail.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    private void showPrimaryButton(boolean show) {
        primaryButton.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    private void showSecondaryButtons(boolean show) {
        secondaryButtons.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    private void showAlternativeButtons(boolean show) {
        alternativeButtons.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        if (id == R.id.primaryButton) {
            onPrimaryAction();
        } else if (id == R.id.pauseButton) {
            onPauseAll();
        } else if (id == R.id.cancelButton) {
            onCancelAll();
        } else if (id == R.id.importButton) {
            onImport();
        } else if (id == R.id.repairButton) {
            onRepair();
        } else if (id == R.id.settingsButton) {
            showSettingsDialog();
        }
    }

    private void onPrimaryAction() {
        switch (currentState) {
            case READY:
                launchGame();
                break;
            case MISSING:
            case CORRUPTED:
                startDownload();
                break;
            case PAUSED:
                resumeDownload();
                break;
            case ERROR:
                startResourceCheck();
                break;
        }
    }

    private void onPauseAll() {
        if (downloadService != null) {
            for (DownloadInfo info : downloadQueue) {
                if (info.status == DownloadInfo.STATUS_DOWNLOADING) {
                    downloadService.pauseDownload(info.fileId);
                }
            }
        }
        setState(ResourceState.PAUSED);
    }

    private void onCancelAll() {
        new MaterialAlertDialogBuilder(this)
            .setTitle("Cancelar descargas")
            .setMessage("¿Cancelar todas las descargas en progreso?")
            .setPositiveButton("Sí, cancelar", (d, w) -> {
                if (downloadService != null) {
                    for (DownloadInfo info : downloadQueue) {
                        downloadService.cancelDownload(info.fileId);
                    }
                }
                downloadQueue.clear();
                setState(downloadQueue.isEmpty() ? ResourceState.MISSING : ResourceState.ERROR);
            })
            .setNegativeButton("No", null)
            .show();
    }

    private void onImport() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_CODE_IMPORT);
    }

    private void onRepair() {
        setState(ResourceState.REPAIRING);
        executor.execute(() -> {
            repairResources();
            runOnUiThread(() -> {
                if (isGameReady) {
                    setState(ResourceState.READY);
                    showSnackbar("Reparación completada");
                } else {
                    setState(ResourceState.CORRUPTED);
                }
            });
        });
    }

    private void repairResources() {
        if (manifest == null || manifest.files == null) return;
        
        File bundleDir = FileUtils.getBundleDir(this);
        List<ResourceManifest.ResourceFile> toRepair = new ArrayList<>();
        
        for (ResourceManifest.ResourceFile rf : manifest.files) {
            if (!rf.required) continue;
            File targetFile = new File(bundleDir, rf.path);
            if (!targetFile.exists() || (rf.sha256 != null && !rf.sha256.isEmpty() 
                    && !CryptoUtils.verifySha256(targetFile, rf.sha256))) {
                toRepair.add(rf);
            }
        }
        
        prepareDownloadQueue(toRepair);
        if (!downloadQueue.isEmpty()) {
            // Download will be started from UI thread
            runOnUiThread(this::startDownload);
        }
    }

    private void startDownload() {
        if (downloadQueue.isEmpty()) return;
        
        // Check network
        if (!NetworkUtils.isNetworkAvailable(this)) {
            showError("Sin conexión", getString(R.string.error_network));
            return;
        }
        
        setState(ResourceState.DOWNLOADING);
        
        // Start service
        Intent serviceIntent = new Intent(this, DownloadService.class);
        serviceIntent.setAction(DownloadService.ACTION_START);
        for (DownloadInfo info : downloadQueue) {
            if (info.status == DownloadInfo.STATUS_PENDING || 
                info.status == DownloadInfo.STATUS_FAILED) {
                serviceIntent.putExtra(DownloadService.EXTRA_DOWNLOAD_INFO, info);
                startForegroundService(serviceIntent);
                break; // Service processes queue sequentially
            }
        }
    }

    private void resumeDownload() {
        if (downloadService != null) {
            for (DownloadInfo info : downloadQueue) {
                if (info.status == DownloadInfo.STATUS_PAUSED) {
                    downloadService.retryDownload(info);
                }
            }
        }
        setState(ResourceState.DOWNLOADING);
    }

    private void launchGame() {
        Intent intent = new Intent(this, GameActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        finish();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_IMPORT && resultCode == Activity.RESULT_OK && data != null) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                importFromUri(treeUri);
            }
        }
    }

    private void importFromUri(Uri treeUri) {
        setState(ResourceState.IMPORTING);
        getContentResolver().takePersistableUriPermission(treeUri,
            Intent.FLAG_GRANT_READ_URI_PERMISSION);
        
        executor.execute(() -> {
            try {
                importBundle(treeUri);
                runOnUiThread(() -> {
                    isGameReady = true;
                    setState(ResourceState.READY);
                    showSnackbar(getString(R.string.import_success));
                });
            } catch (Exception e) {
                Log.e(TAG, "Import failed", e);
                runOnUiThread(() -> {
                    setState(ResourceState.ERROR);
                    showError("Error al importar", e.getMessage());
                });
            }
        });
    }

    private void importBundle(Uri treeUri) throws IOException {
        // Find NovaHD.app in the selected directory
        // This uses DocumentsContract to traverse the tree
        File bundleDir = FileUtils.getBundleDir(this);
        FileUtils.deleteRecursive(bundleDir);
        bundleDir.mkdirs();
        
        // For simplicity, we assume the user selected the NovaHD.app folder directly
        // or a folder containing it. A full implementation would traverse the tree.
        // This is a placeholder - real implementation needs DocumentsContract APIs
        throw new UnsupportedOperationException("SAF import requires DocumentsContract traversal");
    }

    // DownloadService.DownloadCallback implementation
    @Override
    public void onProgress(DownloadInfo info) {
        runOnUiThread(() -> updateProgressUI(info));
    }

    @Override
    public void onCompleted(DownloadInfo info) {
        runOnUiThread(() -> {
            // Check if all downloads complete
            boolean allDone = true;
            for (DownloadInfo di : downloadQueue) {
                if (di.status != DownloadInfo.STATUS_COMPLETED &&
                    di.status != DownloadInfo.STATUS_CANCELLED) {
                    allDone = false;
                    break;
                }
            }
            
            if (allDone) {
                verifyAllDownloads();
            }
            updateProgressUI(info);
        });
    }

    @Override
    public void onFailed(DownloadInfo info, String error) {
        runOnUiThread(() -> {
            updateProgressUI(info);
            if (info.retryCount >= 3) {
                showSnackbar("Error: " + error + " (reintentos agotados)");
            }
        });
    }

    @Override
    public void onPaused(DownloadInfo info) {
        runOnUiThread(() -> updateProgressUI(info));
    }

    @Override
    public void onAllCompleted() {
        runOnUiThread(() -> verifyAllDownloads());
    }

    private void updateProgressUI(DownloadInfo info) {
        // Update overall progress
        long totalDownloaded = 0;
        long totalSize = 0;
        double totalSpeed = 0;
        long maxEta = 0;
        int activeCount = 0;
        
        for (DownloadInfo di : downloadQueue) {
            totalDownloaded += di.downloadedBytes;
            totalSize += di.totalBytes;
            totalSpeed += di.getSpeedBytesPerSec();
            if (di.getEtaMillis() > maxEta) maxEta = di.getEtaMillis();
            if (di.status == DownloadInfo.STATUS_DOWNLOADING) activeCount++;
        }
        
        if (totalSize > 0) {
            int progress = (int) ((totalDownloaded * 100) / totalSize);
            progressBar.setProgress(progress, true);
        }
        
        progressText.setText(String.format("%.1f MB / %.1f MB (%d%%)",
            totalDownloaded / (1024.0 * 1024),
            totalSize / (1024.0 * 1024),
            totalSize > 0 ? (int)((totalDownloaded * 100) / totalSize) : 0));
        
        speedText.setText(String.format("%.1f MB/s", totalSpeed / (1024.0 * 1024)));
        etaText.setText(formatEta(maxEta));
    }

    private String formatEta(long millis) {
        if (millis <= 0) return "Calculando…";
        long seconds = millis / 1000;
        if (seconds < 60) return seconds + "s";
        long minutes = seconds / 60;
        if (minutes < 60) return minutes + "m " + (seconds % 60) + "s";
        long hours = minutes / 60;
        return hours + "h " + (minutes % 60) + "m";
    }

    private void verifyAllDownloads() {
        setState(ResourceState.VERIFYING);
        executor.execute(() -> {
            boolean allValid = true;
            File bundleDir = FileUtils.getBundleDir(this);
            
            for (DownloadInfo info : downloadQueue) {
                File file = new File(info.destinationPath);
                if (!file.exists()) {
                    allValid = false;
                    break;
                }
                if (info.expectedSha256 != null && !info.expectedSha256.isEmpty()) {
                    if (!CryptoUtils.verifySha256(file, info.expectedSha256)) {
                        allValid = false;
                        break;
                    }
                }
            }
            
            runOnUiThread(() -> {
                if (allValid) {
                    isGameReady = true;
                    setState(ResourceState.READY);
                    showSnackbar("¡Recursos instalados correctamente!");
                } else {
                    setState(ResourceState.CORRUPTED);
                    showError("Verificación fallida", 
                        "Algunos archivos no pasaron la verificación SHA-256. Usa Reparar.");
                }
            });
        });
    }

    private void showSettingsDialog() {
        ResourceManagerApplication app = ResourceManagerApplication.getInstance();
        ResourceManagerApplication.ResourceConfig config = app.getConfig();
        
        View dialogView = getLayoutInflater().inflate(R.layout.dialog_settings, null);
        // Settings dialog would be created here - simplified for now
        new MaterialAlertDialogBuilder(this)
            .setTitle(R.string.settings_title)
            .setMessage("URL de descarga: " + config.getDownloadUrl() + "\n"
                + "URL de manifiesto: " + config.getManifestUrl() + "\n\n"
                + "Edita estos valores en la configuración avanzada.")
            .setPositiveButton("OK", null)
            .setNeutralButton("Avanzado", (d, w) -> showAdvancedSettings(config))
            .show();
    }

    private void showAdvancedSettings(ResourceManagerApplication.ResourceConfig config) {
        // Advanced settings implementation
        Toast.makeText(this, "Configuración avanzada: pendiente", Toast.LENGTH_SHORT).show();
    }

    private void showAboutDialog() {
        new MaterialAlertDialogBuilder(this)
            .setTitle(R.string.welcome_title)
            .setMessage(getString(R.string.about_credits))
            .setPositiveButton("OK", null)
            .show();
    }

    private void showError(String title, String message) {
        new MaterialAlertDialogBuilder(this)
            .setTitle(title)
            .setMessage(message)
            .setPositiveButton("OK", null)
            .show();
    }

    private void showSnackbar(String message) {
        Snackbar.make(findViewById(android.R.id.content), message, Snackbar.LENGTH_LONG).show();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdown();
    }
}
