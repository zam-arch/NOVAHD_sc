package com.novahd.recomp.util;

import android.content.Context;
import android.os.Environment;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.GZIPInputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class FileUtils {
    private static final String TAG = "FileUtils";
    private static final int BUFFER_SIZE = 32768;

    public static File getResourceDir(Context context) {
        // Private app storage - no need for Android/data or OBB
        File dir = new File(context.getFilesDir(), "resources");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    public static File getTempDir(Context context) {
        File dir = new File(context.getCacheDir(), "downloads");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    public static File getBundleDir(Context context) {
        File dir = new File(context.getFilesDir(), "NovaHD.app");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    public static long getFreeSpace(File path) {
        return path.getUsableSpace();
    }

    public static boolean hasEnoughSpace(File path, long requiredBytes) {
        return getFreeSpace(path) >= requiredBytes;
    }

    public static void copyFile(File src, File dst) throws IOException {
        dst.getParentFile().mkdirs();
        try (InputStream in = new FileInputStream(src);
             OutputStream out = new FileOutputStream(dst)) {
            byte[] buffer = new byte[BUFFER_SIZE];
            int len;
            while ((len = in.read(buffer)) > 0) {
                out.write(buffer, 0, len);
            }
        }
    }

    public static void extractZip(File zipFile, File destDir, ExtractionCallback callback) throws IOException {
        destDir.mkdirs();
        try (ZipInputStream zis = new ZipInputStream(new BufferedInputStream(new FileInputStream(zipFile), BUFFER_SIZE))) {
            ZipEntry entry;
            long totalSize = 0;
            long extractedSize = 0;
            
            // First pass: calculate total size
            try (ZipInputStream zis2 = new ZipInputStream(new BufferedInputStream(new FileInputStream(zipFile), BUFFER_SIZE))) {
                ZipEntry e;
                while ((e = zis2.getNextEntry()) != null) {
                    if (!e.isDirectory()) totalSize += e.getSize();
                }
            }

            while ((entry = zis.getNextEntry()) != null) {
                File outFile = new File(destDir, entry.getName());
                
                if (entry.isDirectory()) {
                    outFile.mkdirs();
                    continue;
                }
                
                outFile.getParentFile().mkdirs();
                
                try (FileOutputStream fos = new FileOutputStream(outFile)) {
                    byte[] buffer = new byte[BUFFER_SIZE];
                    int len;
                    while ((len = zis.read(buffer)) > 0) {
                        fos.write(buffer, 0, len);
                        extractedSize += len;
                        if (callback != null) {
                            callback.onProgress(extractedSize, totalSize, entry.getName());
                        }
                    }
                }
                zis.closeEntry();
            }
        }
    }

    public static void extractGzip(File gzFile, File destFile, ExtractionCallback callback) throws IOException {
        destFile.getParentFile().mkdirs();
        try (GZIPInputStream gis = new GZIPInputStream(new BufferedInputStream(new FileInputStream(gzFile), BUFFER_SIZE));
             FileOutputStream fos = new FileOutputStream(destFile)) {
            byte[] buffer = new byte[BUFFER_SIZE];
            int len;
            long extracted = 0;
            long total = gzFile.length() * 4; // Rough estimate for compressed
            while ((len = gis.read(buffer)) > 0) {
                fos.write(buffer, 0, len);
                extracted += len;
                if (callback != null) {
                    callback.onProgress(extracted, total, destFile.getName());
                }
            }
        }
    }

    public static void deleteRecursive(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        file.delete();
    }

    public static long getDirectorySize(File dir) {
        long size = 0;
        File[] files = dir.listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.isDirectory()) {
                    size += getDirectorySize(f);
                } else {
                    size += f.length();
                }
            }
        }
        return size;
    }

    public interface ExtractionCallback {
        void onProgress(long extracted, long total, String currentFile);
    }
}
