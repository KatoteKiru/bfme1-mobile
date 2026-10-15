package com.generalsx.zerohour;

import android.content.Context;

/** BFME: E1-04 no network, no download, no remote_config.ini. */
final class NoopUpdateProvider implements UpdateProvider {
    @Override
    public UpdateManager.Result check(Context ctx, boolean withEngine) {
        UpdateManager.Result r = new UpdateManager.Result();
        r.ok = true;
        return r;
    }
}
