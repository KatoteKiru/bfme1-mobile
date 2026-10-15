package com.generalsx.zerohour.importer;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class GameDataManifestTest {
    @Test
    public void singleProfileTakenFromManifest() throws Exception {
        String json = "{\"schemaVersion\":1,\"profiles\":{\"only-profile\":{\"files\":["
                + "{\"path\":\"ini.big\",\"size\":10,\"sha256\":\"aa\",\"required\":true,\"kind\":\"big\"}"
                + "]}}}";
        GameDataManifest m = GameDataManifest.parse(json);
        assertEquals("only-profile", m.profile);
        assertEquals("only-profile", m.displayLabel());
    }

    @Test
    public void defaultProfileSelectsAmongMany() throws Exception {
        String json = "{\"schemaVersion\":1,\"defaultProfile\":\"chosen\","
                + "\"profiles\":{"
                + "\"other\":{\"files\":[{\"path\":\"x.big\",\"size\":1,\"sha256\":\"a\",\"required\":true,\"kind\":\"big\"}]},"
                + "\"chosen\":{\"game\":\"bfme1\",\"displayVersion\":\"Patch X\","
                + "\"files\":[{\"path\":\"ini.big\",\"size\":10,\"sha256\":\"aa\",\"required\":true,\"kind\":\"big\"}]}"
                + "}}";
        GameDataManifest m = GameDataManifest.parse(json);
        assertEquals(1, m.schemaVersion);
        assertEquals("chosen", m.defaultProfile);
        assertEquals("chosen", m.profile);
        assertEquals("bfme1", m.game);
        assertEquals("Patch X", m.displayVersion);
        assertEquals("Patch X", m.displayLabel());
        assertNull(m.get("x.big"));
        assertTrue(m.get("ini.big").required);
    }

    @Test
    public void multiProfileWithoutDefaultRequiresExplicitKey() throws Exception {
        String json = "{\"schemaVersion\":1,\"profiles\":{"
                + "\"a\":{\"files\":[{\"path\":\"a.big\",\"size\":1,\"sha256\":\"a\",\"required\":true}]},"
                + "\"b\":{\"files\":[{\"path\":\"b.big\",\"size\":1,\"sha256\":\"b\",\"required\":true}]}"
                + "}}";
        try {
            GameDataManifest.parse(json);
            fail("expected JSONException");
        } catch (org.json.JSONException e) {
            assertTrue(e.getMessage().contains("defaultProfile"));
        }
        GameDataManifest m = GameDataManifest.parse(json, "b");
        assertEquals("b", m.profile);
    }

    @Test
    public void winBinaryNotImported() throws Exception {
        String json = "{\"schemaVersion\":1,\"defaultProfile\":\"p\",\"profiles\":{\"p\":{\"files\":["
                + "{\"path\":\"lotrbfme.exe\",\"size\":1,\"sha256\":\"aa\",\"required\":false,\"kind\":\"win-binary\"},"
                + "{\"path\":\"ini.big\",\"size\":2,\"sha256\":\"bb\",\"required\":true,\"kind\":\"big\"},"
                + "{\"path\":\"data/x.ini\",\"size\":3,\"sha256\":\"cc\",\"required\":false,\"kind\":\"loose\"}"
                + "]}}}";
        GameDataManifest m = GameDataManifest.parse(json);
        assertEquals(3, m.files.size());
        assertEquals(2, m.importableFiles().size());
        assertEquals("ini.big", m.importableFiles().get(0).path);
        assertEquals(ManifestFileEntry.KIND_LOOSE, m.get("data/x.ini").kind);
        assertFalse(m.get("lotrbfme.exe").shouldImport());
    }

    @Test
    public void windowBigOptionalDoesNotBreakRequired() throws Exception {
        String json = "{\"schemaVersion\":1,\"defaultProfile\":\"p\",\"profiles\":{\"p\":{\"files\":["
                + "{\"path\":\"ini.big\",\"size\":2,\"sha256\":\"bb\",\"required\":true,\"kind\":\"big\"},"
                + "{\"path\":\"window.big\",\"size\":3,\"sha256\":\"cc\",\"required\":false,\"kind\":\"big\"}"
                + "]}}}";
        GameDataManifest m = GameDataManifest.parse(json);
        assertEquals(1, m.requiredFiles().size());
        assertEquals("ini.big", m.requiredFiles().get(0).path);
        assertFalse(m.get("window.big").required);
    }

    @Test
    public void legacyFlatRejected() throws Exception {
        try {
            GameDataManifest.parse("{\"profile\":\"p\",\"files\":[]}");
            fail();
        } catch (org.json.JSONException e) {
            assertTrue(e.getMessage().contains("profiles"));
        }
    }

    @Test
    public void schemaVersionTooNewAsksToUpdateApp() throws Exception {
        String json = "{\"schemaVersion\":2,\"defaultProfile\":\"p\",\"profiles\":{\"p\":{\"files\":[]}}}";
        try {
            GameDataManifest.parse(json);
            fail("expected JSONException");
        } catch (org.json.JSONException e) {
            assertTrue(e.getMessage().contains("update"));
            assertTrue(e.getMessage().contains("schemaVersion"));
        }
    }

    @Test
    public void unknownKindStillImportable() throws Exception {
        String json = "{\"schemaVersion\":1,\"defaultProfile\":\"p\",\"profiles\":{\"p\":{\"files\":["
                + "{\"path\":\"x.big\",\"size\":1,\"sha256\":\"aa\",\"required\":true,\"kind\":\"future-kind\"}"
                + "]}}}";
        GameDataManifest m = GameDataManifest.parse(json);
        assertEquals(1, m.importableFiles().size());
        assertTrue(m.get("x.big").shouldImport());
    }

    @Test
    public void missingSchemaVersionRejected() throws Exception {
        try {
            GameDataManifest.parse("{\"profiles\":{\"p\":{\"files\":[]}}}");
            fail();
        } catch (org.json.JSONException e) {
            assertTrue(e.getMessage().contains("schemaVersion"));
        }
    }
}
