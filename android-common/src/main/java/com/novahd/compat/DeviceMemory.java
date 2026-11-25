package com.novahd.compat;

import android.os.Build;
import android.os.Process;
import android.system.Os;
import android.system.OsConstants;
import java.util.Arrays;

public final class DeviceMemory {
    private DeviceMemory() {}

    public static long pageBytes() {
        return Os.sysconf(OsConstants._SC_PAGESIZE);
    }

    public static boolean arm64() {
        return Process.is64Bit() && Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a");
    }

    public static String report() {
        long bytes = pageBytes();
        String page = bytes > 0 ? bytes + " bytes" : "No se pudo medir";
        if (bytes > 0 && bytes % 1024 == 0) page += " (" + bytes / 1024 + " KiB)";
        String compatibility;
        if (bytes <= 0) {
            compatibility = "No determinada";
        } else if (!arm64()) {
            compatibility = "Requiere un proceso ARM64";
        } else if (bytes == 4096) {
            compatibility = "Cumple el requisito de memoria de 4 KiB";
        } else {
            compatibility = "El motor original requiere páginas de 4 KiB";
        }
        return "Dispositivo: " + Build.MANUFACTURER + " " + Build.MODEL
                + "\nAndroid: " + Build.VERSION.RELEASE + " · API " + Build.VERSION.SDK_INT
                + "\nABI: " + String.join(", ", Build.SUPPORTED_ABIS)
                + "\nPágina de memoria: " + page
                + "\nMotor N.O.V.A. HD 0.4: " + compatibility;
    }
}
