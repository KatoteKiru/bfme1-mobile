package com.generalsx.zerohour.importer;

import android.os.SystemClock;

/** Emit progress at most once per 500 ms or on each whole percent change. */
public final class ProgressThrottle {
    private final long minIntervalMs;
    private long lastEmitMs;
    private int lastPercent = -1;

    public ProgressThrottle(long minIntervalMs) {
        this.minIntervalMs = minIntervalMs;
    }

    public ProgressThrottle() {
        this(500L);
    }

    public boolean shouldEmit(int percent) {
        long now = SystemClock.elapsedRealtime();
        if (percent != lastPercent) {
            lastPercent = percent;
            lastEmitMs = now;
            return true;
        }
        if (now - lastEmitMs >= minIntervalMs) {
            lastEmitMs = now;
            return true;
        }
        return false;
    }

    public void force() {
        lastEmitMs = 0L;
        lastPercent = -1;
    }
}
