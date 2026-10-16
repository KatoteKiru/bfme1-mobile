package com.generalsx.zerohour;

// BFME: crash-path. Hands the real app directories to the native crash handler
// (AndroidCrashHandler.cpp) right after libmain.so is loaded, and picks which copy of a crash
// log goes into the report: the internal one, or the external mirror when the internal is missing.

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

final class BfmeCrashPaths {
    private BfmeCrashPaths() {}

    private static native void nativeSetDirs(String filesDir, String externalFilesDir);

    /** Call only after libmain.so is loaded in this process. Never throws. */
    static void install(Context c) {
        try {
            File ext = c.getExternalFilesDir(null);
            nativeSetDirs(c.getFilesDir().getAbsolutePath(), ext != null ? ext.getAbsolutePath() : null);
        } catch (Throwable t) {
            Log.w("BFME-crash", "crash log dirs not passed to native: " + t);
        }
    }

    /** Internal copy if it exists, else the external mirror if it exists, else the internal path. */
    static File pick(File internalDir, File externalDir, String name) {
        File in = new File(internalDir, name);
        if (in.isFile() || externalDir == null) return in;
        File ext = new File(externalDir, name);
        return ext.isFile() ? ext : in;
    }

    // BFME: crash-path. Crash-log names that exist in both places (internal + external mirror).
    static final String[] CRASH_LOG_NAMES = {"crash.log", "crash-prev.log", "crash-selftest.log"};
    static final String[] EXTERNAL_ONLY_NAMES = {"generals-stderr.log", "generals-stderr-prev.log"};

    /**
     * Files "Clear logs" deletes: every crash log in both directories (otherwise pick() would put a
     * stale external mirror into the next report) plus the external stderr logs.
     */
    static List<File> filesToClear(File internalDir, File externalDir) {
        List<File> out = new ArrayList<>();
        for (String n : CRASH_LOG_NAMES) out.add(new File(internalDir, n));
        if (externalDir != null) {
            for (String n : CRASH_LOG_NAMES) out.add(new File(externalDir, n));
            for (String n : EXTERNAL_ONLY_NAMES) out.add(new File(externalDir, n));
        }
        return out;
    }
}
