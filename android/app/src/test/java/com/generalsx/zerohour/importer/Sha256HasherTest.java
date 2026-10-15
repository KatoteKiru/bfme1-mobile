package com.generalsx.zerohour.importer;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertEquals;

public class Sha256HasherTest {
    @Test
    public void hashesEmpty() throws Exception {
        String hex = Sha256Hasher.hashStream(new ByteArrayInputStream(new byte[0]));
        assertEquals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
    }

    @Test
    public void copyAndHashMatchesKnown() throws Exception {
        byte[] data = "bfme-e1-12a".getBytes(StandardCharsets.UTF_8);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        String hex = Sha256Hasher.copyAndHash(new ByteArrayInputStream(data), out);
        assertEquals(data.length, out.size());
        assertEquals(Sha256Hasher.hashStream(new ByteArrayInputStream(data)), hex);
    }
}
