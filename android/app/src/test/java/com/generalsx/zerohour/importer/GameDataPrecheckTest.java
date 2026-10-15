package com.generalsx.zerohour.importer;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class GameDataPrecheckTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Before
    public void installFreeSpaceStub() {
        GameDataPrecheck.testFreeBytesOverride = 50L * 1024 * 1024 * 1024; // 50 GiB
    }

    @After
    public void clearFreeSpaceStub() {
        GameDataPrecheck.testFreeBytesOverride = null;
    }


    private static GameDataManifest tiny() throws Exception {
        return GameDataManifest.parse("{\"schemaVersion\":1,\"profiles\":{\"p\":{\"files\":["
                + "{\"path\":\"ini.big\",\"size\":100,\"sha256\":\"aa\",\"required\":true},"
                + "{\"path\":\"maps.big\",\"size\":200,\"sha256\":\"bb\",\"required\":true},"
                + "{\"path\":\"music.big\",\"size\":50,\"sha256\":\"cc\",\"required\":false}"
                + "]}}}");
    }

    private static SafTreeEntry file(String path, long size) {
        return new SafTreeEntry(path, path, "content://doc/" + path, size, false);
    }

    private Map<String, SafTreeEntry> fold(SafTreeEntry... entries) {
        Map<String, SafTreeEntry> m = new HashMap<>();
        for (SafTreeEntry e : entries) {
            m.put(PathSafety.foldKey(e.relativePath), e);
        }
        return m;
    }

    @Test
    public void okWhenRequiredPresent() throws Exception {
        File dest = tmp.newFolder("GameData");
        PrecheckResult r = GameDataPrecheck.check(tiny(),
                fold(file("ini.big", 100), file("maps.big", 200)), dest);
        assertEquals(PrecheckResult.Level.OK, r.level);
        assertTrue(r.canStartImport());
    }

    @Test
    public void errorWhenRequiredMissing() throws Exception {
        File dest = tmp.newFolder("GameData");
        PrecheckResult r = GameDataPrecheck.check(tiny(),
                fold(file("ini.big", 100)), dest);
        assertEquals(PrecheckResult.Level.ERROR, r.level);
        assertFalse(r.canStartImport());
    }

    @Test
    public void errorOnUnknownSizeRequired() throws Exception {
        File dest = tmp.newFolder("GameData");
        PrecheckResult r = GameDataPrecheck.check(tiny(),
                fold(file("ini.big", -1), file("maps.big", 200)), dest);
        assertEquals(PrecheckResult.Level.ERROR, r.level);
    }

    @Test
    public void caseInsensitiveTreeLookup() throws Exception {
        File dest = tmp.newFolder("GameData");
        PrecheckResult r = GameDataPrecheck.check(tiny(),
                fold(file("INI.big", 100), file("Maps.BIG", 200)), dest);
        assertTrue(r.missingRequired.isEmpty());
        assertEquals(PrecheckResult.Level.OK, r.level);
    }
}
