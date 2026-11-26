package com.novahd.recomp;

import android.app.Application;

public final class RecoveryApplication extends Application {
    private MusicPlayer music;

    @Override public void onCreate() {
        super.onCreate();
        music = new MusicPlayer(this);
    }

    MusicPlayer music() { return music; }
}
