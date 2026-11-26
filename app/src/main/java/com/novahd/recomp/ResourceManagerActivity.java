package com.novahd.recomp;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import com.novahd.compat.DeviceMemory;
import android.view.View;
import android.widget.TextView;
import android.widget.CheckBox;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.textfield.TextInputEditText;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

public final class ResourceManagerActivity extends AppCompatActivity {
    private static final int PICK_CAMPAIGN = 1;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private CampaignManager manager;
    private boolean launching;
    private MaterialButton primary, importFile, settings, pause, cancel;
    private TextView title, detail, bytes, speed, eta;
    private LinearProgressIndicator progress;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_resource_manager);
        manager = CampaignManager.get(this);
        launching = state != null && state.getBoolean("launching");
        primary = findViewById(R.id.primaryButton);
        importFile = findViewById(R.id.importButton);
        settings = findViewById(R.id.settingsButton);
        pause = findViewById(R.id.pauseButton);
        cancel = findViewById(R.id.cancelButton);
        title = findViewById(R.id.statusTitle);
        detail = findViewById(R.id.statusDetail);
        bytes = findViewById(R.id.progressText);
        speed = findViewById(R.id.speedText);
        eta = findViewById(R.id.etaText);
        progress = findViewById(R.id.progressBar);
        findViewById(R.id.repairButton).setVisibility(View.GONE);
        ((TextView) findViewById(R.id.versionText)).setText(BuildConfig.VERSION_NAME);
        detail.setVisibility(View.VISIBLE);
        primary.setOnClickListener(view -> {
            if (manager.ready) {
                launching = true;
                manager.verifyForLaunch();
                refresh.run();
            } else {
                manager.fromNetwork();
            }
        });
        importFile.setOnClickListener(view -> chooseArchive());
        settings.setOnClickListener(view -> configureSource());
        pause.setOnClickListener(view -> manager.pause());
        cancel.setOnClickListener(view -> manager.cancel());
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("launching", launching);
    }

    @Override protected void onStart() {
        super.onStart();
        handler.post(refresh);
    }

    @Override protected void onStop() {
        handler.removeCallbacks(refresh);
        super.onStop();
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            handler.removeCallbacks(this);
            boolean busy = manager.busy;
            boolean ready = manager.ready;
            boolean network = manager.network;
            boolean configured = !manager.source().isEmpty();
            boolean partial = !busy && manager.hasPartial();
            if (launching && !busy) {
                launching = false;
                if (ready && launchGame()) return;
            }
            title.setText(ready ? R.string.resources_ready : R.string.resource_manager_title);
            if (!manager.status.contentEquals(detail.getText())) detail.setText(manager.status);
            visible(primary, !busy && manager.canInstall() && (ready || configured));
            primary.setText(ready ? R.string.btn_play : partial ? R.string.btn_resume : R.string.btn_download);
            visible(findViewById(R.id.offlineIndicator), ready && !busy);
            visible(findViewById(R.id.alternativeButtons), !busy && manager.canInstall());
            visible(findViewById(R.id.secondaryButtons), manager.canCancel || partial);
            visible(pause, network);
            visible(cancel, manager.canCancel || partial);
            long total = manager.total, received = manager.received, rate = manager.speed;
            visible(findViewById(R.id.progressContainer), network && total > 0);
            if (network && total > 0) {
                int percent = (int) Math.min(100, received * 100 / total);
                progress.setProgressCompat(percent, false);
                bytes.setText(getString(R.string.progress_format, mib(received), mib(total), percent));
                speed.setText(getString(R.string.speed_format, mib(rate)));
                visible(eta, rate > 0 && received < total);
                if (rate > 0 && received < total) eta.setText(getString(R.string.eta_format,
                        String.format(Locale.getDefault(), "%d s", (total - received) / rate)));
            }
            handler.postDelayed(this, 250);
        }
    };

    private boolean launchGame() {
        if (BuildConfig.FOUR_KIB_ONLY && DeviceMemory.pageBytes() != 4096L) {
            new MaterialAlertDialogBuilder(this).setMessage(R.string.game_runtime_page_size)
                    .setPositiveButton(android.R.string.ok, null).show();
            return false;
        }
        File library = new File(getApplicationInfo().nativeLibraryDir, "libnovahd.so");
        try (InputStream image = getAssets().open("image.bin")) {
            if (!library.isFile() || image.read() == -1) throw new IOException();
        } catch (IOException noRuntime) {
            new MaterialAlertDialogBuilder(this).setMessage(R.string.game_runtime_missing_message)
                    .setPositiveButton(android.R.string.ok, null).show();
            return false;
        }
        startActivity(new Intent(this, GameActivity.class));
        finish();
        return true;
    }

    private void chooseArchive() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_CAMPAIGN);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_CAMPAIGN && result == RESULT_OK && data != null) {
            Uri archive = data.getData();
            if (archive != null) manager.fromDocument(archive);
        }
    }

    private void configureSource() {
        View content = getLayoutInflater().inflate(R.layout.dialog_settings, null);
        TextInputEditText input = content.findViewById(R.id.etDownloadUrl);
        input.setText(manager.source());
        CheckBox overlay = content.findViewById(R.id.graphicsOverlay);
        overlay.setChecked(getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.ENABLED, false));
        CheckBox musicCatalog = content.findViewById(R.id.gameMusicCatalog);
        musicCatalog.setVisibility(BuildConfig.REBUILT_ENGINE ? View.VISIBLE : View.GONE);
        musicCatalog.setChecked(getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, false));
        android.widget.Spinner culling = content.findViewById(R.id.graphicsCulling);
        culling.setVisibility(BuildConfig.REBUILT_ENGINE ? View.VISIBLE : View.GONE);
        int selected = getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE)
                .getInt(GameDiagnosticsPanel.CULLING, 0);
        culling.setSelection(selected >= 0 && selected <= 1 ? selected : 0);
        View copyTrace = content.findViewById(R.id.copyGraphicsTrace);
        String trace = getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE).getString(GameDiagnosticsPanel.TRACE, "");
        copyTrace.setVisibility(trace.isEmpty() ? View.GONE : View.VISIBLE);
        copyTrace.setOnClickListener(view -> {
            android.content.ClipboardManager clipboard = getSystemService(android.content.ClipboardManager.class);
            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("NOVA HD GLES", trace));
            android.widget.Toast.makeText(this, R.string.trace_copied, android.widget.Toast.LENGTH_SHORT).show();
        });
        android.content.SharedPreferences prefs = getSharedPreferences(GameDiagnosticsPanel.PREFS, MODE_PRIVATE);
        CheckBox reference = content.findViewById(R.id.referenceFrame);
        reference.setVisibility(BuildConfig.REBUILT_ENGINE ? View.VISIBLE : View.GONE);
        reference.setChecked(prefs.getBoolean(GameActivity.REFERENCE_FRAME, true));
        AlertDialog dialog = new MaterialAlertDialogBuilder(this).setTitle(R.string.settings_title)
                .setView(content).setNegativeButton(android.R.string.cancel, null)
                .setNeutralButton(R.string.device_diagnostics, (dialogInterface, which) ->
                        startActivity(new Intent(this, GraphicsDiagnosticsActivity.class)))
                .setPositiveButton(android.R.string.ok, null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            try {
                manager.setSource(input.getText().toString().trim());
                android.content.SharedPreferences.Editor editor = prefs.edit()
                        .putBoolean(GameDiagnosticsPanel.ENABLED, overlay.isChecked())
                        .putInt(GameDiagnosticsPanel.CULLING, culling.getSelectedItemPosition())
                        .putBoolean(GameDiagnosticsPanel.MUSIC_CATALOG, musicCatalog.isChecked());
                editor.putBoolean(GameActivity.REFERENCE_FRAME, reference.isChecked());
                editor.apply();
                dialog.dismiss();
            } catch (IOException error) {
                input.setError(error.getMessage());
            }
        }));
        content.findViewById(R.id.musicLibrary).setOnClickListener(view -> {
            dialog.dismiss();
            startActivity(new Intent(this, MusicActivity.class));
        });
        dialog.show();
    }

    private static void visible(View view, boolean enabled) {
        view.setVisibility(enabled ? View.VISIBLE : View.GONE);
    }

    private static double mib(long size) { return size / (1024.0 * 1024.0); }
}
