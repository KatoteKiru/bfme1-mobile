package com.generalsx.zerohour.importer;

/** One file row from bfme1-manifest.json (metadata only — no EA content). */
public final class ManifestFileEntry {
    /** Legacy / missing {@code kind}: treated as importable. */
    public static final String KIND_IMPORT = "import";
    public static final String KIND_BIG = "big";
    public static final String KIND_LOOSE = "loose";
    public static final String KIND_WIN_BINARY = "win-binary";

    public final String path;      // relative to GameData/, forward slashes (canonical from JSON)
    public final long size;
    public final String sha256;    // lowercase hex
    public final boolean required;
    /**
     * schemaVersion 1: {@code big} | {@code loose} | {@code win-binary}.
     * Absent ⇒ {@link #KIND_IMPORT} (still importable).
     * {@link #KIND_WIN_BINARY} is never copied.
     */
    public final String kind;

    public ManifestFileEntry(String path, long size, String sha256, boolean required, String kind) {
        this.path = path;
        this.size = size;
        this.sha256 = sha256 == null ? null : sha256.toLowerCase(java.util.Locale.ROOT);
        this.required = required;
        this.kind = (kind == null || kind.isEmpty()) ? KIND_IMPORT : kind;
    }

    public boolean shouldImport() {
        return !KIND_WIN_BINARY.equals(kind);
    }
}
