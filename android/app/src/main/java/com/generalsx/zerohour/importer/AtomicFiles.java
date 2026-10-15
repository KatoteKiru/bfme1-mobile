package com.generalsx.zerohour.importer;

import android.system.ErrnoException;
import android.system.Os;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Best-effort atomic replace (Os.rename / ATOMIC_MOVE, then fallback). */
public final class AtomicFiles {
    private AtomicFiles() {}

    public static void moveReplace(File from, File to) throws IOException {
        if (from == null || to == null) {
            throw new IOException("null path");
        }
        File parent = to.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("mkdir " + parent);
        }
        try {
            Files.move(from.toPath(), to.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return;
        } catch (IOException ignored) {
            // fall through
        }
        try {
            Os.rename(from.getAbsolutePath(), to.getAbsolutePath());
            return;
        } catch (ErrnoException e) {
            // fall through
        }
        if (to.exists() && !to.delete()) {
            throw new IOException("cannot replace " + to);
        }
        if (!from.renameTo(to)) {
            throw new IOException("rename failed: " + from + " → " + to);
        }
    }
}
