package com.generalsx.zerohour.importer;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

/**
 * Relative-path validation and case-folding for SAF → GameData copies.
 * Rejects traversal and writes only under the canonical GameData root.
 */
public final class PathSafety {
    private PathSafety() {}

    /** Normalize separators; does not validate. */
    public static String normalizeSeparators(String path) {
        if (path == null) {
            return "";
        }
        return path.replace('\\', '/');
    }

    /**
     * Validate a manifest-relative path. Throws {@link IllegalArgumentException}
     * on {@code ..}, absolute form, empty segments, {@code :}, NUL, etc.
     */
    public static String requireSafeRelative(String path) {
        if (path == null) {
            throw new IllegalArgumentException("empty path");
        }
        // Reject backslash / NUL on the raw input before separator normalize
        // (normalizeSeparators would otherwise turn "a\b" into "a/b").
        if (path.indexOf('\\') >= 0) {
            throw new IllegalArgumentException("backslash in path: " + path);
        }
        if (path.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("NUL in path: " + path);
        }
        String p = normalizeSeparators(path);
        if (p.isEmpty()) {
            throw new IllegalArgumentException("empty path");
        }
        if (p.charAt(0) == '/') {
            throw new IllegalArgumentException("absolute path: " + path);
        }
        if (p.indexOf(':') >= 0) {
            throw new IllegalArgumentException("colon in path: " + path);
        }
        String[] parts = p.split("/", -1);
        for (String seg : parts) {
            if (seg.isEmpty()) {
                throw new IllegalArgumentException("empty segment in: " + path);
            }
            if (".".equals(seg) || "..".equals(seg)) {
                throw new IllegalArgumentException("dot segment in: " + path);
            }
        }
        return p;
    }

    public static String foldKey(String path) {
        return requireSafeRelative(path).toLowerCase(Locale.ROOT);
    }

    /**
     * Resolve {@code relative} under {@code gameDataRoot} and ensure the
     * canonical result stays inside GameData.
     */
    public static File resolveUnderGameData(File gameDataRoot, String relative) throws IOException {
        String safe = requireSafeRelative(relative);
        File rootCanon = gameDataRoot.getCanonicalFile();
        File dest = new File(rootCanon, safe.replace('/', File.separatorChar));
        File destCanon = dest.getCanonicalFile();
        String rootPath = rootCanon.getPath();
        if (!rootPath.endsWith(File.separator)) {
            rootPath = rootPath + File.separator;
        }
        String destPath = destCanon.getPath();
        if (!destPath.equals(rootCanon.getPath()) && !destPath.startsWith(rootPath)) {
            throw new IOException("path escapes GameData: " + relative);
        }
        return destCanon;
    }
}
