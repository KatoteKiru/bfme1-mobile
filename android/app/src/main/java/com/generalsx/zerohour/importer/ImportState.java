package com.generalsx.zerohour.importer;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class ImportState {
    public static final String STATUS_IDLE = "idle";
    public static final String STATUS_IN_PROGRESS = "in_progress";
    public static final String STATUS_COMPLETE = "complete";
    public static final String STATUS_CANCELLED = "cancelled";
    public static final String STATUS_FAILED = "failed";

    public String status = STATUS_IDLE;
    public String profile;
    /** SHA-256 of the embedded bfme1-manifest.json bytes at import time. */
    public String manifestSha256;
    public String sourceTreeUri;
    public long startedAtMs;
    public long updatedAtMs;
    public long completedAtMs;
    public String lastError;
    /** Optional files that failed hash (skipped, import continues). */
    public final List<String> optionalHashMismatches = new ArrayList<>();
    public final Map<String, FileRecord> files = new LinkedHashMap<>();

    public static final class FileRecord {
        public final String path;
        public final long size;
        public final String sha256;
        public final long mtimeMs;
        public final boolean verified;

        public FileRecord(String path, long size, String sha256, long mtimeMs, boolean verified) {
            this.path = path;
            this.size = size;
            this.sha256 = sha256 == null ? null : sha256.toLowerCase(Locale.ROOT);
            this.mtimeMs = mtimeMs;
            this.verified = verified;
        }

        JSONObject toJson() throws JSONException {
            JSONObject o = new JSONObject();
            o.put("path", path);
            o.put("size", size);
            o.put("sha256", sha256);
            o.put("mtimeMs", mtimeMs);
            o.put("verified", verified);
            return o;
        }

        static FileRecord fromJson(JSONObject o) throws JSONException {
            return new FileRecord(
                    o.getString("path"),
                    o.getLong("size"),
                    o.optString("sha256", null),
                    o.optLong("mtimeMs", 0L),
                    o.optBoolean("verified", false));
        }
    }

    public boolean isComplete() {
        return STATUS_COMPLETE.equals(status);
    }

    public void putVerified(String path, long size, String sha256, long mtimeMs) {
        String canon = GameDataManifest.normalizePath(path);
        files.put(canon, new FileRecord(canon, size, sha256, mtimeMs, true));
    }

    public FileRecord get(String path) {
        return files.get(GameDataManifest.normalizePath(path));
    }

    public List<String> verifiedPaths() {
        List<String> out = new ArrayList<>();
        for (FileRecord r : files.values()) {
            if (r.verified) {
                out.add(r.path);
            }
        }
        return Collections.unmodifiableList(out);
    }

    public String toJson() throws JSONException {
        JSONObject root = new JSONObject();
        root.put("status", status);
        root.put("profile", profile == null ? JSONObject.NULL : profile);
        root.put("manifestSha256", manifestSha256 == null ? JSONObject.NULL : manifestSha256);
        root.put("sourceTreeUri", sourceTreeUri == null ? JSONObject.NULL : sourceTreeUri);
        root.put("startedAtMs", startedAtMs);
        root.put("updatedAtMs", updatedAtMs);
        root.put("completedAtMs", completedAtMs);
        root.put("lastError", lastError == null ? JSONObject.NULL : lastError);
        JSONArray mismatches = new JSONArray();
        for (String m : optionalHashMismatches) {
            mismatches.put(m);
        }
        root.put("optionalHashMismatches", mismatches);
        JSONArray arr = new JSONArray();
        for (FileRecord r : files.values()) {
            arr.put(r.toJson());
        }
        root.put("files", arr);
        return root.toString(2);
    }

    public static ImportState fromJson(String json) throws JSONException {
        JSONObject root = new JSONObject(json);
        ImportState s = new ImportState();
        s.status = root.optString("status", STATUS_IDLE);
        s.profile = root.isNull("profile") ? null : root.optString("profile", null);
        s.manifestSha256 = root.isNull("manifestSha256") ? null : root.optString("manifestSha256", null);
        s.sourceTreeUri = root.isNull("sourceTreeUri") ? null : root.optString("sourceTreeUri", null);
        s.startedAtMs = root.optLong("startedAtMs", 0L);
        s.updatedAtMs = root.optLong("updatedAtMs", 0L);
        s.completedAtMs = root.optLong("completedAtMs", 0L);
        s.lastError = root.isNull("lastError") ? null : root.optString("lastError", null);
        JSONArray mismatches = root.optJSONArray("optionalHashMismatches");
        if (mismatches != null) {
            for (int i = 0; i < mismatches.length(); i++) {
                s.optionalHashMismatches.add(mismatches.getString(i));
            }
        }
        JSONArray arr = root.optJSONArray("files");
        if (arr != null) {
            for (int i = 0; i < arr.length(); i++) {
                FileRecord r = FileRecord.fromJson(arr.getJSONObject(i));
                s.files.put(r.path, r);
            }
        }
        return s;
    }
}
