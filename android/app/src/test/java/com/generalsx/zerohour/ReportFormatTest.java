package com.generalsx.zerohour;

// BFME: E1-15 JVM tests for the report zip builder and metadata formatting.

import static org.junit.Assert.*;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class ReportFormatTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    private File file(String name, String content) throws Exception {
        File f = tmp.newFile(name);
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(content.getBytes(StandardCharsets.UTF_8)); }
        return f;
    }

    @Test public void fileNameHasAllPartsAndIsSanitized() {
        String n = ReportFormat.reportFileName("0.1.0-abc1234", "crash", 0L, "s/1 2");
        assertTrue(n, n.matches("bfme-report_0\\.1\\.0-abc1234_crash_\\d{8}-\\d{6}_s-1-2\\.zip"));
    }

    @Test public void allowlistRejectsGameFiles() {
        for (String ok : Arrays.asList("crash.log", "crash-prev.log", "generals-stderr.log", "generals-stderr-prev.log",
                "startup-meta.txt", "native-smoke.txt", "import-state.json", "import.log", "logcat-pid.txt")) {
            assertTrue(ok, ReportFormat.isAllowedEntry(ok));
        }
        for (String bad : Arrays.asList("ini.big", "maps.big", "Data/INI/GameData.ini", "english.big", "lang.str",
                "options.ini", "../crash.log", "crash.log.big", null)) {
            assertFalse(String.valueOf(bad), ReportFormat.isAllowedEntry(bad));
        }
    }

    @Test public void zipContainsOnlyAllowedPresentFilesAndIndex() throws Exception {
        List<ReportFormat.Entry> e = new ArrayList<>();
        e.add(new ReportFormat.Entry("crash.log", file("crash.log", "=== NATIVE CRASH === signal=11")));
        e.add(new ReportFormat.Entry("crash-prev.log", new File(tmp.getRoot(), "missing")));
        e.add(new ReportFormat.Entry("ini.big", file("ini.big", "BIGF")));
        e.add(new ReportFormat.Entry("logcat-pid.txt", "line".getBytes(StandardCharsets.UTF_8)));
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        assertEquals(2, ReportFormat.writeZip(bos, e));
        Map<String, String> got = new LinkedHashMap<>();
        try (ZipInputStream z = new ZipInputStream(new ByteArrayInputStream(bos.toByteArray()))) {
            ZipEntry ze;
            while ((ze = z.getNextEntry()) != null) got.put(ze.getName(), new String(ReportFormat.readAll(z), StandardCharsets.UTF_8));
        }
        assertEquals(Arrays.asList("crash.log", "logcat-pid.txt", "report-index.txt"), new ArrayList<>(got.keySet()));
        String index = got.get("report-index.txt");
        assertTrue(index, index.contains("absent: crash-prev.log"));
        assertTrue(index, index.contains("skipped (not allowed): ini.big"));
    }

    @Test public void selftestLogIsAllowedAndOffered() throws Exception {
        assertTrue(ReportFormat.isAllowedEntry("crash-selftest.log"));
        assertFalse(ReportFormat.isAllowedEntry("crash-selftest.big"));
        File self = file("crash-selftest.log", "=== BFME crash-selftest: raising SIGSEGV ===\n=== NATIVE CRASH === signal=11\n");
        assertEquals(self.lastModified(), ReportFormat.newestCrashTime(new File(tmp.getRoot(), "crash.log"), self));
    }

    @Test public void metadataFormatting() {
        Map<String, String> m = new LinkedHashMap<>();
        m.put("versionName", "0.1.0-abc1234");
        m.put("soc", null);
        m.put("multi", "a\nb");
        assertEquals("versionName: 0.1.0-abc1234\nsoc: n/a\nmulti: a b\n", ReportFormat.formatMetadata(m));
    }

    @Test public void gitSha() {
        assertEquals("62ba2d1", ReportFormat.gitShaFromVersionName("0.1.0-62ba2d1"));
        assertNull(ReportFormat.gitShaFromVersionName("1.0.0"));
        assertNull(ReportFormat.gitShaFromVersionName(null));
    }

    @Test public void crashOffer() throws Exception {
        File crash = file("c.log", "x\n=== NATIVE CRASH === signal=11 (Segmentation fault)\n");
        File clean = file("d.log", "=== libmain.so loaded, crash handler installed ===\n");
        assertTrue(ReportFormat.containsCrashMarker(crash));
        assertFalse(ReportFormat.containsCrashMarker(clean));
        long t = ReportFormat.newestCrashTime(crash, clean);
        assertEquals(crash.lastModified(), t);
        assertTrue(ReportFormat.shouldOfferReport(t, 0));
        assertFalse(ReportFormat.shouldOfferReport(t, t));
        assertFalse(ReportFormat.shouldOfferReport(0, 0));
    }
}
