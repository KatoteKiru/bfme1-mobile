package com.generalsx.zerohour.importer;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

/** QA 2.10: tree may expose INI.big / Maps.BIG / _PATCH222.big / Lang/English.big. */
public class CaseInsensitiveTreeTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Before
    public void installFreeSpaceStub() {
        GameDataPrecheck.testFreeBytesOverride = 50L * 1024 * 1024 * 1024; // 50 GiB
    }

    @After
    public void clearFreeSpaceStub() {
        GameDataPrecheck.testFreeBytesOverride = null;
    }


    @Test
    public void precheckAcceptsOddCasingFromProvider() throws Exception {
        GameDataManifest m = GameDataManifest.parse("{\"schemaVersion\":1,\"profiles\":{\"p\":{\"files\":["
                + "{\"path\":\"ini.big\",\"size\":1,\"sha256\":\"aa\",\"required\":true},"
                + "{\"path\":\"maps.big\",\"size\":2,\"sha256\":\"bb\",\"required\":true},"
                + "{\"path\":\"_patch222.big\",\"size\":3,\"sha256\":\"cc\",\"required\":true},"
                + "{\"path\":\"lang/english.big\",\"size\":4,\"sha256\":\"dd\",\"required\":true}"
                + "]}}}");
        Map<String, SafTreeEntry> tree = new HashMap<>();
        // Simulate SafTreeWalker folded index after provider returned mixed case paths
        put(tree, "INI.big", 1);
        put(tree, "Maps.BIG", 2);
        put(tree, "_PATCH222.big", 3);
        put(tree, "Lang/English.big", 4);
        File dest = tmp.newFolder("GameData");
        PrecheckResult r = GameDataPrecheck.check(m, tree, dest, null);
        assertTrue(r.missingRequired.isEmpty());
        assertEquals(PrecheckResult.Level.OK, r.level);
    }

    private static void put(Map<String, SafTreeEntry> tree, String displayPath, long size) {
        String fold = PathSafety.foldKey(displayPath);
        tree.put(fold, new SafTreeEntry(displayPath, displayPath, "content://x/" + fold, size, false));
    }
}
