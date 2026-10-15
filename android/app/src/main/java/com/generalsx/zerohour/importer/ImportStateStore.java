package com.generalsx.zerohour.importer;

import android.content.Context;

import org.json.JSONException;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public final class ImportStateStore {
    private static final String TAG = "BfmeImport";

    private ImportStateStore() {}

    public static ImportState load(Context ctx) {
        return load(GameDataPaths.importStateFile(ctx));
    }

    public static ImportState load(File stateFile) {
        if (stateFile == null || !stateFile.isFile()) {
            return new ImportState();
        }
        try (FileInputStream in = new FileInputStream(stateFile)) {
            byte[] buf = new byte[(int) Math.min(stateFile.length(), 8 * 1024 * 1024)];
            int n = in.read(buf);
            if (n <= 0) {
                return new ImportState();
            }
            return ImportState.fromJson(new String(buf, 0, n, StandardCharsets.UTF_8));
        } catch (IOException | JSONException e) {
            ImportLog.w(TAG, "failed to read import-state.json: " + e.getMessage());
            return new ImportState();
        }
    }

    public static void save(Context ctx, ImportState state) throws IOException {
        save(GameDataPaths.importStateFile(ctx), state);
    }

    public static void save(File stateFile, ImportState state) throws IOException {
        if (stateFile == null) {
            throw new IOException("import-state path is null");
        }
        File parent = stateFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("cannot create " + parent);
        }
        state.updatedAtMs = System.currentTimeMillis();
        String json;
        try {
            json = state.toJson();
        } catch (JSONException e) {
            throw new IOException("serialize import-state", e);
        }
        File tmp = new File(stateFile.getAbsolutePath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(json.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        AtomicFiles.moveReplace(tmp, stateFile);
    }
}
