package com.generalsx.zerohour.importer;

/**
 * Stable log lines for QA / Lead (E1-12a). Tag is always {@code BfmeImport}.
 * Do not rephrase without coordinating with QA checklist.
 */
public final class ImportLogLines {
    private ImportLogLines() {}

    /** (a) FGS start — %s = granted|denied */
    public static final String FGS_START =
            "FGS start type=dataSync POST_NOTIFICATIONS=%s";

    /** (b) Resume after process death / swipe — %d verified count, %s uri */
    public static final String RESUME =
            "resuming import from import-state (verified=%d sourceTreeUri=%s)";

    /** (c) Bundled runtime file — %s name, %d size, %s = copied|already present */
    public static final String BUNDLED_FILE =
            "bundled %s size=%d %s";

    public static final String STATUS_COPIED = "copied";
    public static final String STATUS_ALREADY_PRESENT = "already present";
    public static final String NOTIF_GRANTED = "granted";
    public static final String NOTIF_DENIED = "denied";
}
