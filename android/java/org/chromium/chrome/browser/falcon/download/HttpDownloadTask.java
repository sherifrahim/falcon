/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.text.TextUtils;

import org.chromium.base.Log;
import org.chromium.net.ChromiumNetworkAdapter;
import org.chromium.net.NetworkTrafficAnnotationTag;
import org.chromium.chrome.browser.falcon.download.DownloadItem.Segment;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Multi-connection HTTP download of one {@link DownloadItem} into a part file. Probes the
 * server for size and range support, splits the file into segments, fetches them in parallel,
 * and resumes from whatever the item's segments already hold. Pause/cancel are cooperative.
 */
final class HttpDownloadTask implements DownloadTask {
    private static final String TAG = "FalconDownload";
    private static final int BUFFER_BYTES = 64 * 1024;
    private static final long MIN_SEGMENT_BYTES = 2L * 1024 * 1024;
    private static final int MAX_RETRIES = 6;
    private static final int CONNECT_TIMEOUT_MS = 20_000;
    private static final int READ_TIMEOUT_MS = 30_000;
    private static final NetworkTrafficAnnotationTag TRAFFIC_ANNOTATION =
            NetworkTrafficAnnotationTag.createComplete(
                    "falcon_downloader",
                    "semantics {"
                            + "  sender: 'Falcon downloader'"
                            + "  description: 'Fetches a file the user chose to download, in"
                            + " several byte ranges at once, with the page cookies Chromium"
                            + " would have sent.'"
                            + "  trigger: 'A download Chromium handed to Falcon, or a link the"
                            + " user pasted into the Downloads page.'"
                            + "  data: 'The download URL, referer, user agent and cookies.'"
                            + "  destination: WEBSITE"
                            + "}"
                            + "policy {"
                            + "  cookies_allowed: YES"
                            + "  setting: 'Settings > Falcon > Take over page downloads.'"
                            + "  policy_exception_justification: 'Personal fork; no policy.'"
                            + "}");

    interface Listener {
        void onProbed(DownloadItem item);

        void onProgress(DownloadItem item);

        void onFinished(DownloadItem item, File partFile);

        void onPaused(DownloadItem item);

        void onFailed(DownloadItem item, String error);

        void onCancelled(DownloadItem item);
    }

    private final DownloadItem mItem;
    private final File mPartFile;
    private final int mConnections;
    private final Listener mListener;
    private volatile boolean mPauseRequested;
    private volatile boolean mCancelRequested;

    HttpDownloadTask(DownloadItem item, File partFile, int connections, Listener listener) {
        mItem = item;
        mPartFile = partFile;
        mConnections = Math.max(1, connections);
        mListener = listener;
    }

    @Override
    public void pause() {
        mPauseRequested = true;
    }

    @Override
    public void cancel() {
        mCancelRequested = true;
    }

    boolean isStopping() {
        return mPauseRequested || mCancelRequested;
    }

    @Override
    public void run() {
        try {
            if (!mItem.hasSegments() || !mItem.resumable) {
                probe();
                if (isStopping()) {
                    finishStopped();
                    return;
                }
                mListener.onProbed(mItem);
            }
            fetchAll();
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "download %s failed: %s", mItem.fileName, e.getMessage());
            mListener.onFailed(mItem, e.getMessage() == null ? "download failed" : e.getMessage());
        }
    }

    // ── probe ──

    private void probe() throws IOException {
        HttpURLConnection c = open("GET", "bytes=0-0");
        try {
            int code = c.getResponseCode();
            if (code >= 400) throw new IOException("HTTP " + code);
            String disposition = c.getHeaderField("Content-Disposition");
            String fromHeader = filenameFromDisposition(disposition);
            if (!TextUtils.isEmpty(fromHeader)) mItem.fileName = fromHeader;

            long total = -1;
            String range = c.getHeaderField("Content-Range"); // bytes 0-0/123
            if (code == 206 && range != null && range.contains("/")) {
                String size = range.substring(range.lastIndexOf('/') + 1).trim();
                if (!"*".equals(size)) total = Long.parseLong(size);
            } else {
                total = c.getContentLengthLong();
            }
            boolean ranges =
                    code == 206
                            || "bytes".equalsIgnoreCase(c.getHeaderField("Accept-Ranges"));
            String etag = c.getHeaderField("ETag");
            mItem.etag = etag == null ? "" : etag;
            mItem.totalBytes = total;
            mItem.resumable = ranges && total > 0;

            List<Segment> segments = new ArrayList<>();
            if (mItem.resumable) {
                int n = (int) Math.min(mConnections, Math.max(1, total / MIN_SEGMENT_BYTES));
                long per = total / n;
                for (int i = 0; i < n; i++) {
                    long start = i * per;
                    long end = i == n - 1 ? total - 1 : start + per - 1;
                    segments.add(new Segment(start, end, 0));
                }
            } else {
                segments.add(new Segment(0, total > 0 ? total - 1 : -1, 0));
            }
            mItem.setSegments(segments);
        } finally {
            c.disconnect();
        }
        // Reserve the file so segments can seek into it.
        try (RandomAccessFile raf = new RandomAccessFile(mPartFile, "rw")) {
            if (mItem.totalBytes > 0) raf.setLength(mItem.totalBytes);
        }
    }

    // ── fetch ──

    private void fetchAll() {
        List<Segment> segments = mItem.segments();
        List<Segment> pending = new ArrayList<>();
        for (Segment s : segments) if (!s.isComplete()) pending.add(s);
        if (pending.isEmpty()) {
            mListener.onFinished(mItem, mPartFile);
            return;
        }
        ExecutorService pool = Executors.newFixedThreadPool(Math.min(mConnections, pending.size()));
        CountDownLatch latch = new CountDownLatch(pending.size());
        AtomicReference<String> failure = new AtomicReference<>();
        for (Segment s : pending) {
            pool.execute(
                    () -> {
                        try {
                            fetchSegment(s);
                        } catch (IOException e) {
                            failure.compareAndSet(
                                    null, e.getMessage() == null ? "connection failed" : e.getMessage());
                            // Stop the siblings too: a dead server shouldn't burn retries on every thread.
                            if (!isStopping()) mPauseRequested = true;
                        } finally {
                            latch.countDown();
                        }
                    });
        }
        try {
            latch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        pool.shutdownNow();
        try {
            pool.awaitTermination(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }

        if (mCancelRequested) {
            mListener.onCancelled(mItem);
            return;
        }
        if (failure.get() != null) {
            mPauseRequested = false;
            mListener.onFailed(mItem, failure.get());
            return;
        }
        if (mPauseRequested) {
            mListener.onPaused(mItem);
            return;
        }
        boolean complete = true;
        for (Segment s : mItem.segments()) {
            if (s.end >= 0 ? !s.isComplete() : s.done <= 0) complete = false;
        }
        if (complete) {
            mListener.onFinished(mItem, mPartFile);
        } else {
            mListener.onFailed(mItem, "incomplete transfer");
        }
    }

    private void fetchSegment(Segment segment) throws IOException {
        int attempt = 0;
        while (!isStopping()) {
            try {
                fetchSegmentOnce(segment);
                return;
            } catch (IOException e) {
                if (isStopping()) return;
                attempt++;
                if (attempt > MAX_RETRIES) throw e;
                long backoff = Math.min(30_000L, 1000L << Math.min(attempt, 5));
                Log.i(TAG, "segment %d-%d retry %d in %d ms: %s", segment.start, segment.end,
                        attempt, backoff, e.getMessage());
                try {
                    Thread.sleep(backoff);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
    }

    private void fetchSegmentOnce(Segment segment) throws IOException {
        long from = segment.start + segment.done;
        String range = null;
        if (segment.end >= 0) {
            if (from > segment.end) return;
            range = "bytes=" + from + "-" + segment.end;
        } else if (from > 0) {
            range = "bytes=" + from + "-";
        }
        HttpURLConnection c = open("GET", range);
        try {
            int code = c.getResponseCode();
            if (code >= 400) throw new IOException("HTTP " + code);
            if (range != null && code != 206) {
                // Server ignored the range: it sent the whole file. Only usable from offset 0.
                if (from != 0) throw new IOException("server does not support resume");
            }
            if (!TextUtils.isEmpty(mItem.etag)) {
                String etag = c.getHeaderField("ETag");
                if (etag != null && !etag.equals(mItem.etag) && segment.done > 0) {
                    throw new IOException("file changed on the server; restart the download");
                }
            }
            try (InputStream in = c.getInputStream();
                    RandomAccessFile raf = new RandomAccessFile(mPartFile, "rw")) {
                raf.seek(from);
                byte[] buf = new byte[BUFFER_BYTES];
                long remaining = segment.end >= 0 ? segment.end - from + 1 : Long.MAX_VALUE;
                while (remaining > 0 && !isStopping()) {
                    int want = (int) Math.min(buf.length, remaining);
                    int n = in.read(buf, 0, want);
                    if (n < 0) break;
                    raf.write(buf, 0, n);
                    // One writer per segment; the read-then-write is what the volatile is for.
                    long done = segment.done;
                    segment.done = done + n;
                    remaining -= n;
                }
                if (segment.end < 0 && !isStopping()) {
                    // Open-ended stream finished: now we know the size.
                    mItem.totalBytes = segment.done;
                }
            }
        } finally {
            c.disconnect();
        }
    }

    private HttpURLConnection open(String method, String range) throws IOException {
        HttpURLConnection c =
                (HttpURLConnection)
                        ChromiumNetworkAdapter.openConnection(new URL(mItem.url), TRAFFIC_ANNOTATION);
        c.setRequestMethod(method);
        c.setInstanceFollowRedirects(true);
        c.setConnectTimeout(CONNECT_TIMEOUT_MS);
        c.setReadTimeout(READ_TIMEOUT_MS);
        c.setUseCaches(false);
        if (!TextUtils.isEmpty(mItem.userAgent)) c.setRequestProperty("User-Agent", mItem.userAgent);
        if (!TextUtils.isEmpty(mItem.referer)) c.setRequestProperty("Referer", mItem.referer);
        if (!TextUtils.isEmpty(mItem.cookies)) c.setRequestProperty("Cookie", mItem.cookies);
        c.setRequestProperty("Accept-Encoding", "identity");
        if (range != null) c.setRequestProperty("Range", range);
        return c;
    }

    private void finishStopped() {
        if (mCancelRequested) {
            mListener.onCancelled(mItem);
        } else {
            mListener.onPaused(mItem);
        }
    }

    /** Pulls the filename out of a Content-Disposition header (filename* wins over filename). */
    static String filenameFromDisposition(String disposition) {
        if (TextUtils.isEmpty(disposition)) return "";
        String lower = disposition.toLowerCase(java.util.Locale.US);
        int star = lower.indexOf("filename*=");
        if (star >= 0) {
            String v = disposition.substring(star + "filename*=".length()).trim();
            int semi = v.indexOf(';');
            if (semi >= 0) v = v.substring(0, semi);
            int q = v.indexOf("''");
            if (q >= 0) v = v.substring(q + 2);
            try {
                return sanitize(java.net.URLDecoder.decode(v, "UTF-8"));
            } catch (java.io.UnsupportedEncodingException | IllegalArgumentException e) {
                return sanitize(v);
            }
        }
        int plain = lower.indexOf("filename=");
        if (plain >= 0) {
            String v = disposition.substring(plain + "filename=".length()).trim();
            int semi = v.indexOf(';');
            if (semi >= 0) v = v.substring(0, semi);
            v = v.trim();
            if (v.startsWith("\"") && v.endsWith("\"") && v.length() >= 2) {
                v = v.substring(1, v.length() - 1);
            }
            return sanitize(v);
        }
        return "";
    }

    static String sanitize(String name) {
        String n = name.replace('/', '_').replace('\\', '_').replace('\0', '_').trim();
        while (n.startsWith(".")) n = n.substring(1);
        return n.length() > 200 ? n.substring(0, 200) : n;
    }
}
