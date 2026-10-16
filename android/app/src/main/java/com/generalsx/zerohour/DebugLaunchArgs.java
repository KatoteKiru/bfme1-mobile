package com.generalsx.zerohour;

// BFME: debug-args. Engine command-line arguments from the `args` intent extra
// (docs/debug-launch.md). Pure Java (JVM-tested): no android.* here.
//
// Tokenizing: whitespace separates tokens; '...' and "..." group text with spaces; quoted parts
// next to plain text join into one token ("a"'b'c -> abc; "" -> empty token). There is no escape
// character and no expansion: a backslash is an ordinary character, so SAGE paths such as
// maps\foo\foo.map pass through unchanged. Unbalanced quotes, empty input or input over
// MAX_CHARS -> a warning and no extra arguments; never an exception.

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

final class DebugLaunchArgs {
    private DebugLaunchArgs() {}

    static final String EXTRA_ARGS = "args";
    static final String TAG = "BFME-args";
    static final int MAX_CHARS = 4096;

    interface Logger {
        void log(boolean warning, String message);
    }

    /** Tokens, or null when the quotes are unbalanced. */
    static List<String> tokenize(String s) {
        List<String> out = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean inToken = false;
        char quote = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (quote != 0) {
                if (c == quote) quote = 0; else cur.append(c);
            } else if (c == '\'' || c == '"') {
                quote = c;
                inToken = true;
            } else if (Character.isWhitespace(c)) {
                if (inToken) { out.add(cur.toString()); cur.setLength(0); inToken = false; }
            } else {
                cur.append(c);
                inToken = true;
            }
        }
        if (quote != 0) return null;
        if (inToken) out.add(cur.toString());
        return out;
    }

    /**
     * Extra arguments to append after the defaults. raw == null (no extra): nothing, no log.
     * Disabled build, empty/blank, too long or unbalanced input: one warning, nothing.
     * Accepted input: no log (the caller logs the final argv).
     */
    static List<String> resolve(String raw, boolean enabled, Logger log) {
        if (raw == null) return Collections.emptyList();
        if (!enabled) {
            log.log(true, "args extra ignored: built with bfmeDevArgs=false");
            return Collections.emptyList();
        }
        if (raw.trim().isEmpty()) {
            log.log(true, "args extra is empty; launching without extra arguments");
            return Collections.emptyList();
        }
        if (raw.length() > MAX_CHARS) {
            log.log(true, "args extra longer than " + MAX_CHARS + " chars; launching without extra arguments");
            return Collections.emptyList();
        }
        List<String> t = tokenize(raw);
        if (t == null) {
            log.log(true, "args extra has unbalanced quotes; launching without extra arguments: " + raw);
            return Collections.emptyList();
        }
        // Success is not logged here: GeneralsZHActivity logs the final argv once (tag BFME-args).
        return t;
    }
}
