package com.generalsx.zerohour.importer;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

/** E1-12c: screen and native read one truth (gamedata_path.txt gated by import-state). */
public class GameDataLocationTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    @Test
    public void notCompleteResolvesNullEvenWithMarker() throws Exception {
        File files = tmp.newFolder("files");
        File gd = tmp.newFolder("ext", "GameData");
        GameDataLocation.publish(files, gd);
        assertNull(GameDataLocation.resolve(files, gd, false));
    }

    @Test
    public void completeWithoutMarkerHealsMarker() throws Exception {
        // Pixel 8 Pro bug: import complete, but Setup showed "not set" (it read SharedPreferences).
        File files = tmp.newFolder("files");
        File gd = tmp.newFolder("ext", "GameData");
        assertEquals(gd.getAbsolutePath(), GameDataLocation.resolve(files, gd, true));
        assertEquals(gd.getAbsolutePath(), GameDataLocation.readMarker(files));
    }

    @Test
    public void staleMarkerIsRewritten() throws Exception {
        File files = tmp.newFolder("files");
        File gd = tmp.newFolder("ext", "GameData");
        Files.write(new File(files, "gamedata_path.txt").toPath(),
                "/storage/emulated/0/OldGame\n".getBytes(StandardCharsets.UTF_8));
        assertEquals(gd.getAbsolutePath(), GameDataLocation.resolve(files, gd, true));
        assertEquals(gd.getAbsolutePath(), GameDataLocation.readMarker(files));
    }

    @Test
    public void markerFormatIsAbsolutePathPlusNewline() throws Exception {
        File files = tmp.newFolder("files");
        File gd = tmp.newFolder("ext", "GameData");
        GameDataLocation.publish(files, gd);
        String raw = new String(Files.readAllBytes(new File(files, "gamedata_path.txt").toPath()),
                StandardCharsets.UTF_8);
        assertEquals(gd.getAbsolutePath() + "\n", raw);
    }
}
