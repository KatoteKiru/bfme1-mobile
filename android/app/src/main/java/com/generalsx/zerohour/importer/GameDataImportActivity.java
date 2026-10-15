package com.generalsx.zerohour.importer;

import android.Manifest;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.generalsx.zerohour.LogViewerActivity;
import com.generalsx.zerohour.R;
import com.generalsx.zerohour.SetupActivity;

import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** «Нужны файлы BFME1» — SAF import UI. Always starts dataSync FGS for copy. */
public final class GameDataImportActivity extends Activity {
    private static final String TAG = "BfmeImport";
    private static final int REQ_OPEN_TREE = 12120;
    private static final int REQ_POST_NOTIFICATIONS = 12121;

    private TextView statusView;
    private TextView detailView;
    private ProgressBar progressBar;
    private Button chooseButton;
    private Button startButton;
    private Button cancelButton;
    private Button playButton;
    private Button verifyButton;

    private Uri treeUri;
    private GameDataManifest manifest;
    private PrecheckResult lastPrecheck;
    private boolean pendingStartAfterNotif;
    private final ExecutorService verifyExec = Executors.newSingleThreadExecutor();

    private final BroadcastReceiver progressReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent == null || !GameDataImportService.ACTION_PROGRESS.equals(intent.getAction())) {
                return;
            }
            applyProgress(
                    intent.getIntExtra(GameDataImportService.EXTRA_FILES_DONE, 0),
                    intent.getIntExtra(GameDataImportService.EXTRA_FILES_TOTAL, 0),
                    intent.getLongExtra(GameDataImportService.EXTRA_BYTES_DONE, 0L),
                    intent.getLongExtra(GameDataImportService.EXTRA_BYTES_TOTAL, 0L),
                    intent.getStringExtra(GameDataImportService.EXTRA_MESSAGE),
                    intent.getBooleanExtra(GameDataImportService.EXTRA_FINISHED, false),
                    intent.getBooleanExtra(GameDataImportService.EXTRA_SUCCESS, false),
                    intent.getBooleanExtra(GameDataImportService.EXTRA_CANCELLED, false));
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        try {
            byte[] raw = GameDataManifest.readAssetBytes(getAssets(), GameDataPaths.MANIFEST_ASSET);
            manifest = GameDataManifest.parse(new String(raw, java.nio.charset.StandardCharsets.UTF_8));
            statusView.setText(getString(R.string.bfme_import_status_need_files,
                    manifest.displayLabel(),
                    GameDataPrecheck.human(manifest.requiredBytes()),
                    GameDataPrecheck.human(freeBytesSafe())));
        } catch (Exception e) {
            statusView.setText(getString(R.string.bfme_import_status_manifest_error, e.getMessage()));
            Log.e(TAG, "manifest load failed", e);
        }
        if (GameDataReady.isReady(this)) {
            statusView.setText(R.string.bfme_import_status_already_ready);
            playButton.setEnabled(true);
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter(GameDataImportService.ACTION_PROGRESS);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(progressReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(progressReceiver, filter);
        }
    }

    @Override
    protected void onStop() {
        try { unregisterReceiver(progressReceiver); } catch (IllegalArgumentException ignored) {}
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        verifyExec.shutdownNow();
        super.onDestroy();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.bfme_import_title);
        title.setTextSize(22f);
        title.setPadding(0, 0, 0, pad);
        root.addView(title);

        statusView = new TextView(this);
        statusView.setTextSize(15f);
        root.addView(statusView);

        detailView = new TextView(this);
        detailView.setTextSize(13f);
        detailView.setPadding(0, pad / 2, 0, pad / 2);
        root.addView(detailView);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        root.addView(progressBar);

        chooseButton = addBtn(root, R.string.bfme_import_btn_choose_folder, this::onChooseFolder);
        addBtn(root, R.string.bfme_import_btn_howto, () -> detailView.setText(R.string.bfme_import_howto_body));
        addBtn(root, R.string.bfme_import_btn_logs, () ->
                startActivity(new Intent(this, LogViewerActivity.class)));
        verifyButton = addBtn(root, R.string.bfme_import_btn_verify, this::onVerifyInPlace);
        startButton = addBtn(root, R.string.bfme_import_btn_start, this::onStartImport);
        startButton.setEnabled(false);
        cancelButton = addBtn(root, R.string.bfme_import_cancel, () -> GameDataImportService.cancel(this));
        cancelButton.setEnabled(false);
        playButton = addBtn(root, R.string.bfme_import_btn_play, this::onPlay);
        playButton.setEnabled(false);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
        setTitle(R.string.bfme_import_title);
    }

    private Button addBtn(LinearLayout root, int labelRes, Runnable action) {
        Button b = new Button(this);
        b.setText(labelRes);
        b.setOnClickListener(v -> action.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 8;
        root.addView(b, lp);
        return b;
    }

    private void onChooseFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(
                    "content://com.android.externalstorage.documents/document/primary%3ADownload"));
        }
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQ_OPEN_TREE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_OPEN_TREE || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException se) {
            Log.w(TAG, "takePersistableUriPermission: " + se.getMessage());
        }
        treeUri = uri;
        runPrecheckAsync();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_POST_NOTIFICATIONS && pendingStartAfterNotif) {
            pendingStartAfterNotif = false;
            // Denial does not block FGS (Lead #4)
            reallyStartFgs();
        }
    }

    private void runPrecheckAsync() {
        statusView.setText(R.string.bfme_import_status_prechecking);
        progressBar.setIndeterminate(true);
        startButton.setEnabled(false);
        new Thread(() -> {
            try {
                if (manifest == null) {
                    byte[] raw = GameDataManifest.readAssetBytes(getAssets(), GameDataPaths.MANIFEST_ASSET);
                    manifest = GameDataManifest.parse(new String(raw, java.nio.charset.StandardCharsets.UTF_8));
                }
                Map<String, SafTreeEntry> tree = SafTreeWalker.listFiles(getContentResolver(), treeUri);
                ImportState prior = ImportStateStore.load(this);
                lastPrecheck = GameDataPrecheck.check(
                        manifest, tree, GameDataPaths.gameDataDir(this), prior);
                runOnUiThread(this::showPrecheck);
            } catch (SafTreeWalker.AccessDeniedException ade) {
                runOnUiThread(() -> {
                    progressBar.setIndeterminate(false);
                    statusView.setText(ade.getMessage());
                });
            } catch (Exception e) {
                Log.e(TAG, "precheck failed", e);
                runOnUiThread(() -> {
                    progressBar.setIndeterminate(false);
                    statusView.setText(getString(R.string.bfme_import_status_failed, e.getMessage()));
                });
            }
        }, "bfme-precheck").start();
    }

    private void showPrecheck() {
        progressBar.setIndeterminate(false);
        boolean ru = Locale.getDefault().getLanguage().startsWith("ru");
        StringBuilder sb = new StringBuilder();
        for (String m : (ru ? lastPrecheck.messagesRu : lastPrecheck.messagesEn)) {
            if (sb.length() > 0) sb.append('\n');
            sb.append(m);
        }
        detailView.setText(sb.toString());
        switch (lastPrecheck.level) {
            case OK:
                statusView.setText(getString(R.string.bfme_import_status_precheck_ok,
                        manifest != null ? manifest.displayLabel() : lastPrecheck.profile));
                startButton.setEnabled(true);
                break;
            case WARN:
                statusView.setText(getString(R.string.bfme_import_status_precheck_warn,
                        manifest != null ? manifest.displayLabel() : lastPrecheck.profile));
                startButton.setEnabled(true);
                break;
            default:
                statusView.setText(R.string.bfme_import_status_precheck_err);
                startButton.setEnabled(false);
                break;
        }
    }

    private void onStartImport() {
        if (treeUri == null || lastPrecheck == null || !lastPrecheck.canStartImport()) {
            Toast.makeText(this, R.string.bfme_import_toast_pick_first, Toast.LENGTH_LONG).show();
            return;
        }
        if (Build.VERSION.SDK_INT >= 33
                && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            pendingStartAfterNotif = true;
            ActivityCompat.requestPermissions(this,
                    new String[] { Manifest.permission.POST_NOTIFICATIONS },
                    REQ_POST_NOTIFICATIONS);
            return;
        }
        reallyStartFgs();
    }

    private void reallyStartFgs() {
        cancelButton.setEnabled(true);
        startButton.setEnabled(false);
        chooseButton.setEnabled(false);
        playButton.setEnabled(false);
        progressBar.setIndeterminate(true);
        statusView.setText(R.string.bfme_import_status_running);
        GameDataImportService.start(this, treeUri);
    }

    private void onVerifyInPlace() {
        statusView.setText(R.string.bfme_import_status_verifying);
        progressBar.setIndeterminate(true);
        verifyButton.setEnabled(false);
        verifyExec.execute(() -> {
            try {
                ImportProgress r = GameDataVerifier.verifyInPlace(this, p ->
                        runOnUiThread(() -> applyProgress(p.filesDone, p.filesTotal,
                                p.bytesDone, p.bytesTotal, p.message, p.finished, p.success, false)));
                runOnUiThread(() -> {
                    verifyButton.setEnabled(true);
                    if (r.success) playButton.setEnabled(true);
                });
            } catch (Exception e) {
                Log.e(TAG, "verify failed", e);
                runOnUiThread(() -> {
                    verifyButton.setEnabled(true);
                    progressBar.setIndeterminate(false);
                    statusView.setText(getString(R.string.bfme_import_status_failed, e.getMessage()));
                });
            }
        });
    }

    private void applyProgress(int filesDone, int filesTotal, long bytesDone, long bytesTotal,
                               String message, boolean finished, boolean success, boolean cancelled) {
        int pct = bytesTotal > 0 ? (int) Math.min(100, (bytesDone * 100) / bytesTotal) : 0;
        progressBar.setIndeterminate(false);
        progressBar.setMax(100);
        progressBar.setProgress(pct);
        detailView.setText(String.format(Locale.US, "%s\n%d / %d files\n%s / %s",
                message == null ? "" : message, filesDone, filesTotal,
                GameDataPrecheck.human(bytesDone), GameDataPrecheck.human(bytesTotal)));
        if (finished) {
            cancelButton.setEnabled(false);
            startButton.setEnabled(true);
            chooseButton.setEnabled(true);
            if (success) {
                statusView.setText(getString(R.string.bfme_import_status_done,
                        manifest != null ? manifest.displayLabel() : "",
                        filesDone));
                playButton.setEnabled(true);
            } else if (cancelled) {
                statusView.setText(R.string.bfme_import_status_cancelled);
            } else {
                statusView.setText(getString(R.string.bfme_import_status_failed,
                        message == null ? "" : message));
            }
        }
    }

    private void onPlay() {
        if (!GameDataReady.isReady(this)) {
            Toast.makeText(this, R.string.bfme_import_toast_not_ready, Toast.LENGTH_LONG).show();
            return;
        }
        Intent i = new Intent(this, SetupActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(i);
        finish();
    }

    private long freeBytesSafe() {
        try {
            return GameDataPrecheck.freeBytesOrThrow(GameDataPaths.externalRoot(this));
        } catch (RuntimeException e) {
            return -1L;
        }
    }
}
