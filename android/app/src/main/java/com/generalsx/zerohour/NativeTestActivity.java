package com.generalsx.zerohour;

// BFME: E1-15. Runs in its own ":nativetest" process (AndroidManifest.xml), so neither test can
// take the launcher down and libmain.so is unloaded with the process afterwards.
//  - MODE_SMOKE (checklist 1.16): loads libmain.so (its ELF constructor installs the crash
//    handler), calls nativeInfo() over JNI, creates an EGL pbuffer surface + GLES context, logs
//    device / GL_VENDOR / GL_RENDERER / GL_VERSION / surface to native-smoke.txt and logcat and
//    exits normally. No game data needed.
//  - MODE_CRASH (checklist 1.15): loads libmain.so and raises SIGSEGV in native code; the handler
//    writes crash.log and the launcher offers «Отправить отчёт» on its next resume.

import android.app.Activity;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

public class NativeTestActivity extends Activity {
    static final String EXTRA_MODE = "mode";
    static final String MODE_SMOKE = "smoke";
    static final String MODE_CRASH = "crash";
    private static final String TAG = "BFME-smoke";

    static native String nativeInfo();
    static native void nativeCrash();

    private TextView text;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        text = new TextView(this);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        text.setPadding(pad, pad, pad, pad);
        text.setTextIsSelectable(true);
        setContentView(text);
        InsetUtil.applySafeInsets(text);
        String mode = getIntent().getStringExtra(EXTRA_MODE);
        setTitle(MODE_CRASH.equals(mode) ? R.string.bfme_selftest_title : R.string.bfme_smoke_title);
        text.setText(R.string.bfme_smoke_running);
        new Thread(() -> run(mode), "bfme-nativetest").start();
    }

    private void run(String mode) {
        Map<String, String> m = new LinkedHashMap<>();
        m.put("result", "running");
        m.put("versionName", BfmeDiagnostics.versionName(this));
        m.put("device", Build.MANUFACTURER + " " + Build.MODEL);
        if (Build.VERSION.SDK_INT >= 31) m.put("soc", Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL);
        m.put("android", Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")");
        try {
            System.loadLibrary("main");
            m.put("libmain", "loaded");
            m.put("native", nativeInfo());
        } catch (Throwable t) {
            m.put("libmain", "FAILED: " + t);
            m.put("result", "FAIL");
            finish(m);
            return;
        }
        if (MODE_CRASH.equals(mode)) {
            Log.w(TAG, "crash-selftest: raising SIGSEGV in native code");
            nativeCrash();
            return; // not reached
        }
        boolean egl = probeEgl(m);
        m.put("result", egl ? "OK" : "FAIL");
        finish(m);
    }

    private static boolean probeEgl(Map<String, String> m) {
        EGLDisplay dpy = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (dpy == EGL14.EGL_NO_DISPLAY) { m.put("egl", "eglGetDisplay failed"); return false; }
        int[] ver = new int[2];
        if (!EGL14.eglInitialize(dpy, ver, 0, ver, 1)) { m.put("egl", "eglInitialize failed 0x" + Integer.toHexString(EGL14.eglGetError())); return false; }
        EGLSurface surf = EGL14.EGL_NO_SURFACE;
        EGLContext ctx = EGL14.EGL_NO_CONTEXT;
        try {
            m.put("egl", ver[0] + "." + ver[1] + " " + EGL14.eglQueryString(dpy, EGL14.EGL_VENDOR));
            int[] attribs = { EGL14.EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_DEPTH_SIZE, 16, EGL14.EGL_NONE };
            EGLConfig[] cfg = new EGLConfig[1];
            int[] num = new int[1];
            int client = 3;
            if (!EGL14.eglChooseConfig(dpy, attribs, 0, cfg, 0, 1, num, 0) || num[0] == 0) {
                attribs[1] = EGL14.EGL_OPENGL_ES2_BIT;
                client = 2;
                if (!EGL14.eglChooseConfig(dpy, attribs, 0, cfg, 0, 1, num, 0) || num[0] == 0) { m.put("eglConfig", "none"); return false; }
            }
            surf = EGL14.eglCreatePbufferSurface(dpy, cfg[0], new int[] { EGL14.EGL_WIDTH, 64, EGL14.EGL_HEIGHT, 64, EGL14.EGL_NONE }, 0);
            if (surf == EGL14.EGL_NO_SURFACE) { m.put("surface", "eglCreatePbufferSurface failed 0x" + Integer.toHexString(EGL14.eglGetError())); return false; }
            int[] w = new int[1], h = new int[1];
            EGL14.eglQuerySurface(dpy, surf, EGL14.EGL_WIDTH, w, 0);
            EGL14.eglQuerySurface(dpy, surf, EGL14.EGL_HEIGHT, h, 0);
            m.put("surface", "pbuffer " + w[0] + "x" + h[0] + " RGB888 D16");
            ctx = EGL14.eglCreateContext(dpy, cfg[0], EGL14.EGL_NO_CONTEXT, new int[] { EGL14.EGL_CONTEXT_CLIENT_VERSION, client, EGL14.EGL_NONE }, 0);
            if (ctx == EGL14.EGL_NO_CONTEXT) { m.put("context", "eglCreateContext failed 0x" + Integer.toHexString(EGL14.eglGetError())); return false; }
            if (!EGL14.eglMakeCurrent(dpy, surf, surf, ctx)) { m.put("context", "eglMakeCurrent failed 0x" + Integer.toHexString(EGL14.eglGetError())); return false; }
            m.put("context", "GLES " + client);
            m.put("GL_VENDOR", GLES20.glGetString(GLES20.GL_VENDOR));
            m.put("GL_RENDERER", GLES20.glGetString(GLES20.GL_RENDERER));
            m.put("GL_VERSION", GLES20.glGetString(GLES20.GL_VERSION));
            m.put("GL_SHADING_LANGUAGE_VERSION", GLES20.glGetString(GLES20.GL_SHADING_LANGUAGE_VERSION));
            int[] max = new int[1];
            GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, max, 0);
            m.put("GL_MAX_TEXTURE_SIZE", String.valueOf(max[0]));
            return m.get("GL_RENDERER") != null;
        } finally {
            EGL14.eglMakeCurrent(dpy, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (ctx != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(dpy, ctx);
            if (surf != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(dpy, surf);
            EGL14.eglTerminate(dpy);
        }
    }

    private void finish(Map<String, String> m) {
        String report = ReportFormat.formatMetadata(m);
        for (String line : report.split("\n")) Log.i(TAG, line);
        try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), BfmeDiagnostics.NATIVE_SMOKE))) {
            out.write(report.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            Log.w(TAG, "native-smoke.txt write failed", e);
        }
        runOnUiThread(() -> text.setText(getString(R.string.bfme_smoke_done, report)));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Leave no process with libmain.so loaded behind.
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }
}
