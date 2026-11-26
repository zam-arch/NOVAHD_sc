package com.novahd.recomp;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.TextView;

import com.novahd.compat.DeviceMemory;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public final class LauncherActivity extends Activity {
    static final String BOOT_FAILED = "boot_pending";
    private static final String PREFS = "game_launch";
    private static final String RECOVERY_ACTION = "com.novahd.recomp.RECOVERY";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private CampaignManager campaign;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        boolean failed = BuildConfig.REBUILT_ENGINE
                && getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(BOOT_FAILED, false);
        int error = runtimeError(this);
        if (failed || RECOVERY_ACTION.equals(getIntent().getAction()) || error != 0) {
            recover(failed, error);
            return;
        }
        TextView status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setText(R.string.launch_checking);
        setContentView(status);
        campaign = CampaignManager.get(this);
        campaign.verifyForLaunch(); // If initial verification is in progress, reuse that result.
    }

    @Override protected void onStart() {
        super.onStart();
        if (campaign != null) handler.post(route);
    }

    @Override protected void onStop() {
        handler.removeCallbacks(route);
        super.onStop();
    }

    private final Runnable route = new Runnable() {
        @Override public void run() {
            if (campaign.busy) {
                handler.postDelayed(this, 100);
            } else if (campaign.ready && startGame(LauncherActivity.this)) {
                finish();
            } else {
                recover(campaign.ready, 0);
            }
        }
    };

    private void recover(boolean failed, int error) {
        startActivity(new Intent(this, ResourceManagerActivity.class)
                .putExtra(BOOT_FAILED, failed).putExtra("runtime_error", error));
        finish();
    }

    static int runtimeError(Context context) {
        if (BuildConfig.FOUR_KIB_ONLY && DeviceMemory.pageBytes() != 4096L)
            return R.string.game_runtime_page_size;
        File library = new File(context.getApplicationInfo().nativeLibraryDir, "libnovahd.so");
        try (InputStream image = context.getAssets().open("image.bin")) {
            if (library.isFile() && image.read() != -1) return 0;
        } catch (IOException ignored) {}
        return R.string.game_runtime_missing_message;
    }

    static boolean startGame(Activity activity) {
        if (BuildConfig.REBUILT_ENGINE && !activity.getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit().putBoolean(BOOT_FAILED, true).commit()) return false;
        activity.startActivity(new Intent(activity, GameActivity.class));
        return true;
    }

    static void confirmBoot(Context context) {
        context.getSharedPreferences(PREFS, MODE_PRIVATE).edit().remove(BOOT_FAILED).apply();
    }
}
