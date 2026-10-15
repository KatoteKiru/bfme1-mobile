package com.generalsx.zerohour.importer;

/** Thin log wrapper so JVM unit tests do not need android.util.Log. */
final class ImportLog {
    private ImportLog() {}

    static void i(String tag, String msg) {
        try {
            android.util.Log.i(tag, msg);
        } catch (Throwable t) {
            System.out.println("I/" + tag + ": " + msg);
        }
    }

    static void w(String tag, String msg) {
        try {
            android.util.Log.w(tag, msg);
        } catch (Throwable t) {
            System.out.println("W/" + tag + ": " + msg);
        }
    }

    static void e(String tag, String msg) {
        try {
            android.util.Log.e(tag, msg);
        } catch (Throwable t) {
            System.err.println("E/" + tag + ": " + msg);
        }
    }

    static void e(String tag, String msg, Throwable tr) {
        try {
            android.util.Log.e(tag, msg, tr);
        } catch (Throwable t) {
            System.err.println("E/" + tag + ": " + msg);
            if (tr != null) {
                tr.printStackTrace(System.err);
            }
        }
    }
}
