package com.generalsx.zerohour.importer;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Hash files already present in GameData (adb/MTP path) against the manifest
 * and write import-state without SAF copy.
 */
public final class GameDataVerifier {
    private static final String TAG = "BfmeImport";

    private GameDataVerifier() {}

    public interface Listener {
        void onProgress(ImportProgress progress);
    }

    public static ImportProgress verifyInPlace(Context ctx, Listener listener) throws IOException, org.json.JSONException {
        Context app = ctx.getApplicationContext();
        File gameData = GameDataPaths.gameDataDir(app);
        if (gameData == null || !gameData.isDirectory()) {
            throw new IOException("GameData missing");
        }
        byte[] raw = GameDataManifest.readAssetBytes(app.getAssets(), GameDataPaths.MANIFEST_ASSET);
        String manifestSha = GameDataManifest.sha256OfBytes(raw);
        GameDataManifest manifest = GameDataManifest.parse(
                new String(raw, StandardCharsets.UTF_8));

        ImportState state = ImportStateStore.load(app);
        state.status = ImportState.STATUS_IN_PROGRESS;
        state.profile = manifest.profile;
        state.manifestSha256 = manifestSha;
        state.lastError = null;
        ImportStateStore.save(app, state);

        int total = manifest.importableFiles().size();
        // BFME: E1-43 files put there with `adb push` belong to the shell user (drwxrws---), so
        // the app cannot open the folder and every file looked "missing". Say what is wrong.
        if (!gameData.canRead() || !gameData.canExecute()) {
            state.status = ImportState.STATUS_FAILED;
            state.lastError = "GameData is not readable by the app (copied with adb?). On the PC run: "
                    + "adb shell chmod -R a+rwX " + gameData.getAbsolutePath();
            ImportStateStore.save(app, state);
            ImportProgress fail = new ImportProgress(0, total, 0, manifest.totalBytes(),
                    "", state.lastError, true, false, false);
            if (listener != null) listener.onProgress(fail);
            return fail;
        }
        int done = 0;
        long bytesDone = 0;
        long bytesTotal = manifest.totalBytes();
        for (ManifestFileEntry e : manifest.importableFiles()) {
            File f = PathSafety.resolveUnderGameData(gameData, e.path);
            if (!f.isFile()) {
                if (e.required) {
                    state.status = ImportState.STATUS_FAILED;
                    state.lastError = "missing " + e.path;
                    ImportStateStore.save(app, state);
                    ImportProgress fail = new ImportProgress(done, total, bytesDone, bytesTotal,
                            e.path, state.lastError, true, false, false);
                    if (listener != null) listener.onProgress(fail);
                    return fail;
                }
                continue;
            }
            if (f.length() != e.size) {
                if (e.required) {
                    state.status = ImportState.STATUS_FAILED;
                    state.lastError = "size mismatch " + e.path;
                    ImportStateStore.save(app, state);
                    throw new IOException(state.lastError);
                }
                state.optionalHashMismatches.add(e.path);
                continue;
            }
            String hash;
            try (FileInputStream in = new FileInputStream(f)) {
                hash = Sha256Hasher.hashStream(in);
            }
            if (!e.sha256.equalsIgnoreCase(hash)) {
                if (e.required) {
                    state.status = ImportState.STATUS_FAILED;
                    state.lastError = "hash mismatch " + e.path;
                    ImportStateStore.save(app, state);
                    throw new IOException(state.lastError);
                }
                state.optionalHashMismatches.add(e.path);
                continue;
            }
            state.putVerified(e.path, e.size, e.sha256, f.lastModified());
            ImportStateStore.save(app, state);
            done++;
            bytesDone += e.size;
            if (listener != null) {
                listener.onProgress(new ImportProgress(done, total, bytesDone, bytesTotal,
                        e.path, "Verified " + e.path, false, false, false));
            }
        }

        // Marker + complete
        GameDataLocation.publish(app, gameData);

        state.status = ImportState.STATUS_COMPLETE;
        state.completedAtMs = System.currentTimeMillis();
        ImportStateStore.save(app, state);
        ImportProgress ok = new ImportProgress(done, total, bytesDone, bytesTotal,
                null, "Verified in place: " + done + " files", true, true, false);
        if (listener != null) listener.onProgress(ok);
        ImportLog.i(TAG, ok.message);
        return ok;
    }
}
