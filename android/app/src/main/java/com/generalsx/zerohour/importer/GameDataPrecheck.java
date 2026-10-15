package com.generalsx.zerohour.importer;

import android.os.StatFs;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Fast pre-import checks. Space budget = bytes still to copy × 1.1
 * + size of the largest file being replaced (temp .part peak).
 * StatFs failure is an error (not soft-skip).
 */
public final class GameDataPrecheck {
    private GameDataPrecheck() {}

    /** Unit-test only: when non-null, {@link #freeBytesOrThrow} returns this (StatFs stub). */
    static volatile Long testFreeBytesOverride = null;

    public static PrecheckResult check(GameDataManifest manifest,
                                       Map<String, SafTreeEntry> treeByFolded,
                                       File destRootForStatFs,
                                       ImportState priorState) {
        List<String> missing = new ArrayList<>();
        List<String> sizeMismatch = new ArrayList<>();
        List<String> unknownKeys = new ArrayList<>();
        List<String> en = new ArrayList<>();
        List<String> ru = new ArrayList<>();
        List<String> caseCollisions = new ArrayList<>();

        long bytesToCopy = 0L;
        long largestReplace = 0L;

        for (ManifestFileEntry e : manifest.importableFiles()) {
            String fold = PathSafety.foldKey(e.path);
            SafTreeEntry found = treeByFolded.get(fold);
            ImportState.FileRecord prior = priorState != null ? priorState.get(e.path) : null;
            boolean alreadyOk = prior != null && prior.verified
                    && e.sha256.equalsIgnoreCase(prior.sha256);

            if (e.required) {
                if (found == null) {
                    missing.add(e.path);
                    continue;
                }
                if (found.size < 0) {
                    en.add("Unknown size for " + e.path + " (virtual document?)");
                    ru.add("Неизвестный размер " + e.path + " (виртуальный документ?)");
                    sizeMismatch.add(e.path + " (size unknown)");
                    continue;
                }
                if (found.size != e.size) {
                    sizeMismatch.add(e.path + " (expected " + e.size + ", got " + found.size + ")");
                }
            } else if (found != null && found.size >= 0 && found.size != e.size) {
                unknownKeys.add(e.path);
            }

            if (found != null && !alreadyOk) {
                bytesToCopy += e.size;
                if (e.size > largestReplace) {
                    largestReplace = e.size;
                }
            }
        }

        long budget = (long) (bytesToCopy * 1.10) + largestReplace;
        long free;
        try {
            free = freeBytesOrThrow(destRootForStatFs);
        } catch (IllegalStateException se) {
            en.add("Cannot query free space: " + se.getMessage());
            ru.add("Не удалось узнать свободное место: " + se.getMessage());
            return new PrecheckResult(PrecheckResult.Level.ERROR, manifest.profile, budget, -1L,
                    missing, sizeMismatch, unknownKeys, en, ru);
        }

        if (!missing.isEmpty()) {
            en.add("Missing required files (" + missing.size() + "): " + joinSample(missing, 8));
            ru.add("Не хватает обязательных файлов (" + missing.size() + "): " + joinSample(missing, 8));
        }
        if (!sizeMismatch.isEmpty()) {
            en.add("Size mismatch: " + joinSample(sizeMismatch, 5));
            ru.add("Неверный размер: " + joinSample(sizeMismatch, 5));
        }
        if (free < budget && bytesToCopy > 0) {
            en.add(String.format(Locale.US, "Not enough free space: need ~%s, free %s",
                    human(budget), human(free)));
            ru.add(String.format(Locale.US, "Недостаточно места: нужно ≈%s, свободно %s",
                    human(budget), human(free)));
        }
        if (!unknownKeys.isEmpty()) {
            en.add("Modified key/optional sizes (warn): " + joinSample(unknownKeys, 6));
            ru.add("Изменённые размеры (предупреждение): " + joinSample(unknownKeys, 6));
        }

        PrecheckResult.Level level;
        boolean spaceFail = bytesToCopy > 0 && free < budget;
        if (!missing.isEmpty() || !sizeMismatch.isEmpty() || spaceFail) {
            level = PrecheckResult.Level.ERROR;
        } else if (!unknownKeys.isEmpty()) {
            level = PrecheckResult.Level.WARN;
            en.add(0, "Game files look modified (version " + manifest.displayLabel() + ").");
            ru.add(0, "Файлы выглядят изменёнными (версия " + manifest.displayLabel() + ").");
        } else {
            level = PrecheckResult.Level.OK;
            en.add("Ready to import version " + manifest.displayLabel()
                    + " (" + manifest.requiredFiles().size() + " required, "
                    + human(bytesToCopy) + " to copy).");
            ru.add("Готово к импорту версии " + manifest.displayLabel()
                    + " (" + manifest.requiredFiles().size() + " обязательных, "
                    + human(bytesToCopy) + " к копированию).");
        }
        return new PrecheckResult(level, manifest.profile, budget, free,
                missing, sizeMismatch, unknownKeys, en, ru);
    }

    /** Overload without prior state. */
    public static PrecheckResult check(GameDataManifest manifest,
                                       Map<String, SafTreeEntry> treeByFolded,
                                       File destRootForStatFs) {
        return check(manifest, treeByFolded, destRootForStatFs, null);
    }

    static long freeBytesOrThrow(File anyPathOnVolume) {
        if (anyPathOnVolume == null) {
            throw new IllegalStateException("destination volume path is null");
        }
        File probe = anyPathOnVolume;
        if (!probe.exists()) {
            File parent = probe.getParentFile();
            probe = parent != null ? parent : probe;
        }
        if (testFreeBytesOverride != null) {
            return testFreeBytesOverride;
        }
        try {
            if (!probe.exists() && !probe.mkdirs()) {
                // StatFs still needs an existing path on the volume
                probe = anyPathOnVolume.getParentFile() != null
                        ? anyPathOnVolume.getParentFile() : anyPathOnVolume;
            }
            StatFs stat = new StatFs(probe.getAbsolutePath());
            return stat.getAvailableBytes();
        } catch (RuntimeException e) {
            throw new IllegalStateException(e.getMessage() == null ? "StatFs failed" : e.getMessage(), e);
        }
    }

    private static String joinSample(List<String> items, int max) {
        StringBuilder sb = new StringBuilder();
        int n = Math.min(items.size(), max);
        for (int i = 0; i < n; i++) {
            if (i > 0) sb.append(", ");
            sb.append(items.get(i));
        }
        if (items.size() > max) sb.append(", …");
        return sb.toString();
    }

    public static String human(long bytes) {
        if (bytes < 0) return "?";
        double gb = bytes / (1024.0 * 1024.0 * 1024.0);
        if (gb >= 1.0) return String.format(Locale.US, "%.2f GiB", gb);
        return String.format(Locale.US, "%.1f MiB", bytes / (1024.0 * 1024.0));
    }
}
