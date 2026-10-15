package com.generalsx.zerohour;

import android.content.Context;

/** BFME: E1-04 online disabled: no session, no requests. */
final class NoopOnlineProvider implements OnlineProvider {
    @Override
    public boolean isEnabled() {
        return false;
    }

    @Override
    public void refreshSessionAsync(Context ctx) {
    }
}
