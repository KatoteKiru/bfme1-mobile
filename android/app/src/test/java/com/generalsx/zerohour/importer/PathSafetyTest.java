package com.generalsx.zerohour.importer;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class PathSafetyTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void rejectsTraversalAndAbsolute() {
        String[] bad = {"../x", "/abs", "a/../b", "a//b", "a/./b", "C:\\\\x", "a:b", "a\\b", "x\0y", ""};
        for (String p : bad) {
            try {
                PathSafety.requireSafeRelative(p);
                fail("expected reject: " + p);
            } catch (IllegalArgumentException expected) {
                // ok
            }
        }
    }

    @Test
    public void resolveStaysUnderGameData() throws Exception {
        File root = tmp.newFolder("GameData");
        File dest = PathSafety.resolveUnderGameData(root, "lang/english.big");
        assertTrue(dest.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator)
                || dest.getCanonicalPath().equals(root.getCanonicalPath()));
        assertEquals("english.big", dest.getName());
    }

    @Test
    public void foldKeyLowercases() {
        assertEquals("ini.big", PathSafety.foldKey("INI.big"));
        assertEquals("lang/english.big", PathSafety.foldKey("Lang/English.BIG"));
    }
}
