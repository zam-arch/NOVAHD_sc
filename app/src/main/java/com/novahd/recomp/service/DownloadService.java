package com.novahd.recomp.service;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.preference.PreferenceManager;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import com.novahd.recomp.ResourceManagerApplication;
import com.novahd.recomp.ResourceManagerActivity;
import com.novahd.recomp.model.DownloadInfo;
import com.novahd.recomp.util.CryptoUtils;
import com.novahd.recomp.util.FileUtils;
import com.novahd.recomp.util.NetworkUtils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import okhttp3.Call;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;

public class DownloadService extends Service {
    private static final String TAG = "DownloadService";
    private static final String CHANNEL_ID = "novahd_download";
    private static final int NOTIFICATION_ID = 1001;
    private static final int MAX_RETRIES = 3;
    private static final long UPDATE_INTERVAL_MS = 500;

    private ExecutorService executor;
    private OkHttpClient httpClient;
    private PowerManager.WakeLock wakeLock;
    private NotificationManager notificationManager;
    private List<DownloadInfo> downloadQueue;
    private AtomicBoolean isRunning = new AtomicBoolean(false);
    private DownloadCallback callback;
    private SharedPreferences prefs;

    public interface DownloadCallback {
        void onProgress(DownloadInfo info);
        void onCompleted(DownloadInfo info);
        void onFailed(DownloadInfo info, String error);
        void onPaused(DownloadInfo info);
        void onAllCompleted();
    }

    public static final String ACTION_START = "com.novahd.recomp.action.START_DOWNLOAD";
    public static final String ACTION_PAUSE = "com.novahd.recomp.action.PAUSE_DOWNLOAD";
    public static final String ACTION_CANCEL = "com.novahd.recomp.action.CANCEL_DOWNLOAD";
    public static final String ACTION_RETRY = "com.novahd.recomp.action.RETRY_DOWNLOAD";
    public static final String EXTRA_DOWNLOAD_INFO = "download_info";

    @Override
    public void onCreate() {
        super.onCreate();
        executor = Executors.newSingleThreadExecutor();
        httpClient = NetworkUtils.getClient();
        prefs = PreferenceManager.getDefaultSharedPreferences(this);
        
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "NOVAHD::Download");
        
        notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        createNotificationChannel();
        
        downloadQueue = new ArrayList<>();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) return START_STICKY;
        
        String action = intent.getAction();
        DownloadInfo info = intent.getParcelableExtra(EXTRA_DOWNLOAD_INFO);
        
        switch (action) {
            case ACTION_START:
                if (info != null) {
                    addDownload(info);
                }
                break;
            case ACTION_PAUSE:
                if (info != null) pauseDownload(info.fileId);
                break;
            case ACTION_CANCEL:
                if (info != null) cancelDownload(info.fileId);
                break;
            case ACTION_RETRY:
                if (info != null) retryDownload(info);
                break;
        }
        return START_STICKY;
    }

    public void setCallback(DownloadCallback callback) {
        this.callback = callback;
    }

    public void addDownload(DownloadInfo info) {
        info.status = DownloadInfo.STATUS_PENDING;
        info.startTime = System.currentTimeMillis();
        downloadQueue.add(info);
        processQueue();
    }

    public void pauseDownload(String fileId) {
        for (DownloadInfo info : downloadQueue) {
            if (info.fileId.equals(fileId) && info.status == DownloadInfo.STATUS_DOWNLOADING) {
                info.status = DownloadInfo.STATUS_PAUSED;
                notifyCallback(info);
            }
        }
    }

    public void cancelDownload(String fileId) {
        for (DownloadInfo info : downloadQueue) {
            if (info.fileId.equals(fileId)) {
                info.status = DownloadInfo.STATUS_CANCELLED;
                notifyCallback(info);
            }
        }
    }

    public void retryDownload(DownloadInfo info) {
        info.status = DownloadInfo.STATUS_PENDING;
        info.retryCount = 0;
        info.errorMessage = null;
        info.downloadedBytes = 0;
        processQueue();
    }

    private void processQueue() {
        if (isRunning.get()) return;
        
        for (DownloadInfo info : downloadQueue) {
            if (info.status == DownloadInfo.STATUS_PENDING || 
                info.status == DownloadInfo.STATUS_FAILED) {
                if (info.retryCount < MAX_RETRIES) {
                    isRunning.set(true);
                    wakeLock.acquire();
                    startForeground(NOTIFICATION_ID, buildNotification(info, true));
                    executor.execute(() -> downloadFile(info));
                    return;
                }
            }
        }
        
        // Check if all done
        boolean allDone = true;
        for (DownloadInfo info : downloadQueue) {
            if (info.status != DownloadInfo.STATUS_COMPLETED && 
                info.status != DownloadInfo.STATUS_CANCELLED &&
                info.status != DownloadInfo.STATUS_FAILED) {
                allDone = false;
                break;
            }
        }
        
        if (allDone && callback != null) {
            callback.onAllCompleted();
        }
    }

    private void downloadFile(DownloadInfo info) {
        info.status = DownloadInfo.STATUS_DOWNLOADING;
        notifyCallback(info);
        
        File destFile = new File(info.destinationPath);
        destFile.getParentFile().mkdirs();
        
        long resumeOffset = 0;
        if (destFile.exists()) {
            resumeOffset = destFile.length();
            if (resumeOffset >= info.totalBytes) {
                // Already complete
                verifyAndComplete(info);
                return;
            }
            info.downloadedBytes = resumeOffset;
        }
        
        Request.Builder requestBuilder = new Request.Builder()
            .url(info.url)
            .addHeader("User-Agent", "NOVAHD-ResourceManager/0.4");
        
        if (resumeOffset > 0) {
            requestBuilder.addHeader("Range", "bytes=" + resumeOffset + "-");
        }
        
        Call call = httpClient.newCall(requestBuilder.build());
        
        try (Response response = call.execute()) {
            if (!response.isSuccessful()) {
                throw new IOException("HTTP " + response.code() + " " + response.message());
            }
            
            ResponseBody body = response.body();
            if (body == null) {
                throw new IOException("Empty response body");
            }
            
            long contentLength = body.contentLength();
            if (contentLength <= 0 && resumeOffset == 0) {
                // Try to get from header
                String cl = response.header("Content-Length");
                if (cl != null) contentLength = Long.parseLong(cl);
            }
            
            try (InputStream input = body.byteStream();
                 RandomAccessFile output = new RandomAccessFile(destFile, "rw")) {
                
                output.seek(resumeOffset);
                
                byte[] buffer = new byte[32768];
                int bytesRead;
                long lastUpdate = System.currentTimeMillis();
                
                while ((bytesRead = input.read(buffer)) != -1) {
                    // Check for pause/cancel
                    if (info.status == DownloadInfo.STATUS_PAUSED) {
                        notifyCallback(info);
                        return;
                    }
                    if (info.status == DownloadInfo.STATUS_CANCELLED) {
                        destFile.delete();
                        notifyCallback(info);
                        return;
                    }
                    
                    output.write(buffer, 0, bytesRead);
                    info.downloadedBytes += bytesRead;
                    info.lastUpdateTime = System.currentTimeMillis();
                    
                    // Throttle UI updates
                    long now = System.currentTimeMillis();
                    if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
                        notifyCallback(info);
                        updateNotification(info);
                        lastUpdate = now;
                    }
                }
            }
            
            // Verify and complete
            verifyAndComplete(info);
            
        } catch (IOException e) {
            Log.e(TAG, "Download failed: " + info.fileId, e);
            handleFailure(info, e.getMessage());
        } finally {
            isRunning.set(false);
            wakeLock.release();
            stopForeground(true);
            processQueue();
        }
    }

    private void verifyAndComplete(DownloadInfo info) {
        File file = new File(info.destinationPath);
        if (!file.exists()) {
            handleFailure(info, "File not found after download");
            return;
        }
        
        info.status = DownloadInfo.STATUS_COMPLETED;
        notifyCallback(info);
        
        // Verify SHA-256 if provided
        if (info.expectedSha256 != null && !info.expectedSha256.isEmpty()) {
            info.status = 6; // VERIFYING
            notifyCallback(info);
            
            boolean valid = CryptoUtils.verifySha256(file, info.expectedSha256);
            if (!valid) {
                file.delete();
                handleFailure(info, "SHA-256 verification failed");
                return;
            }
        }
        
        info.status = DownloadInfo.STATUS_COMPLETED;
        notifyCallback(info);
    }

    private void handleFailure(DownloadInfo info, String error) {
        info.retryCount++;
        info.errorMessage = error;
        info.status = DownloadInfo.STATUS_FAILED;
        notifyCallback(info);
    }

    private void notifyCallback(DownloadInfo info) {
        if (callback != null) {
            new android.os.Handler(getMainLooper()).post(() -> callback.onProgress(info));
        }
    }

    private void updateNotification(DownloadInfo info) {
        Notification notification = buildNotification(info, false);
        notificationManager.notify(NOTIFICATION_ID, notification);
    }

    private Notification buildNotification(DownloadInfo info, boolean ongoing) {
        int progress = info.totalBytes > 0 ? (int) (info.getProgress() * 100) : 0;
        
        Intent intent = new Intent(this, ResourceManagerActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, 
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("N.O.V.A. HD - Descargando")
            .setContentText(info.fileId + ": " + String.format("%.1f%%", info.getProgress() * 100))
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentIntent(pendingIntent)
            .setProgress(100, progress, false)
            .setOngoing(ongoing)
            .setOnlyAlertOnce(true)
            .setCategory(NotificationCompat.CATEGORY_PROGRESS);
        
        if (info.status == DownloadInfo.STATUS_PAUSED) {
            builder.setContentText("Pausado: " + info.fileId);
        }
        
        return builder.build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Descargas de N.O.V.A. HD",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Notificaciones de descarga de recursos del juego");
            channel.enableVibration(false);
            channel.setSound(null, null);
            notificationManager.createNotificationChannel(channel);
        }
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        isRunning.set(false);
        wakeLock.release();
        executor.shutdown();
        try {
            executor.awaitTermination(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            executor.shutdownNow();
        }
        stopForeground(true);
    }

    // Constants for DownloadInfo status
    public static final int STATUS_PENDING = 0;
    public static final int STATUS_DOWNLOADING = 1;
    public static final int STATUS_PAUSED = 2;
    public static final int STATUS_COMPLETED = 3;
    public static final int STATUS_FAILED = 4;
    public static final int STATUS_CANCELLED = 5;
}
