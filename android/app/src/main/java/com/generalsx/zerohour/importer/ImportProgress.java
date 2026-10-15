package com.generalsx.zerohour.importer;

/** Snapshot of import progress for UI / notification. */
public final class ImportProgress {
    public final int filesDone;
    public final int filesTotal;
    public final long bytesDone;
    public final long bytesTotal;
    public final String currentPath;
    public final String message;
    public final boolean finished;
    public final boolean success;
    public final boolean cancelled;

    public ImportProgress(int filesDone, int filesTotal, long bytesDone, long bytesTotal,
                          String currentPath, String message,
                          boolean finished, boolean success, boolean cancelled) {
        this.filesDone = filesDone;
        this.filesTotal = filesTotal;
        this.bytesDone = bytesDone;
        this.bytesTotal = bytesTotal;
        this.currentPath = currentPath;
        this.message = message;
        this.finished = finished;
        this.success = success;
        this.cancelled = cancelled;
    }

    public int percent() {
        if (bytesTotal <= 0) {
            return 0;
        }
        long p = (bytesDone * 100L) / bytesTotal;
        return (int) Math.max(0, Math.min(100, p));
    }
}
