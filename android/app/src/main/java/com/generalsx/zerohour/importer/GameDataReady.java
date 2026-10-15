package com.generalsx.zerohour.importer;

import android.content.Context;

import java.io.File;

/**
 * Readiness: {@code import-state.json} status=complete, manifest SHA matches the
 * embedded asset, and every <em>required</em> file of the current manifest is
 * verified with matching size on disk.
 */
public final class GameDataReady {
    private static final String TAG = "BfmeImport";

    private GameDataReady() {}

    public static boolean isReady(Context ctx) {
        if (ctx == null) {
            return false;
        }
        File stateFile = GameDataPaths.importStateFile(ctx);
        File gameData = GameDataPaths.gameDataDir(ctx);
        if (!isReadyStructure(stateFile, gameData)) {
            return false;
        }
        try {
            byte[] raw = GameDataManifest.readAssetBytes(ctx.getAssets(), GameDataPaths.MANIFEST_ASSET);
            String sha = GameDataManifest.sha256OfBytes(raw);
            ImportState state = ImportStateStore.load(stateFile);
            if (state.manifestSha256 == null || !state.manifestSha256.equalsIgnoreCase(sha)) {
                ImportLog.w(TAG, "manifest sha mismatch or missing in import-state");
                return false;
            }
            GameDataManifest manifest = GameDataManifest.parse(
                    GameDataManifest.readUtf8(new java.io.ByteArrayInputStream(raw)));
            return allRequiredVerified(state, manifest, gameData);
        } catch (Exception e) {
            ImportLog.w(TAG, "ready check failed: " + e.getMessage());
            return false;
        }
    }

    /** File-only structural check (no asset access) — used sparingly in unit tests. */
    public static boolean isReadyStructure(File stateFile, File gameDataDir) {
        if (gameDataDir == null || !gameDataDir.isDirectory()) {
            return false;
        }
        if (!GameDataPaths.GAME_DATA_DIR_NAME.equals(gameDataDir.getName())) {
            return false;
        }
        ImportState state = ImportStateStore.load(stateFile);
        return state.isComplete() && !state.files.isEmpty();
    }

    public static boolean isReady(File gameDataDir) {
        // Without Context we cannot hash assets — structure-only (tests / legacy).
        return isReadyStructure(GameDataPaths.importStateFileBeside(gameDataDir), gameDataDir);
    }

    static boolean allRequiredVerified(ImportState state, GameDataManifest manifest, File gameData) {
        for (ManifestFileEntry e : manifest.requiredFiles()) {
            ImportState.FileRecord r = state.get(e.path);
            if (r == null || !r.verified) {
                ImportLog.w(TAG, "required not verified: " + e.path);
                return false;
            }
            if (!e.sha256.equalsIgnoreCase(r.sha256) || r.size != e.size) {
                ImportLog.w(TAG, "required record drift: " + e.path);
                return false;
            }
            File f = new File(gameData, e.path.replace('/', File.separatorChar));
            if (!f.isFile() || f.length() != e.size) {
                ImportLog.w(TAG, "required on-disk drift: " + e.path);
                return false;
            }
        }
        return true;
    }

    public static String gameDataAbsolutePath(Context ctx) {
        if (!isReady(ctx)) {
            return null;
        }
        File dir = GameDataPaths.gameDataDir(ctx);
        return dir == null ? null : dir.getAbsolutePath();
    }
}
