/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.text.TextUtils;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The Falcon downloader on Android: queue, engine tasks, persistence, telemetry and the export
 * of finished files into the device's Downloads collection. UI-thread API; downloads run on a
 * private pool. The foreground service ({@link FalconDownloadService}) mirrors the state into
 * notifications.
 */
public final class FalconDownloadManager {
    private static final String TAG = "FalconDownload";
    private static final String STORE_FILE = "falcon_downloads.json";
    private static final long WIFI_ONLY_THRESHOLD = 100L * 1024 * 1024;
    private static final int MAX_CONCURRENT = 3;
    private static final int TICK_MS = 500;
    private static final int TELEMETRY_SECONDS = 60;

    /** Observers get one callback per change; they read the list from the manager. */
    public interface Observer {
        void onDownloadsChanged();

        default void onDownloadFinished(DownloadItem item) {}
    }

    private static FalconDownloadManager sInstance;

    private final Context mContext = ContextUtils.getApplicationContext();
    private final Map<String, DownloadItem> mItems = new LinkedHashMap<>();
    private final Map<String, HttpDownloadTask> mTasks = new HashMap<>();
    private final ObserverList<Observer> mObservers = new ObserverList<>();
    private final ExecutorService mPool = Executors.newCachedThreadPool();
    private final ExecutorService mIo = Executors.newSingleThreadExecutor();
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final long[] mSpeedHistory = new long[TELEMETRY_SECONDS];
    private int mSpeedHistoryHead;
    private long mLastTickBytes = -1;
    private long mLastTickTime;
    private long mTotalSpeedBps;
    private boolean mTicking;
    private boolean mLoaded;
    private ConnectivityManager.NetworkCallback mNetworkCallback;

    public static FalconDownloadManager getInstance() {
        ThreadUtils.assertOnUiThread();
        if (sInstance == null) sInstance = new FalconDownloadManager();
        return sInstance;
    }

    private FalconDownloadManager() {
        load();
    }

    // ── public API (UI thread) ──

    public List<DownloadItem> items() {
        List<DownloadItem> list = new ArrayList<>(mItems.values());
        Collections.reverse(list); // newest first
        return list;
    }

    public DownloadItem get(String id) {
        return mItems.get(id);
    }

    public void addObserver(Observer o) {
        mObservers.addObserver(o);
    }

    public void removeObserver(Observer o) {
        mObservers.removeObserver(o);
    }

    /** Aggregate download speed (bytes/s) over the last tick. */
    public long totalSpeedBps() {
        return mTotalSpeedBps;
    }

    /** The last 60 one-second speed samples, oldest first (for the telemetry graph). */
    public long[] speedHistory() {
        long[] out = new long[TELEMETRY_SECONDS];
        for (int i = 0; i < TELEMETRY_SECONDS; i++) {
            out[i] = mSpeedHistory[(mSpeedHistoryHead + i) % TELEMETRY_SECONDS];
        }
        return out;
    }

    public int activeCount() {
        int n = 0;
        for (DownloadItem i : mItems.values()) if (i.isActive()) n++;
        return n;
    }

    public int queuedCount() {
        int n = 0;
        for (DownloadItem i : mItems.values()) {
            if (i.state == State.QUEUED || i.state == State.WAITING_WIFI) n++;
        }
        return n;
    }

    /** Creates and queues a download. Returns the new item. */
    public DownloadItem enqueue(
            String url,
            String referer,
            String userAgent,
            String cookies,
            String fileName,
            String mimeType,
            long totalBytes) {
        ThreadUtils.assertOnUiThread();
        String name = HttpDownloadTask.sanitize(TextUtils.isEmpty(fileName) ? "download" : fileName);
        DownloadItem item =
                new DownloadItem(
                        UUID.randomUUID().toString(),
                        url,
                        referer,
                        userAgent,
                        cookies,
                        name,
                        mimeType,
                        totalBytes,
                        System.currentTimeMillis());
        mItems.put(item.id, item);
        Log.i(TAG, "enqueue %s (%s)", name, DownloadItem.formatBytes(totalBytes));
        schedule();
        notifyChanged();
        return item;
    }

    public void pause(String id) {
        DownloadItem item = mItems.get(id);
        if (item == null) return;
        HttpDownloadTask task = mTasks.get(id);
        if (task != null) {
            task.pause();
        } else if (item.state == State.QUEUED || item.state == State.WAITING_WIFI) {
            item.state = State.PAUSED;
            save();
            notifyChanged();
        }
    }

    public void resume(String id) {
        DownloadItem item = mItems.get(id);
        if (item == null || mTasks.containsKey(id)) return;
        if (item.state == State.PAUSED || item.state == State.FAILED) {
            item.state = State.QUEUED;
            item.error = "";
            schedule();
            notifyChanged();
        }
    }

    public void pauseAll() {
        for (DownloadItem i : new ArrayList<>(mItems.values())) {
            if (i.isActive() || i.state == State.QUEUED) pause(i.id);
        }
    }

    public void resumeAll() {
        for (DownloadItem i : new ArrayList<>(mItems.values())) {
            if (i.state == State.PAUSED) resume(i.id);
        }
    }

    public void cancel(String id) {
        DownloadItem item = mItems.get(id);
        if (item == null) return;
        HttpDownloadTask task = mTasks.get(id);
        if (task != null) {
            task.cancel();
        } else {
            item.state = State.CANCELLED;
            deletePart(item);
            save();
            notifyChanged();
        }
    }

    /** Removes the entry (and the part file); the exported file in Downloads is kept. */
    public void remove(String id) {
        DownloadItem item = mItems.get(id);
        if (item == null) return;
        if (mTasks.containsKey(id)) {
            cancel(id);
        }
        mItems.remove(id);
        deletePart(item);
        save();
        notifyChanged();
    }

    public void clearFinished() {
        for (DownloadItem i : new ArrayList<>(mItems.values())) {
            if (i.isTerminal()) mItems.remove(i.id);
        }
        save();
        notifyChanged();
    }

    /** An intent that opens the exported file with the system chooser, or null. */
    public Intent openIntent(DownloadItem item) {
        if (TextUtils.isEmpty(item.savedUri)) return null;
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(
                Uri.parse(item.savedUri),
                TextUtils.isEmpty(item.mimeType) ? "*/*" : item.mimeType);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        return intent;
    }

    // ── scheduling ──

    private void schedule() {
        ThreadUtils.assertOnUiThread();
        int running = mTasks.size();
        boolean wifiOnly = FalconPrefs.isDownloaderWifiOnly();
        boolean onWifi = isOnWifi();
        for (DownloadItem item : mItems.values()) {
            if (running >= MAX_CONCURRENT) break;
            if (item.state != State.QUEUED && item.state != State.WAITING_WIFI) continue;
            if (wifiOnly && !onWifi && item.totalBytes > WIFI_ONLY_THRESHOLD) {
                item.state = State.WAITING_WIFI;
                ensureNetworkCallback();
                continue;
            }
            start(item);
            running++;
        }
        save();
        if (running > 0) {
            ensureTicking();
            FalconDownloadService.ensureRunning(mContext);
        }
    }

    private void start(DownloadItem item) {
        item.state = State.ACTIVE;
        item.error = "";
        HttpDownloadTask task =
                new HttpDownloadTask(
                        item, partFile(item), FalconPrefs.getDownloaderConnections(), mTaskListener);
        mTasks.put(item.id, task);
        mPool.execute(task);
    }

    private final HttpDownloadTask.Listener mTaskListener =
            new HttpDownloadTask.Listener() {
                @Override
                public void onProbed(DownloadItem item) {
                    post(() -> {
                        save();
                        notifyChanged();
                    });
                }

                @Override
                public void onProgress(DownloadItem item) {}

                @Override
                public void onFinished(DownloadItem item, File partFile) {
                    // Hashing and the export copy are slow: keep them off the pool thread
                    // that ran the download and off the UI thread.
                    mIo.execute(() -> finalizeFile(item, partFile));
                }

                @Override
                public void onPaused(DownloadItem item) {
                    post(() -> {
                        item.state = State.PAUSED;
                        item.speedBps = 0;
                        item.etaSeconds = -1;
                        taskDone(item);
                    });
                }

                @Override
                public void onFailed(DownloadItem item, String error) {
                    post(() -> {
                        item.state = State.FAILED;
                        item.error = error;
                        item.speedBps = 0;
                        item.etaSeconds = -1;
                        taskDone(item);
                    });
                }

                @Override
                public void onCancelled(DownloadItem item) {
                    post(() -> {
                        item.state = State.CANCELLED;
                        deletePart(item);
                        taskDone(item);
                    });
                }
            };

    private void finalizeFile(DownloadItem item, File partFile) {
        String error = null;
        try {
            if (!TextUtils.isEmpty(item.sha256Expected) && FalconPrefs.isDownloaderVerifyEnabled()) {
                item.sha256Actual = sha256(partFile);
                if (!item.sha256Actual.equalsIgnoreCase(item.sha256Expected.trim())) {
                    error = "SHA-256 mismatch";
                }
            }
            if (error == null) {
                item.savedUri = exportToDownloads(item, partFile);
                if (TextUtils.isEmpty(item.savedUri)) error = "could not save to Downloads";
            }
        } catch (IOException e) {
            error = e.getMessage() == null ? "could not save file" : e.getMessage();
        }
        final String finalError = error;
        post(() -> {
            if (finalError != null) {
                item.state = State.FAILED;
                item.error = finalError;
            } else {
                item.state = State.DONE;
                item.finishedAt = System.currentTimeMillis();
                item.speedBps = 0;
                item.etaSeconds = 0;
                deletePart(item);
            }
            taskDone(item);
            if (finalError == null) {
                for (Observer o : mObservers) o.onDownloadFinished(item);
            }
        });
    }

    private void taskDone(DownloadItem item) {
        mTasks.remove(item.id);
        save();
        notifyChanged();
        schedule();
        if (mTasks.isEmpty()) {
            mTotalSpeedBps = 0;
            FalconDownloadService.onIdle(mContext);
        }
    }

    // ── telemetry tick ──

    private void ensureTicking() {
        if (mTicking) return;
        mTicking = true;
        mLastTickBytes = -1;
        mHandler.postDelayed(mTick, TICK_MS);
    }

    private final Runnable mTick =
            new Runnable() {
                @Override
                public void run() {
                    if (mTasks.isEmpty()) {
                        mTicking = false;
                        return;
                    }
                    long now = System.currentTimeMillis();
                    long total = 0;
                    for (String id : mTasks.keySet()) {
                        DownloadItem item = mItems.get(id);
                        if (item != null) total += item.doneBytes();
                    }
                    if (mLastTickBytes >= 0 && now > mLastTickTime) {
                        long bps = (total - mLastTickBytes) * 1000 / (now - mLastTickTime);
                        mTotalSpeedBps = Math.max(0, bps);
                        // Per-item speed: share of the delta by each item's own delta is
                        // overkill; approximate by each item's progress delta.
                        for (String id : mTasks.keySet()) {
                            DownloadItem item = mItems.get(id);
                            if (item == null) continue;
                            long done = item.doneBytes();
                            long prev = mPerItemLast.containsKey(id) ? mPerItemLast.get(id) : done;
                            item.speedBps = Math.max(0, (done - prev) * 1000 / (now - mLastTickTime));
                            long left = item.totalBytes > 0 ? item.totalBytes - done : -1;
                            item.etaSeconds = left >= 0 && item.speedBps > 0 ? left / item.speedBps : -1;
                            mPerItemLast.put(id, done);
                        }
                        if (now - mLastHistoryTime >= 1000) {
                            mSpeedHistory[mSpeedHistoryHead] = mTotalSpeedBps;
                            mSpeedHistoryHead = (mSpeedHistoryHead + 1) % TELEMETRY_SECONDS;
                            mLastHistoryTime = now;
                        }
                    } else {
                        for (String id : mTasks.keySet()) {
                            DownloadItem item = mItems.get(id);
                            if (item != null) mPerItemLast.put(id, item.doneBytes());
                        }
                    }
                    mLastTickBytes = total;
                    mLastTickTime = now;
                    if (now - mLastSaveTime >= 3000) {
                        save();
                        mLastSaveTime = now;
                    }
                    notifyChanged();
                    mHandler.postDelayed(this, TICK_MS);
                }
            };
    private final Map<String, Long> mPerItemLast = new HashMap<>();
    private long mLastHistoryTime;
    private long mLastSaveTime;

    // ── files ──

    private File partsDir() {
        File dir = new File(mContext.getFilesDir(), "falcon_downloads");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private File partFile(DownloadItem item) {
        return new File(partsDir(), item.partFileName());
    }

    private void deletePart(DownloadItem item) {
        File f = partFile(item);
        if (f.exists()) f.delete();
    }

    /** Copies the finished file into the device's Downloads and returns its content URI. */
    @SuppressWarnings("deprecation") // getExternalStoragePublicDirectory: pre-Q fallback only.
    private String exportToDownloads(DownloadItem item, File partFile) throws IOException {
        String name = uniqueName(item.fileName);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ContentResolver resolver = mContext.getContentResolver();
            ContentValues values = new ContentValues();
            values.put(MediaStore.Downloads.DISPLAY_NAME, name);
            values.put(
                    MediaStore.Downloads.MIME_TYPE,
                    TextUtils.isEmpty(item.mimeType) ? "application/octet-stream" : item.mimeType);
            values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
            values.put(MediaStore.Downloads.IS_PENDING, 1);
            Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) return "";
            try (InputStream in = new FileInputStream(partFile);
                    OutputStream out = resolver.openOutputStream(uri)) {
                if (out == null) throw new IOException("no output stream");
                copy(in, out);
            } catch (IOException e) {
                resolver.delete(uri, null, null);
                throw e;
            }
            values.clear();
            values.put(MediaStore.Downloads.IS_PENDING, 0);
            resolver.update(uri, values, null, null);
            return uri.toString();
        }
        File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (!dir.exists()) dir.mkdirs();
        File target = new File(dir, name);
        try (InputStream in = new FileInputStream(partFile);
                OutputStream out = new FileOutputStream(target)) {
            copy(in, out);
        }
        return Uri.fromFile(target).toString();
    }

    @SuppressWarnings("deprecation")
    private String uniqueName(String fileName) {
        // MediaStore de-duplicates names itself on Q+; keep the pre-Q path honest.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) return fileName;
        File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (!new File(dir, fileName).exists()) return fileName;
        int dot = fileName.lastIndexOf('.');
        String base = dot > 0 ? fileName.substring(0, dot) : fileName;
        String ext = dot > 0 ? fileName.substring(dot) : "";
        for (int i = 1; i < 1000; i++) {
            String candidate = String.format(Locale.US, "%s (%d)%s", base, i, ext);
            if (!new File(dir, candidate).exists()) return candidate;
        }
        return fileName;
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[256 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        out.flush();
    }

    static String sha256(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[256 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format(Locale.US, "%02x", b));
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 unavailable");
        }
    }

    // ── network ──

    private boolean isOnWifi() {
        ConnectivityManager cm =
                (ConnectivityManager) mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return true;
        Network network = cm.getActiveNetwork();
        if (network == null) return false;
        NetworkCapabilities caps = cm.getNetworkCapabilities(network);
        return caps != null
                && (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                        || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));
    }

    private void ensureNetworkCallback() {
        if (mNetworkCallback != null) return;
        ConnectivityManager cm =
                (ConnectivityManager) mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return;
        mNetworkCallback =
                new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                        post(() -> schedule());
                    }
                };
        cm.registerDefaultNetworkCallback(mNetworkCallback);
    }

    // ── persistence ──

    private void load() {
        File f = new File(mContext.getFilesDir(), STORE_FILE);
        if (!f.exists()) {
            mLoaded = true;
            return;
        }
        try (InputStream in = new FileInputStream(f)) {
            byte[] bytes = new byte[(int) f.length()];
            int off = 0;
            while (off < bytes.length) {
                int n = in.read(bytes, off, bytes.length - off);
                if (n < 0) break;
                off += n;
            }
            JSONArray arr = new JSONArray(new String(bytes, 0, off, StandardCharsets.UTF_8));
            for (int i = 0; i < arr.length(); i++) {
                DownloadItem item = DownloadItem.fromJson(arr.getJSONObject(i));
                mItems.put(item.id, item);
            }
        } catch (IOException | JSONException e) {
            Log.w(TAG, "could not load downloads: %s", e.getMessage());
        }
        mLoaded = true;
    }

    private void save() {
        if (!mLoaded) return;
        final JSONArray arr = new JSONArray();
        try {
            for (DownloadItem item : mItems.values()) arr.put(item.toJson());
        } catch (JSONException e) {
            Log.w(TAG, "could not serialise downloads: %s", e.getMessage());
            return;
        }
        final byte[] bytes = arr.toString().getBytes(StandardCharsets.UTF_8);
        mIo.execute(() -> {
            File f = new File(mContext.getFilesDir(), STORE_FILE);
            File tmp = new File(mContext.getFilesDir(), STORE_FILE + ".tmp");
            try (OutputStream out = new FileOutputStream(tmp)) {
                out.write(bytes);
            } catch (IOException e) {
                Log.w(TAG, "could not save downloads: %s", e.getMessage());
                return;
            }
            if (!tmp.renameTo(f)) {
                Log.w(TAG, "could not replace the downloads store");
            }
        });
    }

    private void notifyChanged() {
        for (Observer o : mObservers) o.onDownloadsChanged();
    }

    private void post(Runnable r) {
        mHandler.post(r);
    }

    /** Whether a file of this size/type should be taken over from Chromium. */
    static boolean wantsDownload(String url, String fileName, String mimeType, long contentLength) {
        if (!FalconPrefs.isDownloaderEnabled()) return false;
        // Tiny files: Chromium's own path is fine and keeps its download UI.
        if (contentLength >= 0 && contentLength < 512 * 1024) return false;
        return true;
    }
}
