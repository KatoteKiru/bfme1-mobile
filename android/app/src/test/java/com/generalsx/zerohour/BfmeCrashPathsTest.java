package com.generalsx.zerohour;

// BFME: crash-path JVM tests: which crash log copy goes into the report.

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.util.List;

public class BfmeCrashPathsTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Test public void internalWins() throws Exception {
        File in = tmp.newFolder("in"), ext = tmp.newFolder("ext");
        new File(in, "crash.log").createNewFile();
        new File(ext, "crash.log").createNewFile();
        assertEquals(new File(in, "crash.log"), BfmeCrashPaths.pick(in, ext, "crash.log"));
    }

    @Test public void externalWhenInternalMissing() throws Exception {
        File in = tmp.newFolder("in"), ext = tmp.newFolder("ext");
        new File(ext, "crash-selftest.log").createNewFile();
        assertEquals(new File(ext, "crash-selftest.log"), BfmeCrashPaths.pick(in, ext, "crash-selftest.log"));
    }

    @Test public void neitherOrNoExternalGivesInternalPath() throws Exception {
        File in = tmp.newFolder("in"), ext = tmp.newFolder("ext");
        assertEquals(new File(in, "crash.log"), BfmeCrashPaths.pick(in, ext, "crash.log"));
        assertEquals(new File(in, "crash.log"), BfmeCrashPaths.pick(in, null, "crash.log"));
    }

    @Test public void clearCoversExternalMirrors() throws Exception {
        File in = tmp.newFolder("in"), ext = tmp.newFolder("ext");
        String[] names = {"crash.log", "crash-prev.log", "crash-selftest.log"};
        for (String n : names) {
            new File(in, n).createNewFile();
            new File(ext, n).createNewFile();
        }
        new File(ext, "generals-stderr.log").createNewFile();
        new File(ext, "generals-stderr-prev.log").createNewFile();
        List<File> list = BfmeCrashPaths.filesToClear(in, ext);
        assertEquals(8, list.size());
        for (File f : list) assertTrue(f.delete());
        for (String n : names) {
            assertFalse(new File(ext, n).exists());
            // the stale external copy can no longer reach the report
            assertEquals(new File(in, n), BfmeCrashPaths.pick(in, ext, n));
        }
        assertEquals(0, ext.list().length);
        assertEquals(0, in.list().length);
    }

    @Test public void clearWithoutExternal() {
        File in = new File("/x/in");
        List<File> list = BfmeCrashPaths.filesToClear(in, null);
        assertEquals(3, list.size());
        for (File f : list) assertEquals(in, f.getParentFile());
    }
}
