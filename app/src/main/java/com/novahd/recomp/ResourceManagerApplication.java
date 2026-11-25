package com.novahd.recomp;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

public class ResourceManagerApplication extends Application {
    private static ResourceManagerApplication instance;
    private SharedPreferences prefs;
    private ResourceConfig config;

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        prefs = PreferenceManager.getDefaultSharedPreferences(this);
        config = new ResourceConfig(prefs);
    }

    public static ResourceManagerApplication getInstance() {
        return instance;
    }

    public SharedPreferences getPrefs() {
        return prefs;
    }

    public ResourceConfig getConfig() {
        return config;
    }

    public static class ResourceConfig {
        private static final String KEY_DOWNLOAD_URL = "download_url";
        private static final String KEY_MANIFEST_URL = "manifest_url";
        private static final String KEY_AUTO_REPAIR = "auto_repair";
        private static final String KEY_NOTIFICATIONS = "notifications";

        // Default URLs - these should be configured to point to authorized sources
        private static final String DEFAULT_DOWNLOAD_URL = "https://example.com/novahd/resources/";
        private static final String DEFAULT_MANIFEST_URL = "https://example.com/novahd/resources/manifest.json";

        private final SharedPreferences prefs;

        ResourceConfig(SharedPreferences prefs) {
            this.prefs = prefs;
        }

        public String getDownloadUrl() {
            return prefs.getString(KEY_DOWNLOAD_URL, DEFAULT_DOWNLOAD_URL);
        }

        public void setDownloadUrl(String url) {
            prefs.edit().putString(KEY_DOWNLOAD_URL, url).apply();
        }

        public String getManifestUrl() {
            return prefs.getString(KEY_MANIFEST_URL, DEFAULT_MANIFEST_URL);
        }

        public void setManifestUrl(String url) {
            prefs.edit().putString(KEY_MANIFEST_URL, url).apply();
        }

        public boolean isAutoRepairEnabled() {
            return prefs.getBoolean(KEY_AUTO_REPAIR, true);
        }

        public void setAutoRepairEnabled(boolean enabled) {
            prefs.edit().putBoolean(KEY_AUTO_REPAIR, enabled).apply();
        }

        public boolean areNotificationsEnabled() {
            return prefs.getBoolean(KEY_NOTIFICATIONS, true);
        }

        public void setNotificationsEnabled(boolean enabled) {
            prefs.edit().putBoolean(KEY_NOTIFICATIONS, enabled).apply();
        }
    }
}
