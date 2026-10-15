package com.generalsx.zerohour.importer;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class GameDataReadyTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void structureReadyRequiresComplete() throws Exception {
        File root = tmp.getRoot();
        File gameData = new File(root, "GameData");
        assertTrue(gameData.mkdir());
        assertFalse(GameDataReady.isReady(gameData));

        byte[] payload = "hello".getBytes(StandardCharsets.UTF_8);
        File ini = new File(gameData, "ini.big");
        try (FileOutputStream out = new FileOutputStream(ini)) { out.write(payload); }

        ImportState s = new ImportState();
        s.status = ImportState.STATUS_COMPLETE;
        s.manifestSha256 = "deadbeef";
        s.putVerified("ini.big", payload.length, "abcd", ini.lastModified());
        ImportStateStore.save(new File(root, "import-state.json"), s);
        assertTrue(GameDataReady.isReadyStructure(new File(root, "import-state.json"), gameData));
    }
}
