package com.generalsx.zerohour;

import android.content.Context;

/**
 * BFME: E1-04 hook for our own online server. Set BFME_ONLINE_URL (Gradle property
 * {@code -PbfmeOnlineUrl=https://...}) and supply an implementation; with no URL the
 * build uses {@link NoopOnlineProvider} and nothing opens a socket.
 */
interface OnlineProvider {
    boolean isEnabled();

    void refreshSessionAsync(Context ctx);

    static OnlineProvider forBuild() {
        // BFME: E1-04 no client for our server yet; always no-op.
        return new NoopOnlineProvider();
    }
}
