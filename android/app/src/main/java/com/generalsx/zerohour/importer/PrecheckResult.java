package com.generalsx.zerohour.importer;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Outcome of comparing a SAF tree listing against the embedded manifest. */
public final class PrecheckResult {
    public enum Level { OK, WARN, ERROR }

    public final Level level;
    public final String profile;
    public final long bytesNeeded;
    public final long bytesFree;
    public final List<String> missingRequired;
    public final List<String> sizeMismatches;
    public final List<String> unknownHashKeys; // key archives with unexpected size (hash deferred)
    public final List<String> messagesEn;
    public final List<String> messagesRu;

    public PrecheckResult(Level level, String profile, long bytesNeeded, long bytesFree,
                          List<String> missingRequired, List<String> sizeMismatches,
                          List<String> unknownHashKeys,
                          List<String> messagesEn, List<String> messagesRu) {
        this.level = level;
        this.profile = profile;
        this.bytesNeeded = bytesNeeded;
        this.bytesFree = bytesFree;
        this.missingRequired = Collections.unmodifiableList(new ArrayList<>(missingRequired));
        this.sizeMismatches = Collections.unmodifiableList(new ArrayList<>(sizeMismatches));
        this.unknownHashKeys = Collections.unmodifiableList(new ArrayList<>(unknownHashKeys));
        this.messagesEn = Collections.unmodifiableList(new ArrayList<>(messagesEn));
        this.messagesRu = Collections.unmodifiableList(new ArrayList<>(messagesRu));
    }

    public boolean canStartImport() {
        return level != Level.ERROR;
    }
}
