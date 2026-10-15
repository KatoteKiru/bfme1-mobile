package com.generalsx.zerohour.importer;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

/**
 * QA: Formats test set = ini.big + maps.big + 5×_patch222* (7 files).
 * Pins what the app does on such a folder.
 */
public class FormatsTestSetTest {
    static final List<String> SEVEN = Arrays.asList(
            "ini.big", "maps.big", "_patch222.big", "_patch222bases.big",
            "_patch222libraries.big", "_patch222maps.big", "_patch222textures.big");

    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Before public void stub() { GameDataPrecheck.testFreeBytesOverride = 50L << 30; }
    @After public void unstub() { GameDataPrecheck.testFreeBytesOverride = null; }

    private static String officialJson() throws Exception {
        Path p = Paths.get("..", "..", "docs", "bfme1-manifest.json").normalize().toAbsolutePath();
        return new String(Files.readAllBytes(p), StandardCharsets.UTF_8);
    }

    private static Map<String, SafTreeEntry> treeOfSeven(GameDataManifest m) {
        Map<String, SafTreeEntry> tree = new HashMap<>();
        for (String n : SEVEN) {
            ManifestFileEntry e = m.get(n);
            tree.put(PathSafety.foldKey(n), new SafTreeEntry(n, n, "content://t/" + n, e.size, false));
        }
        return tree;
    }

    /** (a) Official manifest (APK): red precheck, import button disabled. */
    @Test
    public void officialManifestOnSevenFilesIsRed() throws Exception {
        GameDataManifest m = GameDataManifest.parse(officialJson());
        int required = m.requiredFiles().size();
        PrecheckResult r = GameDataPrecheck.check(m, treeOfSeven(m), tmp.newFolder("GD"));
        assertEquals(PrecheckResult.Level.ERROR, r.level);
        assertFalse(r.canStartImport());
        assertEquals(required - 7, r.missingRequired.size());
        assertTrue(r.messagesRu.get(0),
                r.messagesRu.get(0).startsWith("Не хватает обязательных файлов (" + (required - 7) + "): "));
        assertTrue(r.messagesEn.get(0).startsWith("Missing required files (" + (required - 7) + "): "));
        System.out.println("QA 7-file/official: " + r.messagesRu.get(0));
    }

    /** (b) Manifest of exactly these 7 rows (real names/sizes/sha from docs) → N = 7. */
    @Test
    public void sevenFileManifestGivesN7() throws Exception {
        JSONObject root = new JSONObject(officialJson());
        String key = root.getString("defaultProfile");
        JSONObject prof = root.getJSONObject("profiles").getJSONObject(key);
        JSONArray files = prof.getJSONArray("files");
        JSONArray seven = new JSONArray();
        for (int i = 0; i < files.length(); i++) {
            if (SEVEN.contains(files.getJSONObject(i).getString("path"))) {
                seven.put(files.getJSONObject(i));
            }
        }
        prof.put("files", seven);
        GameDataManifest m = GameDataManifest.parse(root.toString());
        assertEquals(7, m.importableFiles().size());
        assertEquals(7, m.requiredFiles().size());
        PrecheckResult r = GameDataPrecheck.check(m, treeOfSeven(m), tmp.newFolder("GD"));
        assertEquals(PrecheckResult.Level.OK, r.level);
        assertTrue(r.missingRequired.isEmpty());
        // Importer N == importable files present (all 7 required, no optional).
        assertEquals("Patch 2.22v7.0.5", m.displayLabel());
    }

    /** Resume: same size+sha but different mtime → re-hash; tampered content → not verified. */
    @Test
    public void resumeRehashesWhenMtimeChanged() throws Exception {
        File f = tmp.newFile("a.big");
        byte[] data = "hello-bfme".getBytes(StandardCharsets.UTF_8);
        Files.write(f.toPath(), data);
        String sha = Sha256Hasher.hashStream(new java.io.ByteArrayInputStream(data));
        ManifestFileEntry e = new ManifestFileEntry("a.big", data.length, sha, true, "big");
        ImportState.FileRecord same = new ImportState.FileRecord("a.big", data.length, sha, f.lastModified(), true);
        assertTrue(GameDataImporter.isResumeVerified(same, e, f));
        ImportState.FileRecord stale = new ImportState.FileRecord("a.big", data.length, sha, 1L, true);
        assertTrue(GameDataImporter.isResumeVerified(stale, e, f));
        Files.write(f.toPath(), "HELLO-bfme".getBytes(StandardCharsets.UTF_8));
        assertFalse(GameDataImporter.isResumeVerified(stale, e, f));
    }
}
