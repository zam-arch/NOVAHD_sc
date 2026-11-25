package com.novahd.recomp.util;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class CryptoUtils {
    private static final int BUFFER_SIZE = 8192;

    public static String calculateSha256(Path file) throws IOException, NoSuchAlgorithmException {
        return calculateSha256(Files.newInputStream(file));
    }

    public static String calculateSha256(java.io.File file) throws IOException, NoSuchAlgorithmException {
        return calculateSha256(new FileInputStream(file));
    }

    public static String calculateSha256(InputStream input) throws IOException, NoSuchAlgorithmException {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] buffer = new byte[BUFFER_SIZE];
        int bytesRead;
        try {
            while ((bytesRead = input.read(buffer)) != -1) {
                digest.update(buffer, 0, bytesRead);
            }
        } finally {
            input.close();
        }
        return bytesToHex(digest.digest());
    }

    public static boolean verifySha256(Path file, String expectedHash) {
        try {
            String actual = calculateSha256(file);
            return expectedHash.equalsIgnoreCase(actual);
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean verifySha256(java.io.File file, String expectedHash) {
        try {
            String actual = calculateSha256(file);
            return expectedHash.equalsIgnoreCase(actual);
        } catch (Exception e) {
            return false;
        }
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }
}
