package com.generalsx.zerohour.importer;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * BFME: E1-12c — single source of truth for "where is the game data".
 *
 * <p>Truth = {@code <filesDir>/gamedata_path.txt} (the file the native engine reads),
 * gated by {@link GameDataReady} (import-state complete). Java (Setup screen, launch,
 * importer) and native (SDL3Main / BFME_GAMEDATA) must all go through here; nothing
 * else writes the marker and SharedPreferences {@code game_path} is not consulted for BFME.
 */
public final class GameDataLocation {
    private static final String TAG = "BfmeImport";

    private GameDataLocation() {}

    /**
     * Absolute GameData path if the import is complete, else {@code null}.
     * Self-heals the marker if it is missing or points elsewhere.
     */
    public static String resolve(Context ctx) {
        if (ctx == null) {
            return null;
        }
        boolean ready = GameDataReady.isReady(ctx);
        return resolve(ctx.getFilesDir(), GameDataPaths.gameDataDir(ctx), ready);
    }

    /** Pure core for unit tests. */
    static String resolve(File filesDir, File gameDataDir, boolean importComplete) {
        if (!importComplete || gameDataDir == null) {
            return null;
        }
        String want = gameDataDir.getAbsolutePath();
        if (!want.equals(readMarker(filesDir))) {
            try {
                publish(filesDir, gameDataDir);
            } catch (IOException e) {
                ImportLog.w(TAG, "could not heal gamedata_path.txt: " + e.getMessage());
            }
        }
        return want;
    }

    /** Write the marker atomically (import success / every launch). */
    public static void publish(Context ctx, File gameDataDir) throws IOException {
        publish(ctx.getFilesDir(), gameDataDir);
    }

    static void publish(File filesDir, File gameDataDir) throws IOException {
        File marker = new File(filesDir, GameDataPaths.GAMEDATA_PATH_MARKER);
        File tmp = new File(marker.getAbsolutePath() + ".tmp");
        try (OutputStreamWriter w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            w.write(gameDataDir.getAbsolutePath());
            w.write("\n");
        }
        AtomicFiles.moveReplace(tmp, marker);
        ImportLog.i(TAG, "wrote gamedata_path.txt → " + gameDataDir.getAbsolutePath());
    }

    /** Trimmed first line of the marker, or {@code null}. */
    static String readMarker(File filesDir) {
        File marker = new File(filesDir, GameDataPaths.GAMEDATA_PATH_MARKER);
        if (!marker.isFile()) {
            return null;
        }
        try {
            String s = new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).trim();
            int nl = s.indexOf('\n');
            if (nl >= 0) {
                s = s.substring(0, nl).trim();
            }
            return s.isEmpty() ? null : s;
        } catch (IOException e) {
            return null;
        }
    }
}
