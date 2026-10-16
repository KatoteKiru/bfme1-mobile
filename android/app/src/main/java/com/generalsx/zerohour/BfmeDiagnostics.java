package com.generalsx.zerohour;

// BFME: E1-15. Android side of the report: startup metadata file + logcat, the list of report
// files, logcat capture and the "previous run crashed" bookkeeping. Formatting and zipping are
// in ReportFormat (JVM-tested).

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.os.StatFs;
import android.util.Log;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

final class BfmeDiagnostics {
    private BfmeDiagnostics() {}

    static final String TAG = "BFME-diag";
    static final String STARTUP_META = "startup-meta.txt";
    static final String NATIVE_SMOKE = "native-smoke.txt";
    private static final String PREFS = "bfme_diagnostics";
    private static final String PREF_LAST_OFFERED_CRASH = "last_offered_crash_mtime";

    /** One id per launcher process: ties the zip name to the startup-meta of the same session. */
    static final String SESSION = UUID.randomUUID().toString().substring(0, 8);

    static String versionName(Context c) {
        try {
            return c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "unknown";
        }
    }

    static long versionCode(Context c) {
        try {
            PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return pi.getLongVersionCode();
        } catch (Exception e) {
            return -1;
        }
    }

    static Map<String, String> collectMetadata(Context c) {
        Map<String, String> m = new LinkedHashMap<>();
        String vn = versionName(c);
        m.put("time", new java.util.Date().toString());
        m.put("session", SESSION);
        m.put("package", c.getPackageName());
        m.put("versionName", vn);
        m.put("versionCode", String.valueOf(versionCode(c)));
        m.put("gitSha", ReportFormat.gitShaFromVersionName(vn));
        m.put("device", Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + ")");
        if (Build.VERSION.SDK_INT >= 31) {
            m.put("soc", Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL);
        } else {
            m.put("soc", Build.HARDWARE);
        }
        m.put("android", Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")");
        m.put("fingerprint", Build.FINGERPRINT);
        m.put("abis", Arrays.toString(Build.SUPPORTED_ABIS));
        m.put("glRenderer", lastSmokeValue(c, "GL_RENDERER"));
        File state = importStateFile(c);
        if (state != null && state.isFile()) {
            try {
                JSONObject o = new JSONObject(new String(readFile(state), StandardCharsets.UTF_8));
                m.put("gamedata.status", o.optString("status", null));
                m.put("gamedata.profile", o.isNull("profile") ? null : o.optString("profile"));
                m.put("gamedata.manifestSha256", o.isNull("manifestSha256") ? null : o.optString("manifestSha256"));
                m.put("gamedata.lastError", o.isNull("lastError") ? null : o.optString("lastError"));
            } catch (Exception e) {
                m.put("gamedata.status", "unreadable: " + e.getMessage());
            }
        } else {
            m.put("gamedata.status", "no import-state.json");
        }
        m.put("free.internal", freeSpace(c.getFilesDir()));
        m.put("free.external", freeSpace(c.getExternalFilesDir(null)));
        return m;
    }

    /** Writes startup-meta.txt (previous one -> startup-meta-prev.txt) and logs each line. */
    static void writeStartupMetadata(Context c) {
        try {
            String text = ReportFormat.formatMetadata(collectMetadata(c));
            File f = new File(c.getFilesDir(), STARTUP_META);
            File prev = new File(c.getFilesDir(), "startup-meta-prev.txt");
            if (f.exists()) f.renameTo(prev);
            try (FileOutputStream out = new FileOutputStream(f)) {
                out.write(text.getBytes(StandardCharsets.UTF_8));
            }
            for (String line : text.split("\n")) Log.i(TAG, line);
        } catch (Throwable t) {
            Log.w(TAG, "startup metadata failed", t);
        }
    }

    static File importStateFile(Context c) {
        File ext = c.getExternalFilesDir(null);
        return ext == null ? null : new File(ext, "import-state.json");
    }

    /** Report candidates in display order. Files may be absent; ReportFormat skips those. */
    static List<ReportFormat.Entry> candidateFiles(Context c) {
        List<ReportFormat.Entry> l = new ArrayList<>();
        File in = c.getFilesDir();
        File ext = c.getExternalFilesDir(null);
        l.add(new ReportFormat.Entry("crash.log", new File(in, "crash.log")));
        l.add(new ReportFormat.Entry("crash-prev.log", new File(in, "crash-prev.log")));
        l.add(new ReportFormat.Entry("crash-selftest.log", new File(in, "crash-selftest.log")));
        if (ext != null) {
            l.add(new ReportFormat.Entry("generals-stderr.log", new File(ext, "generals-stderr.log")));
            l.add(new ReportFormat.Entry("generals-stderr-prev.log", new File(ext, "generals-stderr-prev.log")));
        }
        l.add(new ReportFormat.Entry(STARTUP_META, new File(in, STARTUP_META)));
        l.add(new ReportFormat.Entry("startup-meta-prev.txt", new File(in, "startup-meta-prev.txt")));
        l.add(new ReportFormat.Entry(NATIVE_SMOKE, new File(in, NATIVE_SMOKE)));
        if (ext != null) {
            l.add(new ReportFormat.Entry("import-state.json", new File(ext, "import-state.json")));
            File[] logs = ext.listFiles((d, n) -> n.startsWith("import") && (n.endsWith(".log") || n.endsWith(".txt")));
            if (logs != null) for (File f : logs) l.add(new ReportFormat.Entry(f.getName(), f));
        }
        return l;
    }

    /** logcat of this process (--pid) and of the app uid (the OS shows an app only its own lines). */
    static byte[] captureLogcat(boolean ownPidOnly) {
        List<String> cmd = new ArrayList<>(Arrays.asList("logcat", "-d", "-v", "threadtime"));
        if (ownPidOnly) {
            cmd.add("--pid=" + android.os.Process.myPid());
        } else {
            cmd.add("-t");
            cmd.add("5000");
        }
        try {
            Process p = new ProcessBuilder(cmd).redirectErrorStream(true).start();
            byte[] out;
            try (InputStream is = p.getInputStream()) {
                out = ReportFormat.readAll(is);
            }
            p.waitFor();
            return out;
        } catch (Exception e) {
            return ("logcat failed: " + e).getBytes(StandardCharsets.UTF_8);
        }
    }

    static long newestCrashTime(Context c) {
        return ReportFormat.newestCrashTime(new File(c.getFilesDir(), "crash.log"),
            new File(c.getFilesDir(), "crash-prev.log"), new File(c.getFilesDir(), "crash-selftest.log"));
    }

    static boolean shouldOfferCrashReport(Context c) {
        SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return ReportFormat.shouldOfferReport(newestCrashTime(c), p.getLong(PREF_LAST_OFFERED_CRASH, 0));
    }

    static void markCrashOffered(Context c) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putLong(PREF_LAST_OFFERED_CRASH, newestCrashTime(c)).apply();
    }

    static String lastSmokeValue(Context c, String key) {
        File f = new File(c.getFilesDir(), NATIVE_SMOKE);
        if (!f.isFile()) return null;
        try {
            for (String line : new String(readFile(f), StandardCharsets.UTF_8).split("\n")) {
                if (line.startsWith(key + ": ")) return line.substring(key.length() + 2);
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    private static String freeSpace(File dir) {
        if (dir == null) return null;
        try {
            StatFs s = new StatFs(dir.getPath());
            return String.format(Locale.US, "%d MB free of %d MB",
                s.getAvailableBytes() >> 20, s.getTotalBytes() >> 20);
        } catch (Exception e) {
            return "n/a: " + e.getMessage();
        }
    }

    static byte[] readFile(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            return ReportFormat.readAll(in);
        }
    }
}
