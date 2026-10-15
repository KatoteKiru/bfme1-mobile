package com.generalsx.zerohour.importer;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class ImportStateStoreTest {
    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void roundTripResumeState() throws Exception {
        File stateFile = tmp.newFile("import-state.json");
        ImportState s = new ImportState();
        s.status = ImportState.STATUS_IN_PROGRESS;
        s.profile = "patch-2.22v5.5.4";
        s.sourceTreeUri = "content://tree/abc";
        s.startedAtMs = 1000L;
        s.putVerified("ini.big", 10, "abcd", 2000L);
        ImportStateStore.save(stateFile, s);

        ImportState loaded = ImportStateStore.load(stateFile);
        assertEquals(ImportState.STATUS_IN_PROGRESS, loaded.status);
        assertEquals("patch-2.22v5.5.4", loaded.profile);
        assertEquals("content://tree/abc", loaded.sourceTreeUri);
        assertTrue(loaded.get("ini.big").verified);
        assertEquals(10L, loaded.get("ini.big").size);
        assertEquals("abcd", loaded.get("ini.big").sha256);
    }

    @Test
    public void completeMeansReadyFlag() throws Exception {
        ImportState s = new ImportState();
        s.status = ImportState.STATUS_COMPLETE;
        assertTrue(s.isComplete());
    }
}
