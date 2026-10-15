package com.generalsx.zerohour.importer;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class Sha256CancelTest {
    @Test
    public void cancelStopsMidCopy() throws Exception {
        byte[] data = new byte[1024 * 1024];
        AtomicBoolean cancel = new AtomicBoolean(false);
        AtomicLong seen = new AtomicLong();
        try {
            Sha256Hasher.copyAndHash(new ByteArrayInputStream(data), new ByteArrayOutputStream(),
                    () -> { if (seen.get() > 100_000) cancel.set(true); return cancel.get(); },
                    seen::set);
            fail("expected CancelledException");
        } catch (Sha256Hasher.CancelledException expected) {
            assertTrue(seen.get() > 0 && seen.get() < data.length);
        }
    }
}
