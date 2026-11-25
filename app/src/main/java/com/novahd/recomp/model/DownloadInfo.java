package com.novahd.recomp.model;

import android.os.Parcel;
import android.os.Parcelable;

public class DownloadInfo implements Parcelable {
    public static final int STATUS_PENDING = 0;
    public static final int STATUS_DOWNLOADING = 1;
    public static final int STATUS_PAUSED = 2;
    public static final int STATUS_COMPLETED = 3;
    public static final int STATUS_FAILED = 4;
    public static final int STATUS_CANCELLED = 5;

    public String fileId;
    public String url;
    public String destinationPath;
    public long totalBytes;
    public long downloadedBytes;
    public String expectedSha256;
    public int status = STATUS_PENDING;
    public String errorMessage;
    public long startTime;
    public long lastUpdateTime;
    public int retryCount;

    public DownloadInfo() {}

    protected DownloadInfo(Parcel in) {
        fileId = in.readString();
        url = in.readString();
        destinationPath = in.readString();
        totalBytes = in.readLong();
        downloadedBytes = in.readLong();
        expectedSha256 = in.readString();
        status = in.readInt();
        errorMessage = in.readString();
        startTime = in.readLong();
        lastUpdateTime = in.readLong();
        retryCount = in.readInt();
    }

    public static final Creator<DownloadInfo> CREATOR = new Creator<DownloadInfo>() {
        @Override
        public DownloadInfo createFromParcel(Parcel in) {
            return new DownloadInfo(in);
        }

        @Override
        public DownloadInfo[] newArray(int size) {
            return new DownloadInfo[size];
        }
    };

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(fileId);
        dest.writeString(url);
        dest.writeString(destinationPath);
        dest.writeLong(totalBytes);
        dest.writeLong(downloadedBytes);
        dest.writeString(expectedSha256);
        dest.writeInt(status);
        dest.writeString(errorMessage);
        dest.writeLong(startTime);
        dest.writeLong(lastUpdateTime);
        dest.writeInt(retryCount);
    }

    public float getProgress() {
        if (totalBytes <= 0) return 0f;
        return (float) downloadedBytes / totalBytes;
    }

    public long getRemainingBytes() {
        return totalBytes - downloadedBytes;
    }

    public double getSpeedBytesPerSec() {
        long elapsed = System.currentTimeMillis() - startTime;
        if (elapsed <= 0) return 0;
        return (downloadedBytes * 1000.0) / elapsed;
    }

    public long getEtaMillis() {
        double speed = getSpeedBytesPerSec();
        if (speed <= 0) return -1;
        return (long) (getRemainingBytes() / speed * 1000);
    }

    public String getFormattedSpeed() {
        double speed = getSpeedBytesPerSec();
        if (speed < 1024) return String.format("%.1f B/s", speed);
        if (speed < 1024 * 1024) return String.format("%.1f KB/s", speed / 1024);
        return String.format("%.1f MB/s", speed / (1024 * 1024));
    }

    public String getFormattedEta() {
        long eta = getEtaMillis();
        if (eta < 0) return "Calculando…";
        long seconds = eta / 1000;
        if (seconds < 60) return seconds + "s";
        long minutes = seconds / 60;
        if (minutes < 60) return minutes + "m " + (seconds % 60) + "s";
        long hours = minutes / 60;
        return hours + "h " + (minutes % 60) + "m";
    }
}
