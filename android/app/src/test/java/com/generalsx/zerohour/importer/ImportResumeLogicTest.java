package com.generalsx.zerohour.importer;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

/** Checkpoint/resume contract: verified entries survive cancel and reload. */
public class ImportResumeLogicTest {
    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void cancelledStateKeepsVerifiedForResume() throws Exception {
        File root = tmp.getRoot();
        File gameData = new File(root, "GameData");
        assertTrue(gameData.mkdir());
        File ini = new File(gameData, "ini.big");
        byte[] payload = "verified-payload".getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream out = new FileOutputStream(ini)) {
            out.write(payload);
        }

        ImportState s = new ImportState();
        s.status = ImportState.STATUS_CANCELLED;
        s.profile = "patch-2.22v5.5.4";
        s.putVerified("ini.big", payload.length,
                Sha256Hasher.hashStream(new java.io.ByteArrayInputStream(payload)),
                ini.lastModified());
        File stateFile = new File(root, "import-state.json");
        ImportStateStore.save(stateFile, s);

        ImportState loaded = ImportStateStore.load(stateFile);
        assertEquals(ImportState.STATUS_CANCELLED, loaded.status);
        assertTrue(loaded.get("ini.big").verified);
        assertEquals(1, loaded.verifiedPaths().size());
        // Not ready until status=complete
        assertTrue(!GameDataReady.isReady(gameData));

        loaded.status = ImportState.STATUS_COMPLETE;
        ImportStateStore.save(stateFile, loaded);
        assertTrue(GameDataReady.isReady(gameData));
    }
}
