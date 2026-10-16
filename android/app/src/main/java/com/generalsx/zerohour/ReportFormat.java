package com.generalsx.zerohour;

// BFME: E1-15. Pure-Java helpers for the crash/log report (no android.* imports, so the JVM unit
// tests in src/test exercise them directly): report file name, entry allowlist, zip writer,
// startup-metadata formatting and the "previous run crashed" check.

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

final class ReportFormat {
    private ReportFormat() {}

    /** Line the native handler writes for every fatal signal (AndroidCrashHandler.cpp). */
    static final String NATIVE_CRASH_MARKER = "=== NATIVE CRASH ===";

    /** Hard cap per file so a runaway log cannot produce an unsendable zip. */
    static final long MAX_ENTRY_BYTES = 64L * 1024 * 1024;

    // Only our own diagnostics. Game files (EA archives, maps, INI, art, audio) never match.
    private static final Pattern ALLOWED = Pattern.compile(
        "(crash|crash-prev|crash-selftest|generals-stderr|generals-stderr-prev|startup-meta|startup-meta-prev"
            + "|native-smoke|logcat-pid|logcat-app|report-index)\\.(log|txt)"
            + "|import-state\\.json|import[\\w.-]*\\.(log|txt)");

    static boolean isAllowedEntry(String name) {
        return name != null && ALLOWED.matcher(name).matches();
    }

    static String sanitize(String s) {
        if (s == null || s.isEmpty()) return "unknown";
        return s.replaceAll("[^A-Za-z0-9._-]", "-");
    }

    /** bfme-report_&lt;version&gt;_&lt;reason&gt;_&lt;yyyyMMdd-HHmmss&gt;_&lt;session&gt;.zip */
    static String reportFileName(String version, String reason, long timeMs, String session) {
        SimpleDateFormat f = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US);
        f.setTimeZone(TimeZone.getDefault());
        return "bfme-report_" + sanitize(version) + "_" + sanitize(reason) + "_"
            + f.format(new Date(timeMs)) + "_" + sanitize(session) + ".zip";
    }

    /** "key: value" lines in insertion order; null values become "n/a". */
    static String formatMetadata(Map<String, String> fields) {
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, String> e : fields.entrySet()) {
            String v = e.getValue();
            sb.append(e.getKey()).append(": ")
              .append(v == null || v.isEmpty() ? "n/a" : v.replace('\n', ' ')).append('\n');
        }
        return sb.toString();
    }

    /** Git sha from versionName "0.1.0-62ba2d1" (last dash-separated part), or null. */
    static String gitShaFromVersionName(String versionName) {
        if (versionName == null) return null;
        int i = versionName.lastIndexOf('-');
        if (i < 0 || i == versionName.length() - 1) return null;
        String sha = versionName.substring(i + 1);
        return sha.matches("[0-9a-f]{7,40}") ? sha : null;
    }

    static final class Entry {
        final String name;
        final File file;
        final byte[] bytes;
        Entry(String name, File file) { this.name = name; this.file = file; this.bytes = null; }
        Entry(String name, byte[] bytes) { this.name = name; this.file = null; this.bytes = bytes; }
    }

    /**
     * Writes the allowed, existing entries plus report-index.txt (what went in and what was
     * skipped and why). Returns the number of data entries written (index not counted).
     */
    static int writeZip(OutputStream out, List<Entry> entries) throws IOException {
        List<String> index = new ArrayList<>();
        int written = 0;
        try (ZipOutputStream zos = new ZipOutputStream(out)) {
            byte[] buf = new byte[64 * 1024];
            for (Entry e : entries) {
                if (!isAllowedEntry(e.name)) { index.add("skipped (not allowed): " + e.name); continue; }
                if (e.bytes != null) {
                    zos.putNextEntry(new ZipEntry(e.name));
                    zos.write(e.bytes);
                    zos.closeEntry();
                    index.add("included: " + e.name + " (" + e.bytes.length + " bytes)");
                    written++;
                    continue;
                }
                if (e.file == null || !e.file.isFile()) { index.add("absent: " + e.name); continue; }
                long len = e.file.length();
                zos.putNextEntry(new ZipEntry(e.name));
                long copied = 0;
                try (InputStream in = new FileInputStream(e.file)) {
                    int n;
                    while (copied < MAX_ENTRY_BYTES && (n = in.read(buf, 0,
                            (int) Math.min(buf.length, MAX_ENTRY_BYTES - copied))) > 0) {
                        zos.write(buf, 0, n);
                        copied += n;
                    }
                }
                zos.closeEntry();
                index.add("included: " + e.name + " (" + copied + (copied < len ? " of " + len + ", truncated" : "") + " bytes)");
                written++;
            }
            zos.putNextEntry(new ZipEntry("report-index.txt"));
            StringBuilder sb = new StringBuilder();
            for (String line : index) sb.append(line).append('\n');
            zos.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            zos.closeEntry();
        }
        return written;
    }

    /** True when the file holds a native crash record. Reads at most the last 4 MB. */
    static boolean containsCrashMarker(File f) {
        if (f == null || !f.isFile()) return false;
        try (InputStream in = new FileInputStream(f)) {
            long skip = Math.max(0, f.length() - 4L * 1024 * 1024);
            while (skip > 0) { long s = in.skip(skip); if (s <= 0) break; skip -= s; }
            byte[] data = readAll(in);
            return new String(data, StandardCharsets.ISO_8859_1).contains(NATIVE_CRASH_MARKER);
        } catch (IOException e) {
            return false;
        }
    }

    /** Newest mtime among files that hold a crash record, or 0. */
    static long newestCrashTime(File... files) {
        long best = 0;
        for (File f : files) {
            if (containsCrashMarker(f)) best = Math.max(best, f.lastModified());
        }
        return best;
    }

    static boolean shouldOfferReport(long crashTimeMs, long lastOfferedCrashTimeMs) {
        return crashTimeMs > 0 && crashTimeMs != lastOfferedCrashTimeMs;
    }

    static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[16 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
        return bos.toByteArray();
    }
}
