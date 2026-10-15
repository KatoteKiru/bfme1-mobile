package com.generalsx.zerohour.importer;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Stream-copy SAF documents into {@code getExternalFilesDir(null)/GameData}.
 * Writes canonical manifest paths; validates traversal; checkpoints per file.
 */
public final class GameDataImporter {
    private static final String TAG = "BfmeImport";

    public interface Listener {
        void onProgress(ImportProgress progress);
    }

    private final Context appCtx;
    private final ContentResolver resolver;
    private final AtomicBoolean cancel = new AtomicBoolean(false);
    private final ProgressThrottle throttle = new ProgressThrottle();

    public GameDataImporter(Context ctx) {
        this.appCtx = ctx.getApplicationContext();
        this.resolver = appCtx.getContentResolver();
    }

    public void requestCancel() {
        cancel.set(true);
    }

    public boolean isCancelRequested() {
        return cancel.get();
    }

    public ImportProgress run(GameDataManifest manifest,
                              Uri treeUri,
                              Map<String, SafTreeEntry> treeByFolded,
                              String manifestSha256,
                              Listener listener) throws IOException {
        File gameData = GameDataPaths.gameDataDir(appCtx);
        if (gameData == null) {
            throw new IOException("externalFilesDir is null");
        }
        if (!gameData.exists() && !gameData.mkdirs()) {
            throw new IOException("cannot create GameData: " + gameData);
        }

        ImportState state = ImportStateStore.load(appCtx);
        long now = System.currentTimeMillis();
        if (state.startedAtMs == 0L) {
            state.startedAtMs = now;
        }
        state.status = ImportState.STATUS_IN_PROGRESS;
        state.profile = manifest.profile;
        state.manifestSha256 = manifestSha256;
        state.sourceTreeUri = treeUri != null ? treeUri.toString() : state.sourceTreeUri;
        state.lastError = null;
        ImportStateStore.save(appCtx, state);

        List<ManifestFileEntry> work = new ArrayList<>();
        long bytesDone = 0L;
        int filesDone = 0;
        for (ManifestFileEntry e : manifest.importableFiles()) {
            ImportState.FileRecord existing = state.get(e.path);
            File dest;
            try {
                dest = PathSafety.resolveUnderGameData(gameData, e.path);
            } catch (IOException | IllegalArgumentException ex) {
                fail(state, "unsafe path " + e.path + ": " + ex.getMessage(), listener);
                throw new IOException(ex.getMessage(), ex);
            }
            if (isResumeVerified(existing, e, dest)) {
                if (existing.mtimeMs != dest.lastModified()) {
                    // Re-hashed OK: refresh the stored mtime.
                    state.putVerified(e.path, e.size, e.sha256, dest.lastModified());
                    ImportStateStore.save(appCtx, state);
                }
                bytesDone += e.size;
                filesDone++;
                continue;
            }
            SafTreeEntry src = treeByFolded.get(PathSafety.foldKey(e.path));
            if (src == null) {
                if (e.required) {
                    fail(state, "missing required: " + e.path, listener);
                    return lastFailProgress(manifest, e.path);
                }
                continue;
            }
            if (src.size < 0) {
                String msg = "virtual document / unknown size: " + e.path;
                if (e.required) {
                    fail(state, msg, listener);
                    throw new IOException(msg);
                }
                state.optionalHashMismatches.add(e.path + " (unknown size)");
                continue;
            }
            work.add(e);
        }

        long bytesTotal = bytesDone;
        for (ManifestFileEntry e : work) {
            bytesTotal += e.size;
        }
        int filesTotal = filesDone + work.size();
        publishThrottled(listener, filesDone, filesTotal, bytesDone, bytesTotal, null,
                "Importing…", false, false, false, true);

        for (ManifestFileEntry e : work) {
            if (cancel.get()) {
                return cancelAt(state, filesDone, filesTotal, bytesDone, bytesTotal, e.path, listener);
            }

            SafTreeEntry src = treeByFolded.get(PathSafety.foldKey(e.path));
            File dest = PathSafety.resolveUnderGameData(gameData, e.path);
            File parent = dest.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs()) {
                fail(state, "mkdir failed: " + parent, listener);
                return lastFailProgress(manifest, e.path);
            }

            // Re-hash skip if on-disk already matches
            if (dest.isFile() && dest.length() == e.size) {
                String existingHash = hashFile(dest);
                if (e.sha256.equalsIgnoreCase(existingHash)) {
                    state.putVerified(e.path, e.size, e.sha256, dest.lastModified());
                    ImportStateStore.save(appCtx, state);
                    filesDone++;
                    bytesDone += e.size;
                    publishThrottled(listener, filesDone, filesTotal, bytesDone, bytesTotal, e.path,
                            "Skipped (hash match)", false, false, false, false);
                    continue;
                }
            }

            long freeNow;
            try {
                freeNow = GameDataPrecheck.freeBytesOrThrow(gameData);
            } catch (IllegalStateException se) {
                fail(state, "StatFs failed: " + se.getMessage(), listener);
                throw new IOException(se.getMessage(), se);
            }
            if (freeNow < e.size + (2L * 1024 * 1024)) {
                String msg = "Not enough free space for " + e.path
                        + " (need " + GameDataPrecheck.human(e.size)
                        + ", free " + GameDataPrecheck.human(freeNow) + ")";
                fail(state, msg, listener);
                throw new IOException(msg);
            }

            publishThrottled(listener, filesDone, filesTotal, bytesDone, bytesTotal, e.path,
                    "Copying " + e.path, false, false, false, true);

            File part = new File(dest.getAbsolutePath() + GameDataPaths.PART_SUFFIX);
            if (part.exists() && !part.delete()) {
                ImportLog.w(TAG, "could not clear stale part: " + part);
            }

            String actualHash;
            final int filesDoneSnap = filesDone;
            final long bytesDoneSnap = bytesDone;
            final int filesTotalSnap = filesTotal;
            final long bytesTotalSnap = bytesTotal;
            boolean fullyCopied = false;
            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(Uri.parse(src.documentUri), "r")) {
                if (pfd == null) {
                    throw new IOException("openFileDescriptor null for " + e.path);
                }
                try (InputStream in = new FileInputStream(pfd.getFileDescriptor());
                     FileOutputStream out = new FileOutputStream(part)) {
                    actualHash = Sha256Hasher.copyAndHash(in, out, cancel::get, fileBytes -> {
                        int pct = bytesTotalSnap <= 0 ? 0
                                : (int) Math.min(100, ((bytesDoneSnap + fileBytes) * 100) / bytesTotalSnap);
                        if (throttle.shouldEmit(pct)) {
                            publish(listener, filesDoneSnap, filesTotalSnap,
                                    bytesDoneSnap + fileBytes, bytesTotalSnap, e.path,
                                    "Copying " + e.path + " (" + GameDataPrecheck.human(fileBytes) + ")",
                                    false, false, false);
                        }
                    });
                    out.getFD().sync();
                    fullyCopied = true;
                }
            } catch (Sha256Hasher.CancelledException ce) {
                // If the last block finished hashing/writing, treat as full copy below.
                // CancelledException means mid-stream — drop partial .part.
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                return cancelAt(state, filesDone, filesTotal, bytesDone, bytesTotal, e.path, listener);
            } catch (SecurityException se) {
                state.status = ImportState.STATUS_FAILED;
                state.lastError = "нет доступа к файлу / no access: " + e.path;
                ImportStateStore.save(appCtx, state);
                throw se;
            } catch (IOException ioe) {
                state.status = ImportState.STATUS_FAILED;
                state.lastError = ioe.getMessage();
                ImportStateStore.save(appCtx, state);
                throw ioe;
            }

            if (cancel.get() && !fullyCopied) {
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                return cancelAt(state, filesDone, filesTotal, bytesDone, bytesTotal, e.path, listener);
            }

            // Cancel after full copy of this file: keep verified .part → rename (Lead #13)
            if (!e.sha256.equalsIgnoreCase(actualHash)) {
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                if (e.required) {
                    String msg = "SHA-256 mismatch for " + e.path;
                    fail(state, msg, listener);
                    throw new IOException(msg);
                }
                state.optionalHashMismatches.add(e.path);
                ImportStateStore.save(appCtx, state);
                ImportLog.w(TAG, "optional hash mismatch, skipped: " + e.path);
                publishThrottled(listener, filesDone, filesTotal, bytesDone, bytesTotal, e.path,
                        "Skipped optional (hash mismatch): " + e.path, false, false, false, true);
                continue;
            }
            if (part.length() != e.size) {
                //noinspection ResultOfMethodCallIgnored
                part.delete();
                String msg = "size mismatch after copy for " + e.path;
                if (e.required) {
                    fail(state, msg, listener);
                    throw new IOException(msg);
                }
                state.optionalHashMismatches.add(e.path + " (size)");
                ImportStateStore.save(appCtx, state);
                continue;
            }

            AtomicFiles.moveReplace(part, dest);
            state.putVerified(e.path, e.size, e.sha256, dest.lastModified());
            ImportStateStore.save(appCtx, state);
            filesDone++;
            bytesDone += e.size;
            ImportLog.i(TAG, "imported " + e.path + " (" + e.size + " bytes)");
            publishThrottled(listener, filesDone, filesTotal, bytesDone, bytesTotal, e.path,
                    "Verified " + e.path, false, false, false, false);

            if (cancel.get()) {
                // File fully verified — resume will skip it.
                return cancelAt(state, filesDone, filesTotal, bytesDone, bytesTotal, e.path, listener);
            }
        }

        // Success: write gamedata_path.txt to GameData absolute path, then complete.
        writeGamedataPathMarker(gameData);
        state.status = ImportState.STATUS_COMPLETE;
        state.completedAtMs = System.currentTimeMillis();
        state.lastError = null;
        ImportStateStore.save(appCtx, state);

        String doneMsg = "Done, version " + manifest.displayLabel() + ", "
                + filesDone + " files verified";
        if (!state.optionalHashMismatches.isEmpty()) {
            doneMsg += " (" + state.optionalHashMismatches.size() + " optional skipped)";
        }
        ImportLog.i(TAG, doneMsg + "; GameData=" + gameData.getAbsolutePath());
        ImportProgress done = new ImportProgress(filesDone, filesTotal, bytesDone, bytesTotal,
                null, doneMsg, true, true, false);
        if (listener != null) {
            listener.onProgress(done);
        }
        return done;
    }

    /**
     * BFME: resume skip check. Size + manifest sha + stored mtime must match;
     * if mtime differs, the destination is re-hashed (SHA-256) before skipping.
     */
    static boolean isResumeVerified(ImportState.FileRecord existing, ManifestFileEntry e, File dest)
            throws IOException {
        if (existing == null || !existing.verified
                || !e.sha256.equalsIgnoreCase(existing.sha256)
                || !dest.isFile() || dest.length() != e.size) {
            return false;
        }
        if (existing.mtimeMs == dest.lastModified()) {
            return true;
        }
        ImportLog.i(TAG, "mtime changed, re-hashing " + e.path);
        try (InputStream in = new FileInputStream(dest)) {
            return e.sha256.equalsIgnoreCase(Sha256Hasher.hashStream(in));
        }
    }

    private void writeGamedataPathMarker(File gameData) throws IOException {
        File marker = new File(appCtx.getFilesDir(), GameDataPaths.GAMEDATA_PATH_MARKER);
        File tmp = new File(marker.getAbsolutePath() + ".tmp");
        try (OutputStreamWriter w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            w.write(gameData.getAbsolutePath());
            w.write("\n");
        }
        AtomicFiles.moveReplace(tmp, marker);
        ImportLog.i(TAG, "wrote gamedata_path.txt → " + gameData.getAbsolutePath());
    }

    private ImportProgress cancelAt(ImportState state, int filesDone, int filesTotal,
                                    long bytesDone, long bytesTotal, String path,
                                    Listener listener) throws IOException {
        state.status = ImportState.STATUS_CANCELLED;
        ImportStateStore.save(appCtx, state);
        ImportProgress p = new ImportProgress(filesDone, filesTotal, bytesDone, bytesTotal,
                path, "Cancelled", true, false, true);
        if (listener != null) {
            listener.onProgress(p);
        }
        ImportLog.i(TAG, "import cancelled; verified=" + state.verifiedPaths().size());
        return p;
    }

    private void fail(ImportState state, String msg, Listener listener) throws IOException {
        state.status = ImportState.STATUS_FAILED;
        state.lastError = msg;
        ImportStateStore.save(appCtx, state);
        ImportLog.e(TAG, msg);
        if (listener != null) {
            listener.onProgress(new ImportProgress(0, 0, 0, 0, null, msg, true, false, false));
        }
    }

    private static ImportProgress lastFailProgress(GameDataManifest manifest, String path) {
        return new ImportProgress(0, manifest.importableFiles().size(), 0, manifest.totalBytes(),
                path, "Failed", true, false, false);
    }

    private void publishThrottled(Listener listener, int filesDone, int filesTotal,
                                  long bytesDone, long bytesTotal, String path,
                                  String msg, boolean finished, boolean success, boolean cancelled,
                                  boolean force) {
        int pct = bytesTotal <= 0 ? 0 : (int) Math.min(100, (bytesDone * 100) / bytesTotal);
        if (force || finished || throttle.shouldEmit(pct)) {
            publish(listener, filesDone, filesTotal, bytesDone, bytesTotal, path, msg,
                    finished, success, cancelled);
        }
    }

    private static void publish(Listener listener, int filesDone, int filesTotal,
                                long bytesDone, long bytesTotal, String path,
                                String msg, boolean finished, boolean success, boolean cancelled) {
        if (listener != null) {
            listener.onProgress(new ImportProgress(filesDone, filesTotal, bytesDone, bytesTotal,
                    path, msg, finished, success, cancelled));
        }
    }

    private static String hashFile(File f) throws IOException {
        try (FileInputStream in = new FileInputStream(f)) {
            return Sha256Hasher.hashStream(in);
        }
    }
}
