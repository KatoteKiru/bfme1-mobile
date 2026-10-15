package com.generalsx.zerohour.importer;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.UriPermission;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import com.generalsx.zerohour.R;

import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Foreground service ({@code dataSync}) for {@link GameDataImporter}.
 *
 * <p>Always uses FGS (even if {@code POST_NOTIFICATIONS} is denied — the system
 * still shows the FGS notification on modern Android).
 *
 * <p>Android <strong>15+</strong> may stop dataSync FGS after ~6 hours
 * ({@link #onTimeout}); we cancel and rely on {@code import-state.json} resume.
 * (The 6h dataSync limit is an Android 15 behavior, not Android 14.)
 */
public final class GameDataImportService extends Service {
    private static final String TAG = "BfmeImport";
    public static final String ACTION_START = "com.generalsx.zerohour.importer.START";
    public static final String ACTION_CANCEL = "com.generalsx.zerohour.importer.CANCEL";
    public static final String EXTRA_TREE_URI = "tree_uri";

    public static final String ACTION_PROGRESS = "com.generalsx.zerohour.importer.PROGRESS";
    public static final String EXTRA_FILES_DONE = "files_done";
    public static final String EXTRA_FILES_TOTAL = "files_total";
    public static final String EXTRA_BYTES_DONE = "bytes_done";
    public static final String EXTRA_BYTES_TOTAL = "bytes_total";
    public static final String EXTRA_MESSAGE = "message";
    public static final String EXTRA_FINISHED = "finished";
    public static final String EXTRA_SUCCESS = "success";
    public static final String EXTRA_CANCELLED = "cancelled";
    public static final String EXTRA_CURRENT = "current";

    private static final String CHANNEL_ID = "bfme_gamedata_import";
    private static final int NOTIFICATION_ID = 1212;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicReference<GameDataImporter> running = new AtomicReference<>();
    private volatile boolean importStarted;

    public static void start(Context ctx, Uri treeUri) {
        Intent i = new Intent(ctx, GameDataImportService.class);
        i.setAction(ACTION_START);
        i.putExtra(EXTRA_TREE_URI, treeUri.toString());
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                ctx.startForegroundService(i);
            } else {
                ctx.startService(i);
            }
        } catch (RuntimeException e) {
            String name = e.getClass().getSimpleName();
            if (name.contains("ForegroundServiceStartNotAllowed") || name.contains("IllegalState")) {
                Log.e(TAG, "FGS start not allowed: " + e.getMessage());
                Intent fail = new Intent(ACTION_PROGRESS);
                fail.setPackage(ctx.getPackageName());
                fail.putExtra(EXTRA_FINISHED, true);
                fail.putExtra(EXTRA_SUCCESS, false);
                fail.putExtra(EXTRA_MESSAGE,
                        "Cannot start import in background — open the app and retry");
                ctx.sendBroadcast(fail);
                return;
            }
            throw e;
        }
    }

    public static void cancel(Context ctx) {
        Intent i = new Intent(ctx, GameDataImportService.class);
        i.setAction(ACTION_CANCEL);
        ctx.startService(i);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        String action = intent.getAction();
        if (ACTION_CANCEL.equals(action)) {
            GameDataImporter imp = running.get();
            if (imp != null) {
                imp.requestCancel();
            }
            return START_NOT_STICKY;
        }
        if (!ACTION_START.equals(action)) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (importStarted) {
            Log.i(TAG, "import already running");
            return START_NOT_STICKY;
        }
        boolean resumeFromState = false;
        String uriStr = intent.getStringExtra(EXTRA_TREE_URI);
        ImportState prior = ImportStateStore.load(this);
        if (uriStr == null) {
            // Resume after swipe / process death: URI from import-state + persisted grant.
            uriStr = prior.sourceTreeUri;
            resumeFromState = uriStr != null;
        } else if (ImportState.STATUS_IN_PROGRESS.equals(prior.status)
                || ImportState.STATUS_CANCELLED.equals(prior.status)) {
            resumeFromState = !prior.verifiedPaths().isEmpty();
        }
        if (uriStr == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        final Uri treeUri = Uri.parse(uriStr);
        if (!hasPersistedRead(treeUri)) {
            Log.e(TAG, "no persisted READ permission for " + treeUri);
            notifyTerminal("нет доступа к папке — выберите папку снова", false, false);
            stopSelf();
            return START_NOT_STICKY;
        }

        String notifState = ImportLogLines.NOTIF_GRANTED;
        if (Build.VERSION.SDK_INT >= 33) {
            boolean granted = ContextCompat.checkSelfPermission(this,
                    android.Manifest.permission.POST_NOTIFICATIONS)
                    == PackageManager.PERMISSION_GRANTED;
            notifState = granted ? ImportLogLines.NOTIF_GRANTED : ImportLogLines.NOTIF_DENIED;
        }
        // Stable QA line (a):
        Log.i(TAG, String.format(java.util.Locale.US, ImportLogLines.FGS_START, notifState));
        if (resumeFromState) {
            // Stable QA line (b):
            Log.i(TAG, String.format(java.util.Locale.US, ImportLogLines.RESUME,
                    prior.verifiedPaths().size(),
                    prior.sourceTreeUri == null ? uriStr : prior.sourceTreeUri));
        }

        ensureChannel();
        startAsForeground(buildNotification(
                getString(R.string.bfme_import_notif_title),
                getString(R.string.bfme_import_notif_starting),
                0, 0, 0L, 0L, true, true));

        importStarted = true;
        executor.execute(() -> runImport(treeUri));
        return START_NOT_STICKY;
    }

    /** Android 15+ dataSync timeout (~6 h). */
    @Override
    public void onTimeout(int startId, int fgsType) {
        Log.w(TAG, "onTimeout startId=" + startId + " type=" + fgsType
                + " — cancelling; resume via import-state (Android 15+ dataSync limit)");
        GameDataImporter imp = running.get();
        if (imp != null) {
            imp.requestCancel();
        }
        notifyTerminal("Import paused (system time limit) — tap Start to resume", false, true);
        stopSelf();
    }

    private boolean hasPersistedRead(Uri treeUri) {
        List<UriPermission> perms = getContentResolver().getPersistedUriPermissions();
        for (UriPermission p : perms) {
            if (p.getUri().equals(treeUri) && p.isReadPermission()) {
                return true;
            }
        }
        try {
            getContentResolver().getType(DocumentsContract.buildDocumentUriUsingTree(
                    treeUri, DocumentsContract.getTreeDocumentId(treeUri)));
            return true;
        } catch (SecurityException se) {
            return false;
        }
    }

    private void runImport(Uri treeUri) {
        GameDataImporter importer = new GameDataImporter(this);
        running.set(importer);
        long t0 = SystemClock.elapsedRealtime();
        try {
            byte[] raw = GameDataManifest.readAssetBytes(getAssets(), GameDataPaths.MANIFEST_ASSET);
            String manifestSha = GameDataManifest.sha256OfBytes(raw);
            GameDataManifest manifest = GameDataManifest.parse(new String(raw, java.nio.charset.StandardCharsets.UTF_8));
            Map<String, SafTreeEntry> tree;
            try {
                tree = SafTreeWalker.listFiles(getContentResolver(), treeUri);
            } catch (SafTreeWalker.AccessDeniedException ade) {
                broadcastProgress(new ImportProgress(0, 0, 0, 0, null, ade.getMessage(), true, false, false));
                notifyTerminal(ade.getMessage(), false, false);
                return;
            } catch (SafTreeWalker.CaseCollisionException cce) {
                broadcastProgress(new ImportProgress(0, 0, 0, 0, null, cce.getMessage(), true, false, false));
                notifyTerminal(cce.getMessage(), false, false);
                return;
            }
            ImportState prior = ImportStateStore.load(this);
            PrecheckResult pre = GameDataPrecheck.check(
                    manifest, tree, GameDataPaths.gameDataDir(this), prior);
            if (!pre.canStartImport()) {
                String msg = pre.messagesEn.isEmpty() ? "Precheck failed" : pre.messagesEn.get(0);
                broadcastProgress(new ImportProgress(0, 0, 0, 0, null, msg, true, false, false));
                notifyTerminal(msg, false, false);
                return;
            }
            ImportProgress result = importer.run(manifest, treeUri, tree, manifestSha, progress -> {
                broadcastProgress(progress);
                updateNotification(progress);
            });
            Log.i(TAG, "import finished success=" + result.success
                    + " cancelled=" + result.cancelled
                    + " in " + (SystemClock.elapsedRealtime() - t0) + "ms");
            notifyTerminal(result.message, result.success, result.cancelled);
        } catch (Exception e) {
            Log.e(TAG, "import failed", e);
            String msg = e.getMessage() == null ? e.toString() : e.getMessage();
            broadcastProgress(new ImportProgress(0, 0, 0, 0, null, msg, true, false, false));
            notifyTerminal(msg, false, false);
        } finally {
            running.set(null);
            importStarted = false;
            stopForeground(STOP_FOREGROUND_DETACH);
            stopSelf();
        }
    }

    private void startAsForeground(Notification n) {
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(NOTIFICATION_ID, n);
        }
    }

    private void broadcastProgress(ImportProgress p) {
        Intent i = new Intent(ACTION_PROGRESS);
        i.setPackage(getPackageName());
        i.putExtra(EXTRA_FILES_DONE, p.filesDone);
        i.putExtra(EXTRA_FILES_TOTAL, p.filesTotal);
        i.putExtra(EXTRA_BYTES_DONE, p.bytesDone);
        i.putExtra(EXTRA_BYTES_TOTAL, p.bytesTotal);
        i.putExtra(EXTRA_MESSAGE, p.message);
        i.putExtra(EXTRA_FINISHED, p.finished);
        i.putExtra(EXTRA_SUCCESS, p.success);
        i.putExtra(EXTRA_CANCELLED, p.cancelled);
        i.putExtra(EXTRA_CURRENT, p.currentPath);
        sendBroadcast(i);
    }

    private void ensureChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationChannel ch = new NotificationChannel(CHANNEL_ID,
                getString(R.string.bfme_import_notif_channel),
                NotificationManager.IMPORTANCE_LOW);
        ch.setDescription(getString(R.string.bfme_import_notif_channel_desc));
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) nm.createNotificationChannel(ch);
    }

    private Notification buildNotification(String title, String text,
                                           int filesDone, int filesTotal,
                                           long bytesDone, long bytesTotal,
                                           boolean indeterminate, boolean withCancel) {
        Intent open = new Intent(this, GameDataImportActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        String big = text;
        if (filesTotal > 0 || bytesTotal > 0) {
            big = String.format(Locale.US, "%s\n%d/%d files · %s / %s",
                    text == null ? "" : text, filesDone, filesTotal,
                    GameDataPrecheck.human(bytesDone), GameDataPrecheck.human(bytesTotal));
        }
        int pct = bytesTotal > 0 ? (int) Math.min(100, (bytesDone * 100L) / bytesTotal) : 0;

        NotificationCompat.Builder b = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle(title)
                .setContentText(String.format(Locale.US, "%d/%d · %s",
                        filesDone, filesTotal, GameDataPrecheck.human(bytesDone)))
                .setStyle(new NotificationCompat.BigTextStyle().bigText(big))
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentIntent(pi)
                .setOnlyAlertOnce(true)
                .setSilent(true)
                .setOngoing(withCancel && (indeterminate || pct < 100))
                .setProgress(100, pct, indeterminate && bytesTotal <= 0);
        if (withCancel) {
            Intent cancel = new Intent(this, GameDataImportService.class);
            cancel.setAction(ACTION_CANCEL);
            PendingIntent cancelPi = PendingIntent.getService(this, 1, cancel,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            b.addAction(0, getString(R.string.bfme_import_cancel), cancelPi);
        }
        return b.build();
    }

    private void updateNotification(ImportProgress p) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm == null) return;
        boolean ongoing = !p.finished;
        nm.notify(NOTIFICATION_ID, buildNotification(
                getString(R.string.bfme_import_notif_title),
                p.message == null ? "" : p.message,
                p.filesDone, p.filesTotal, p.bytesDone, p.bytesTotal,
                false, ongoing));
    }

    private void notifyTerminal(String text, boolean success, boolean cancelled) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm == null) return;
        // Final: not ongoing, no Cancel action (Lead #14)
        nm.notify(NOTIFICATION_ID, buildNotification(
                getString(R.string.bfme_import_notif_title),
                text == null ? "" : text,
                0, 0, 0L, 0L, false, false));
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        GameDataImporter imp = running.get();
        if (imp != null) {
            imp.requestCancel();
        }
        executor.shutdownNow();
        super.onDestroy();
    }
}
