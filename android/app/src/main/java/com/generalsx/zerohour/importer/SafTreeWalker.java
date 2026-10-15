package com.generalsx.zerohour.importer;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Walk a SAF document tree via {@link DocumentsContract}.
 * Index keys are {@link PathSafety#foldKey(String)} (Locale.ROOT lower-case).
 * Case-only collisions throw; {@link SecurityException} aborts with no partial map.
 */
public final class SafTreeWalker {
    private static final String TAG = "BfmeImport";

    private static final String[] PROJECTION = new String[] {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
    };

    public static final class AccessDeniedException extends RuntimeException {
        public AccessDeniedException(String msg, Throwable cause) {
            super(msg, cause);
        }
    }

    public static final class CaseCollisionException extends RuntimeException {
        public CaseCollisionException(String msg) {
            super(msg);
        }
    }

    private SafTreeWalker() {}

    /**
     * @return map foldedPath → entry (display name keeps original case from provider)
     */
    public static Map<String, SafTreeEntry> listFiles(ContentResolver cr, Uri treeUri) {
        Map<String, SafTreeEntry> out = new LinkedHashMap<>();
        if (cr == null || treeUri == null) {
            return out;
        }
        String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
        ArrayDeque<DirJob> queue = new ArrayDeque<>();
        queue.add(new DirJob(treeDocId, ""));

        try {
            while (!queue.isEmpty()) {
                DirJob job = queue.removeFirst();
                Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, job.docId);
                try (Cursor c = cr.query(children, PROJECTION, null, null, null)) {
                    if (c == null) {
                        ImportLog.w(TAG, "SAF query returned null for " + job.relPrefix);
                        continue;
                    }
                    int iId = c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
                    int iName = c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                    int iMime = c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_MIME_TYPE);
                    int iSize = c.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_SIZE);
                    while (c.moveToNext()) {
                        String docId = c.getString(iId);
                        String name = c.getString(iName);
                        String mime = c.getString(iMime);
                        long size = c.isNull(iSize) ? -1L : c.getLong(iSize);
                        if (name == null || name.isEmpty() || ".".equals(name) || "..".equals(name)) {
                            continue;
                        }
                        if ("MANIFEST.sha256".equals(name) || "COPY_LOG.md".equals(name)
                                || name.endsWith(".copytool")) {
                            continue;
                        }
                        boolean isDir = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
                        String rel = job.relPrefix.isEmpty() ? name : job.relPrefix + "/" + name;
                        rel = GameDataManifest.normalizePath(rel);
                        Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
                        if (isDir) {
                            queue.addLast(new DirJob(docId, rel));
                        } else {
                            String fold;
                            try {
                                fold = PathSafety.foldKey(rel);
                            } catch (IllegalArgumentException bad) {
                                ImportLog.w(TAG, "skip unsafe tree path: " + rel);
                                continue;
                            }
                            SafTreeEntry prev = out.put(fold,
                                    new SafTreeEntry(rel, name, docUri.toString(), size, false));
                            if (prev != null && !prev.relativePath.equals(rel)
                                    && prev.relativePath.toLowerCase(Locale.ROOT).equals(fold)) {
                                throw new CaseCollisionException(
                                        "case-only collision: " + prev.relativePath + " vs " + rel);
                            }
                        }
                    }
                }
            }
        } catch (SecurityException se) {
            throw new AccessDeniedException("нет доступа к папке / no access to folder", se);
        }
        ImportLog.i(TAG, "SAF tree listed " + out.size() + " files under " + treeUri);
        return out;
    }

    private static final class DirJob {
        final String docId;
        final String relPrefix;

        DirJob(String docId, String relPrefix) {
            this.docId = docId;
            this.relPrefix = relPrefix;
        }
    }
}
