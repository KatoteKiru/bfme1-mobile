package com.generalsx.zerohour.importer;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.concurrent.atomic.AtomicBoolean;

/** Streaming SHA-256 helpers used during copy and unit tests. */
public final class Sha256Hasher {
    private static final int BUF = 256 * 1024;

    public interface CancelCheck {
        boolean isCancelled();
    }

    public interface ByteProgress {
        void onBytes(long fileBytesCopied);
    }

    public static final class CancelledException extends IOException {
        public CancelledException() {
            super("cancelled");
        }
    }

    private Sha256Hasher() {}

    public static MessageDigest newDigest() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 not available", e);
        }
    }

    /** Copy in→out while hashing; returns lowercase hex digest. */
    public static String copyAndHash(InputStream in, OutputStream out) throws IOException {
        return copyAndHash(in, out, null, null);
    }

    public static String copyAndHash(InputStream in, OutputStream out,
                                     CancelCheck cancel, ByteProgress progress) throws IOException {
        MessageDigest md = newDigest();
        byte[] buf = new byte[BUF];
        long copied = 0L;
        int n;
        while ((n = in.read(buf)) >= 0) {
            if (cancel != null && cancel.isCancelled()) {
                throw new CancelledException();
            }
            if (n == 0) {
                continue;
            }
            md.update(buf, 0, n);
            out.write(buf, 0, n);
            copied += n;
            if (progress != null) {
                progress.onBytes(copied);
            }
        }
        return toHex(md.digest());
    }

    public static String hashStream(InputStream in) throws IOException {
        MessageDigest md = newDigest();
        byte[] buf = new byte[BUF];
        int n;
        while ((n = in.read(buf)) >= 0) {
            md.update(buf, 0, n);
        }
        return toHex(md.digest());
    }

    public static String toHex(byte[] digest) {
        StringBuilder sb = new StringBuilder(digest.length * 2);
        for (byte b : digest) {
            sb.append(String.format(java.util.Locale.ROOT, "%02x", b));
        }
        return sb.toString();
    }

    /** Test helper: wrap AtomicBoolean as CancelCheck. */
    public static CancelCheck fromAtomic(AtomicBoolean flag) {
        return flag::get;
    }
}
