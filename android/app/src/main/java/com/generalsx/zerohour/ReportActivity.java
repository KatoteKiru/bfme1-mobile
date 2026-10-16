package com.generalsx.zerohour;

// BFME: E1-15. «Отправить отчёт» (checklist 1.14/1.15): list of our own log files with check boxes,
// "Send" (ACTION_SEND via FileProvider) and "Save to file" (ACTION_CREATE_DOCUMENT). The zip is
// built in cache/reports. Only diagnostics are offered (ReportFormat allowlist), never game files.

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class ReportActivity extends Activity {
    static final String EXTRA_REASON = "reason";
    private static final int REQ_SAVE = 4115;

    private final List<CheckBox> boxes = new ArrayList<>();
    private final List<ReportFormat.Entry> entries = new ArrayList<>();
    private CheckBox logcatBox;
    private TextView status;
    private File lastZip;
    private String reason;
    private Button sendButton;
    private Button saveButton;
    private volatile boolean destroyed;
    static final String EXTRA_SAVE = "save";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setTitle(R.string.bfme_report_title);
        reason = getIntent().getStringExtra(EXTRA_REASON);
        if (reason == null) reason = "manual";

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);
        scroll.addView(root);
        setContentView(scroll);
        InsetUtil.applySafeInsets(scroll);

        TextView intro = new TextView(this);
        intro.setText(R.string.bfme_report_intro);
        root.addView(intro);

        for (ReportFormat.Entry e : BfmeDiagnostics.candidateFiles(this)) {
            CheckBox cb = new CheckBox(this);
            boolean present = e.file != null && e.file.isFile();
            cb.setText(present ? e.name + " (" + Math.max(1, e.file.length() / 1024) + " KB)"
                               : getString(R.string.bfme_report_absent, e.name));
            cb.setChecked(present);
            cb.setEnabled(present);
            root.addView(cb);
            boxes.add(cb);
            entries.add(e);
        }
        logcatBox = new CheckBox(this);
        logcatBox.setText(R.string.bfme_report_logcat);
        logcatBox.setChecked(true);
        root.addView(logcatBox);

        Button send = sendButton = new Button(this);
        send.setText(R.string.bfme_report_send);
        send.setOnClickListener(v -> build(false));
        root.addView(send);
        Button save = saveButton = new Button(this);
        save.setText(R.string.bfme_report_save);
        save.setOnClickListener(v -> build(true));
        root.addView(save);

        status = new TextView(this);
        status.setTextIsSelectable(true);
        root.addView(status);

        // Crash offer «Сохранить в файл» goes straight to the SAF picker.
        if (savedInstanceState == null && getIntent().getBooleanExtra(EXTRA_SAVE, false)) {
            build(true);
        }
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        super.onDestroy();
    }

    private void setBusy(boolean busy) {
        sendButton.setEnabled(!busy);
        saveButton.setEnabled(!busy);
    }

    private void build(boolean saveToFile) {
        status.setText(R.string.bfme_report_building);
        setBusy(true);
        final List<ReportFormat.Entry> chosen = new ArrayList<>();
        for (int i = 0; i < boxes.size(); i++) {
            if (boxes.get(i).isChecked()) chosen.add(entries.get(i));
        }
        final boolean withLogcat = logcatBox.isChecked();
        new Thread(() -> {
            try {
                if (withLogcat) {
                    chosen.add(new ReportFormat.Entry("logcat-pid.txt", BfmeDiagnostics.captureLogcat(true)));
                    chosen.add(new ReportFormat.Entry("logcat-app.txt", BfmeDiagnostics.captureLogcat(false)));
                }
                File dir = new File(getCacheDir(), "reports");
                File[] old = dir.listFiles();
                if (old != null) for (File f : old) f.delete();
                dir.mkdirs();
                File zip = new File(dir, ReportFormat.reportFileName(BfmeDiagnostics.versionName(this),
                    reason, System.currentTimeMillis(), BfmeDiagnostics.SESSION));
                int n;
                try (OutputStream out = new FileOutputStream(zip)) {
                    n = ReportFormat.writeZip(out, chosen);
                }
                final int count = n;
                runOnUiThread(() -> {
                    if (destroyed || isFinishing()) return;
                    setBusy(false);
                    onZipReady(zip, count, saveToFile);
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    if (destroyed || isFinishing()) return;
                    setBusy(false);
                    status.setText(getString(R.string.bfme_report_failed, String.valueOf(e)));
                });
            }
        }, "bfme-report").start();
    }

    private void onZipReady(File zip, int count, boolean saveToFile) {
        lastZip = zip;
        status.setText(getString(R.string.bfme_report_ready, zip.getName(), count, zip.length() / 1024));
        if (saveToFile) {
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/zip");
            i.putExtra(Intent.EXTRA_TITLE, zip.getName());
            startActivityForResult(i, REQ_SAVE);
            return;
        }
        Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", zip);
        Intent share = new Intent(Intent.ACTION_SEND);
        share.setType("application/zip");
        share.putExtra(Intent.EXTRA_STREAM, uri);
        share.putExtra(Intent.EXTRA_SUBJECT, zip.getName());
        share.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(Intent.createChooser(share, getString(R.string.bfme_report_send)));
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_SAVE || resultCode != RESULT_OK || data == null || data.getData() == null || lastZip == null) {
            return;
        }
        try (InputStream in = new FileInputStream(lastZip);
             OutputStream out = getContentResolver().openOutputStream(data.getData())) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            Toast.makeText(this, R.string.bfme_report_saved, Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            status.setText(getString(R.string.bfme_report_failed, String.valueOf(e)));
        }
    }
}
