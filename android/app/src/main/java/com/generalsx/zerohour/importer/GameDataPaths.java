package com.generalsx.zerohour.importer;

import android.content.Context;

import java.io.File;

/**
 * BFME1 Mobile E1-12a: on-disk layout under getExternalFilesDir(null).
 *
 * <pre>
 *   &lt;externalFilesDir&gt;/
 *     GameData/           ← engine cwd (SDL3Main chdir fallback)
 *     import-state.json   ← readiness gate (status: complete)
 * </pre>
 *
 * Native also consults {@code &lt;filesDir&gt;/gamedata_path.txt}; after a successful
 * import we delete that marker so the engine uses GameData above.
 */
public final class GameDataPaths {
    public static final String GAME_DATA_DIR_NAME = "GameData";
    public static final String IMPORT_STATE_FILE_NAME = "import-state.json";
    public static final String PART_SUFFIX = ".part";
    public static final String MANIFEST_ASSET = "bfme/bfme1-manifest.json";
    public static final String GAMEDATA_PATH_MARKER = "gamedata_path.txt";

    private GameDataPaths() {}

    public static File externalRoot(Context ctx) {
        return ctx.getExternalFilesDir(null);
    }

    public static File gameDataDir(Context ctx) {
        File root = externalRoot(ctx);
        return root == null ? null : new File(root, GAME_DATA_DIR_NAME);
    }

    public static File importStateFile(Context ctx) {
        File root = externalRoot(ctx);
        return root == null ? null : new File(root, IMPORT_STATE_FILE_NAME);
    }

    /** Sibling of GameData when only a File path is known (gate without Context). */
    public static File importStateFileBeside(File gameDataDir) {
        if (gameDataDir == null) {
            return null;
        }
        File parent = gameDataDir.getParentFile();
        return parent == null ? null : new File(parent, IMPORT_STATE_FILE_NAME);
    }
}
