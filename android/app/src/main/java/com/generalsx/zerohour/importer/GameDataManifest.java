package com.generalsx.zerohour.importer;

import android.content.Context;
import android.content.res.AssetManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Parser for Formats' {@code docs/bfme1-manifest.json} (E1-24 / schemaVersion 1).
 *
 * <pre>
 * {
 *   "schemaVersion": 1,
 *   "defaultProfile": "&lt;key&gt;",
 *   "profiles": {
 *     "&lt;key&gt;": {
 *       "game": "bfme1",
 *       "displayVersion": "Patch 2.22v7.0.5",
 *       "files": [{path, size, sha256, required, kind}]
 *     }
 *   }
 * }
 * </pre>
 *
 * Profile selection: explicit {@code profileKey}, else root {@code defaultProfile},
 * else the sole profile key. Never hard-code a patch id.
 */
public final class GameDataManifest {
    /** Highest schemaVersion this APK understands (Formats PR #33). */
    public static final int SUPPORTED_SCHEMA_VERSION = 1;

    public final int schemaVersion;
    /** Top-level or profile {@code game} (e.g. {@code bfme1}); may be null. */
    public final String game;
    /** Root {@code defaultProfile} key; may be null on legacy fixtures. */
    public final String defaultProfile;
    /** Selected profile key. */
    public final String profile;
    /** Profile {@code displayVersion} for the import UI; may be null. */
    public final String displayVersion;
    public final List<ManifestFileEntry> files;
    private final Map<String, ManifestFileEntry> byPath;
    private final Map<String, ManifestFileEntry> byFoldedPath;

    public GameDataManifest(String profile, List<ManifestFileEntry> files) {
        this(0, null, null, profile, null, files);
    }

    public GameDataManifest(int schemaVersion, String game, String defaultProfile,
                            String profile, String displayVersion,
                            List<ManifestFileEntry> files) {
        this.schemaVersion = schemaVersion;
        this.game = game;
        this.defaultProfile = defaultProfile;
        this.profile = profile;
        this.displayVersion = displayVersion;
        this.files = Collections.unmodifiableList(new ArrayList<>(files));
        Map<String, ManifestFileEntry> map = new LinkedHashMap<>();
        Map<String, ManifestFileEntry> folded = new LinkedHashMap<>();
        for (ManifestFileEntry e : files) {
            map.put(e.path, e);
            String key = PathSafety.foldKey(e.path);
            ManifestFileEntry prev = folded.put(key, e);
            if (prev != null && !prev.path.equals(e.path)) {
                throw new IllegalArgumentException(
                        "case-only collision in manifest: " + prev.path + " vs " + e.path);
            }
        }
        this.byPath = Collections.unmodifiableMap(map);
        this.byFoldedPath = Collections.unmodifiableMap(folded);
    }

    /** Label for import screen: {@code displayVersion} if set, else profile key. */
    public String displayLabel() {
        if (displayVersion != null && !displayVersion.isEmpty()) {
            return displayVersion;
        }
        return profile;
    }

    public ManifestFileEntry get(String relativePath) {
        if (relativePath == null) {
            return null;
        }
        ManifestFileEntry exact = byPath.get(GameDataManifest.normalizePath(relativePath));
        if (exact != null) {
            return exact;
        }
        try {
            return byFoldedPath.get(PathSafety.foldKey(relativePath));
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    public ManifestFileEntry getByFolded(String foldedKey) {
        return byFoldedPath.get(foldedKey);
    }

    public long totalBytes() {
        long sum = 0;
        for (ManifestFileEntry e : files) {
            if (e.shouldImport()) {
                sum += e.size;
            }
        }
        return sum;
    }

    public long requiredBytes() {
        long sum = 0;
        for (ManifestFileEntry e : files) {
            if (e.required && e.shouldImport()) {
                sum += e.size;
            }
        }
        return sum;
    }

    public List<ManifestFileEntry> requiredFiles() {
        List<ManifestFileEntry> out = new ArrayList<>();
        for (ManifestFileEntry e : files) {
            if (e.required && e.shouldImport()) {
                out.add(e);
            }
        }
        return out;
    }

    public List<ManifestFileEntry> importableFiles() {
        List<ManifestFileEntry> out = new ArrayList<>();
        for (ManifestFileEntry e : files) {
            if (e.shouldImport()) {
                out.add(e);
            }
        }
        return out;
    }

    public static String normalizePath(String path) {
        return PathSafety.normalizeSeparators(path);
    }

    public static GameDataManifest loadFromAssets(Context ctx) throws IOException, JSONException {
        return loadFromAssets(ctx.getAssets(), GameDataPaths.MANIFEST_ASSET);
    }

    public static GameDataManifest loadFromAssets(AssetManager assets, String assetPath)
            throws IOException, JSONException {
        try (InputStream in = assets.open(assetPath)) {
            return parse(readUtf8(in), null);
        }
    }

    public static GameDataManifest parse(String json) throws JSONException {
        return parse(json, null);
    }

    /**
     * @param profileKey explicit profile key, or {@code null} to resolve via
     *                   {@code defaultProfile} / sole profile
     */
    public static GameDataManifest parse(String json, String profileKey) throws JSONException {
        JSONObject root = new JSONObject(json);
        if (!root.has("profiles")) {
            throw new JSONException("bfme1-manifest.json must have top-level \"profiles\" (E1-24)");
        }
        int schemaVersion = root.optInt("schemaVersion", 0);
        if (schemaVersion < 1) {
            throw new JSONException(
                    "bfme1-manifest.json has no valid schemaVersion (got " + schemaVersion
                            + "); expected 1..." + SUPPORTED_SCHEMA_VERSION);
        }
        if (schemaVersion > SUPPORTED_SCHEMA_VERSION) {
            throw new JSONException(
                    "bfme1-manifest.json schemaVersion " + schemaVersion
                            + " is newer than this app supports ("
                            + SUPPORTED_SCHEMA_VERSION
                            + "). Please update the application.");
        }
        String rootGame = root.has("game") && !root.isNull("game")
                ? root.getString("game") : null;
        String defaultProfile = root.has("defaultProfile") && !root.isNull("defaultProfile")
                ? root.getString("defaultProfile") : null;
        JSONObject profiles = root.getJSONObject("profiles");
        String key = resolveProfileKey(profiles, defaultProfile, profileKey);
        JSONObject profileObj = profiles.getJSONObject(key);
        String game = profileObj.has("game") && !profileObj.isNull("game")
                ? profileObj.getString("game")
                : rootGame;
        String displayVersion = profileObj.has("displayVersion") && !profileObj.isNull("displayVersion")
                ? profileObj.getString("displayVersion") : null;
        return new GameDataManifest(schemaVersion, game, defaultProfile, key, displayVersion,
                parseFiles(profileObj.getJSONArray("files")));
    }

    static String resolveProfileKey(JSONObject profiles, String defaultProfile, String requested)
            throws JSONException {
        List<String> keys = new ArrayList<>();
        for (Iterator<String> it = profiles.keys(); it.hasNext(); ) {
            keys.add(it.next());
        }
        if (keys.isEmpty()) {
            throw new JSONException("profiles object is empty");
        }
        if (requested != null && !requested.isEmpty()) {
            if (!profiles.has(requested)) {
                throw new JSONException("profile not found: " + requested);
            }
            return requested;
        }
        if (defaultProfile != null && !defaultProfile.isEmpty()) {
            if (!profiles.has(defaultProfile)) {
                throw new JSONException("defaultProfile not found in profiles: " + defaultProfile);
            }
            return defaultProfile;
        }
        if (keys.size() == 1) {
            return keys.get(0);
        }
        throw new JSONException(
                "multiple profiles and no defaultProfile; pass an explicit profile key: " + keys);
    }

    private static List<ManifestFileEntry> parseFiles(JSONArray arr) throws JSONException {
        List<ManifestFileEntry> list = new ArrayList<>(arr.length());
        for (int i = 0; i < arr.length(); i++) {
            JSONObject o = arr.getJSONObject(i);
            String path = PathSafety.requireSafeRelative(o.getString("path"));
            long size = o.getLong("size");
            String sha = o.getString("sha256").toLowerCase(Locale.ROOT);
            boolean required = o.optBoolean("required", false);
            String kind = o.has("kind") ? o.getString("kind") : null;
            if (kind != null && !kind.isEmpty() && !isKnownKind(kind)) {
                ImportLog.w("BfmeImport",
                        "unknown manifest kind \"" + kind + "\" for " + path
                                + " — treating as importable");
            }
            list.add(new ManifestFileEntry(path, size, sha, required, kind));
        }
        return list;
    }

    private static boolean isKnownKind(String kind) {
        return ManifestFileEntry.KIND_BIG.equals(kind)
                || ManifestFileEntry.KIND_LOOSE.equals(kind)
                || ManifestFileEntry.KIND_WIN_BINARY.equals(kind)
                || ManifestFileEntry.KIND_IMPORT.equals(kind);
    }

    public static String readUtf8(InputStream in) throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        byte[] tmp = new byte[8192];
        int n;
        while ((n = in.read(tmp)) >= 0) {
            buf.write(tmp, 0, n);
        }
        return new String(buf.toByteArray(), StandardCharsets.UTF_8);
    }

    /** SHA-256 of raw manifest bytes (for import-state binding). */
    public static String sha256OfBytes(byte[] raw) {
        java.security.MessageDigest md = Sha256Hasher.newDigest();
        md.update(raw);
        return Sha256Hasher.toHex(md.digest());
    }

    public static byte[] readAssetBytes(AssetManager assets, String assetPath) throws IOException {
        try (InputStream in = assets.open(assetPath)) {
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] tmp = new byte[8192];
            int n;
            while ((n = in.read(tmp)) >= 0) {
                buf.write(tmp, 0, n);
            }
            return buf.toByteArray();
        }
    }
}
