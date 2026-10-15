package com.generalsx.zerohour;

import android.content.Context;

/**
 * BFME: E1-04 hook for our own signed OTA. The fork's update host is gone; a build that
 * sets BFME_OTA_URL (Gradle property {@code -PbfmeOtaUrl=https://...}) may plug in a provider
 * here. A provider MUST verify the manifest signature against
 * {@link UpdateManager#PUBLIC_KEY_B64} (our key) and the serial before applying anything.
 * With no URL the build uses {@link NoopUpdateProvider}.
 */
interface UpdateProvider {
    UpdateManager.Result check(Context ctx, boolean withEngine);

    /** BFME: E1-04 empty BFME_OTA_URL means updates are disabled. */
    static UpdateProvider forBuild() {
        String url = BuildConfig.BFME_OTA_URL;
        if (url == null || url.isEmpty()) {
            return new NoopUpdateProvider();
        }
        // BFME: E1-04 no signed-OTA client exists yet; stay no-op until one is written.
        return new NoopUpdateProvider();
    }
}
