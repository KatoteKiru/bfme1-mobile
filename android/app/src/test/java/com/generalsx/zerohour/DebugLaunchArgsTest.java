package com.generalsx.zerohour;

// BFME: debug-args JVM tests.

import static org.junit.Assert.*;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public class DebugLaunchArgsTest {
    private final List<String> logs = new ArrayList<>();
    private final DebugLaunchArgs.Logger log = (w, m) -> logs.add((w ? "W " : "I ") + m);

    private static List<String> l(String... s) { return Arrays.asList(s); }

    @Test public void plainAndSpaces() {
        assertEquals(l("-noshellmap", "-xres", "800"), DebugLaunchArgs.tokenize("  -noshellmap   -xres\t800 "));
    }

    @Test public void quotes() {
        assertEquals(l("-file", "maps\\my map\\my map.map"), DebugLaunchArgs.tokenize("-file \"maps\\my map\\my map.map\""));
        assertEquals(l("a b", "c\"d"), DebugLaunchArgs.tokenize("'a b' 'c\"d'"));
        assertEquals(l("abc"), DebugLaunchArgs.tokenize("\"a\"'b'c"));
        assertEquals(l("", "x"), DebugLaunchArgs.tokenize("\"\" x"));
    }

    @Test public void backslashIsLiteral() {
        assertEquals(l("-file", "maps\\foo\\foo.map"), DebugLaunchArgs.tokenize("-file maps\\foo\\foo.map"));
        assertEquals(l("a\\", "b"), DebugLaunchArgs.tokenize("a\\ b"));
        assertEquals(l("x\\\"y"), DebugLaunchArgs.tokenize("'x\\\"y'"));
    }

    @Test public void unbalancedIsNull() {
        assertNull(DebugLaunchArgs.tokenize("-file \"maps\\foo"));
        assertNull(DebugLaunchArgs.tokenize("'"));
    }

    @Test public void resolveNoExtraIsSilent() {
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve(null, true, log));
        assertTrue(logs.isEmpty());
    }

    @Test public void resolveEmptyAndUnbalancedWarn() {
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve("", true, log));
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve("   ", true, log));
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve("-file 'x", true, log));
        assertEquals(3, logs.size());
        for (String s : logs) assertTrue(s, s.startsWith("W "));
    }

    @Test public void resolveMaxLength() {
        StringBuilder ok = new StringBuilder();
        while (ok.length() < DebugLaunchArgs.MAX_CHARS) ok.append('a');
        assertEquals(1, DebugLaunchArgs.resolve(ok.toString(), true, log).size());
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve(ok + "a", true, log));
        assertTrue(logs.size() == 1 && logs.get(0).startsWith("W "));
    }

    @Test public void resolveGate() {
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve("-noshellmap", false, log));
        assertEquals(1, logs.size());
        assertTrue(logs.get(0).startsWith("W "));
        assertEquals(l("-noshellmap", "-file", "maps\\foo\\foo.map"),
            DebugLaunchArgs.resolve("-noshellmap -file maps\\foo\\foo.map", true, log));
        assertEquals("success is not logged (the caller logs the final argv)", 1, logs.size());
    }

    @Test public void quotedAtLimit() {
        StringBuilder inner = new StringBuilder();
        while (inner.length() < DebugLaunchArgs.MAX_CHARS - 2) inner.append(inner.length() % 7 == 3 ? ' ' : 'x');
        String atLimit = "\"" + inner + "\"";
        assertEquals(DebugLaunchArgs.MAX_CHARS, atLimit.length());
        assertEquals(l(inner.toString()), DebugLaunchArgs.resolve(atLimit, true, log));
        assertTrue(logs.isEmpty());

        String over = "\"" + inner + "x\"";
        assertEquals(DebugLaunchArgs.MAX_CHARS + 1, over.length());
        assertEquals(Collections.emptyList(), DebugLaunchArgs.resolve(over, true, log));
        assertEquals(1, logs.size());
        assertTrue(logs.get(0), logs.get(0).startsWith("W ") && logs.get(0).contains("longer than"));
    }
}
