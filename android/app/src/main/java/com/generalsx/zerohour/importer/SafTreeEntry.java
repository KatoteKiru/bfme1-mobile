package com.generalsx.zerohour.importer;

/** One document discovered under an OPEN_DOCUMENT_TREE grant. */
public final class SafTreeEntry {
    public final String relativePath; // forward slashes, relative to tree root
    public final String displayName;
    /** content:// document URI as string (parsed when opening). */
    public final String documentUri;
    public final long size;           // -1 if unknown
    public final boolean isDirectory;

    public SafTreeEntry(String relativePath, String displayName, String documentUri,
                        long size, boolean isDirectory) {
        this.relativePath = relativePath;
        this.displayName = displayName;
        this.documentUri = documentUri;
        this.size = size;
        this.isDirectory = isDirectory;
    }
}
