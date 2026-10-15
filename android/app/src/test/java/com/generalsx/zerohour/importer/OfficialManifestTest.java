package com.generalsx.zerohour.importer;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Locale;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

public class OfficialManifestTest {
    private static final long EXPECTED_FILE_COUNT = 351L;
    /** Sum of importable (non win-binary) sizes from docs/bfme1-manifest.json. */
    private static final long EXPECTED_IMPORTABLE_BYTES = 3_812_762_862L;
    private static final int EXPECTED_WIN_BINARY = 15;

    private static Path resolveDocsManifest() {
        Path fromApp = Paths.get("..", "..", "docs", "bfme1-manifest.json").normalize().toAbsolutePath();
        if (Files.isRegularFile(fromApp)) return fromApp;
        Path generated = Paths.get("build", "generated", "bfmeAssets", "bfme", "bfme1-manifest.json").toAbsolutePath();
        if (Files.isRegularFile(generated)) return generated;
        throw new AssertionError("docs/bfme1-manifest.json not found at " + fromApp);
    }

    @Test
    public void officialManifestShapeAndTotals() throws Exception {
        Path path = resolveDocsManifest();
        byte[] raw = Files.readAllBytes(path);
        GameDataManifest m = GameDataManifest.parse(new String(raw, StandardCharsets.UTF_8));

        assertEquals(1, m.schemaVersion);
        assertEquals("patch-2.22v5.5.4", m.defaultProfile);
        assertEquals("patch-2.22v5.5.4", m.profile);
        assertEquals("bfme1", m.game);
        assertEquals("Patch 2.22v7.0.5", m.displayVersion);
        assertEquals("Patch 2.22v7.0.5", m.displayLabel());

        assertEquals(EXPECTED_FILE_COUNT, m.files.size());
        assertEquals(EXPECTED_FILE_COUNT - EXPECTED_WIN_BINARY, m.importableFiles().size());
        assertEquals(EXPECTED_IMPORTABLE_BYTES, m.totalBytes());

        // required count is derived from the manifest (may grow when window.big becomes required).
        int requiredFromScan = 0;
        long allBytes = 0L;
        int winBinary = 0;
        for (ManifestFileEntry e : m.files) {
            assertNotNull(e.path);
            assertEquals(e.path, e.path.toLowerCase(Locale.ROOT));
            PathSafety.requireSafeRelative(e.path);
            assertEquals(64, e.sha256.length());
            assertNotNull(e.kind);
            allBytes += e.size;
            if (ManifestFileEntry.KIND_WIN_BINARY.equals(e.kind)) {
                winBinary++;
                assertFalse(e.shouldImport());
            } else {
                assertTrue(e.shouldImport());
                assertTrue(ManifestFileEntry.KIND_BIG.equals(e.kind)
                        || ManifestFileEntry.KIND_LOOSE.equals(e.kind)
                        || ManifestFileEntry.KIND_IMPORT.equals(e.kind));
                if (e.required) {
                    requiredFromScan++;
                }
            }
        }
        assertEquals(EXPECTED_WIN_BINARY, winBinary);
        assertEquals(requiredFromScan, m.requiredFiles().size());
        assertTrue("at least one required importable file", requiredFromScan > 0);
        assertTrue(m.requiredBytes() > 0L);
        assertEquals(allBytes, m.totalBytes() + sumWinBinary(m));

        ManifestFileEntry window = m.get("window.big");
        assertNotNull(window);
        assertTrue("window.big is required since Formats #37", window.required);

        String sha = GameDataManifest.sha256OfBytes(raw);
        assertEquals(64, sha.length());
    }

    private static long sumWinBinary(GameDataManifest m) {
        long sum = 0L;
        for (ManifestFileEntry e : m.files) {
            if (ManifestFileEntry.KIND_WIN_BINARY.equals(e.kind)) {
                sum += e.size;
            }
        }
        return sum;
    }
}
