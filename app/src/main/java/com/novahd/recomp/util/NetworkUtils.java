package com.novahd.recomp.util;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.util.Log;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.Headers;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;

public class NetworkUtils {
    private static final String TAG = "NetworkUtils";
    private static OkHttpClient client;
    private static final Object clientLock = new Object();

    public static OkHttpClient getClient() {
        synchronized (clientLock) {
            if (client == null) {
                client = new OkHttpClient.Builder()
                    .connectTimeout(30, TimeUnit.SECONDS)
                    .readTimeout(60, TimeUnit.SECONDS)
                    .writeTimeout(60, TimeUnit.SECONDS)
                    .retryOnConnectionFailure(true)
                    .followRedirects(true)
                    .followSslRedirects(true)
                    .build();
            }
            return client;
        }
    }

    public static boolean isNetworkAvailable(Context context) {
        ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return false;
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            NetworkCapabilities caps = cm.getNetworkCapabilities(cm.getActiveNetwork());
            return caps != null && (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) 
                || caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
                || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));
        } else {
            return cm.getActiveNetworkInfo() != null && cm.getActiveNetworkInfo().isConnected();
        }
    }

    public static void requestNetwork(Context context, NetworkCallback callback) {
        ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            callback.onLost();
            return;
        }
        
        NetworkRequest request = new NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
            .addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET)
            .build();
        
        cm.requestNetwork(request, new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                callback.onAvailable(network);
            }
            
            @Override
            public void onLost(Network network) {
                callback.onLost();
            }
        });
    }

    public static void fetchManifest(String url, ManifestCallback callback) {
        Request request = new Request.Builder()
            .url(url)
            .addHeader("Accept", "application/json")
            .addHeader("User-Agent", "NOVAHD-ResourceManager/0.4")
            .build();
        
        getClient().newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                callback.onError(e.getMessage());
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                try (ResponseBody body = response.body()) {
                    if (!response.isSuccessful() || body == null) {
                        callback.onError("HTTP " + response.code());
                        return;
                    }
                    String json = body.string();
                    callback.onSuccess(json);
                }
            }
        });
    }

    public static long getContentLength(String url, LengthCallback callback) {
        Request request = new Request.Builder()
            .url(url)
            .head()
            .build();
        
        getClient().newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                callback.onError(e.getMessage());
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (response.isSuccessful()) {
                    String length = response.header("Content-Length");
                    if (length != null) {
                        try {
                            callback.onSuccess(Long.parseLong(length));
                        } catch (NumberFormatException e) {
                            callback.onError("Invalid Content-Length");
                        }
                    } else {
                        callback.onError("No Content-Length header");
                    }
                } else {
                    callback.onError("HTTP " + response.code());
                }
            }
        });
        return -1;
    }

    public interface NetworkCallback {
        void onAvailable(Network network);
        void onLost();
    }

    public interface ManifestCallback {
        void onSuccess(String json);
        void onError(String error);
    }

    public interface LengthCallback {
        void onSuccess(long length);
        void onError(String error);
    }
}
