/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.text.TextUtils;

import org.chromium.base.Log;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.security.GeneralSecurityException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * Downloads an HLS or DASH stream the way 1DM does: every segment of the chosen quality in
 * parallel (AES-128 segments decrypted as they land), then the pieces joined per track and
 * rewritten into one MP4 without re-encoding. If the platform can't rewrite a single MPEG-TS
 * track, the joined .ts is kept instead ("merge as TS"). Finished segments survive a pause or a
 * restart; resume fetches only what is missing.
 */
final class StreamDownloadTask implements DownloadTask {
    private static final String TAG = "FalconStream";
    private static final int MAX_RETRIES = 5;
    private static final int MAX_KEY_BYTES = 64 * 1024;

    /** One piece to fetch. */
    private static final class Part {
        int index; // global, for the progress map
        String url;
        String range; // null = whole resource
        HlsPlaylist.Key key; // AES-128 or null
        long sequence;
        File file;
    }

    private static final class Track {
        final String name;
        final List<Part> parts = new ArrayList<>();
        Part init; // fMP4 / WebM init section, or null
        boolean ts;

        Track(String name) {
            this.name = name;
        }
    }

    private final DownloadItem mItem;
    private final File mDir;
    private final int mConnections;
    private final HttpDownloadTask.Listener mListener;
    private final StreamHttp.Headers mHeaders;
    private final Map<String, byte[]> mKeys = new HashMap<>();
    private volatile boolean mPauseRequested;
    private volatile boolean mCancelRequested;

    StreamDownloadTask(
            DownloadItem item, File dir, int connections, HttpDownloadTask.Listener listener) {
        mItem = item;
        mDir = dir;
        mConnections = Math.max(1, connections);
        mListener = listener;
        mHeaders = new StreamHttp.Headers(item.referer, item.userAgent, item.cookies, item.url);
    }

    @Override
    public void pause() {
        mPauseRequested = true;
    }

    @Override
    public void cancel() {
        mCancelRequested = true;
    }

    private boolean isStopping() {
        return mPauseRequested || mCancelRequested;
    }

    @Override
    public void run() {
        try {
            if (!mDir.exists() && !mDir.mkdirs()) throw new IOException("no space for the download");
            List<Track> tracks = resolve();
            if (isStopping()) {
                finishStopped();
                return;
            }
            List<Part> all = new ArrayList<>();
            for (Track t : tracks) {
                if (t.init != null) all.add(t.init);
                all.addAll(t.parts);
            }
            for (int i = 0; i < all.size(); i++) all.get(i).index = i;
            mItem.startStream(all.size());
            long bytes = 0;
            for (Part p : all) {
                if (p.file.exists()) {
                    mItem.markStreamPart(p.index);
                    bytes += p.file.length();
                }
            }
            mItem.streamBytes.set(bytes);
            mListener.onProbed(mItem);

            String failure = fetchAll(all);
            if (mCancelRequested) {
                mListener.onCancelled(mItem);
                return;
            }
            if (failure != null) {
                mListener.onFailed(mItem, failure);
                return;
            }
            if (mPauseRequested) {
                mListener.onPaused(mItem);
                return;
            }
            File out = assemble(tracks);
            if (isStopping()) {
                finishStopped();
                return;
            }
            mItem.totalBytes = out.length();
            mListener.onFinished(mItem, out);
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "stream %s failed: %s", mItem.fileName, e.getMessage());
            String msg = e.getMessage() == null ? "download failed" : e.getMessage();
            if (msg.startsWith("HTTP 403") || msg.startsWith("HTTP 401") || msg.startsWith("HTTP 410")) {
                msg = "the stream link expired; play the video again and grab it anew";
            }
            mListener.onFailed(mItem, msg);
        }
    }

    // ── what to fetch ──

    private List<Track> resolve() throws IOException {
        JSONObject spec;
        try {
            spec = new JSONObject(mItem.streamSpec);
        } catch (JSONException e) {
            throw new IOException("bad stream description");
        }
        List<Track> tracks = new ArrayList<>();
        if ("dash".equals(spec.optString("t"))) {
            StreamHttp.Text text = StreamHttp.getText(spec.optString("m"), mHeaders);
            DashManifest mpd = DashManifest.parse(text.body, text.finalUrl);
            for (String role : new String[] {"v", "a"}) {
                String id = spec.optString(role);
                if (TextUtils.isEmpty(id)) continue;
                DashManifest.Representation rep = mpd.find(id);
                if (rep == null) throw new IOException("the stream changed; grab it again");
                Track t = new Track(role);
                if (rep.init != null) t.init = part(t, "init", rep.init.url, rep.init.range(), null, 0);
                for (int i = 0; i < rep.segments.size(); i++) {
                    DashManifest.Seg s = rep.segments.get(i);
                    t.parts.add(part(t, String.valueOf(i), s.url, s.range(), null, 0));
                }
                tracks.add(t);
            }
        } else {
            for (String role : new String[] {"v", "a"}) {
                String url = spec.optString(role);
                if (TextUtils.isEmpty(url)) continue;
                StreamHttp.Text text = StreamHttp.getText(url, mHeaders);
                HlsPlaylist pl = HlsPlaylist.parse(text.body, text.finalUrl);
                if (pl.isDrm()) throw new IOException("this stream is DRM-protected");
                Track t = new Track(role);
                t.ts = !pl.isFragmentedMp4();
                if (!pl.segments.isEmpty() && pl.segments.get(0).map != null) {
                    HlsPlaylist.Segment m = pl.segments.get(0).map;
                    t.init = part(t, "init", m.url, range(m.byteStart, m.byteLength), null, 0);
                }
                for (int i = 0; i < pl.segments.size(); i++) {
                    HlsPlaylist.Segment s = pl.segments.get(i);
                    t.parts.add(
                            part(t, String.valueOf(i), s.url, range(s.byteStart, s.byteLength),
                                    s.key, s.sequence));
                }
                tracks.add(t);
            }
        }
        if (tracks.isEmpty() || tracks.get(0).parts.isEmpty()) {
            throw new IOException("the stream has no segments");
        }
        return tracks;
    }

    private Part part(Track t, String name, String url, String range, HlsPlaylist.Key key, long seq) {
        Part p = new Part();
        p.url = url;
        p.range = range;
        p.key = key != null && "AES-128".equals(key.method) ? key : null;
        p.sequence = seq;
        p.file = new File(mDir, t.name + "_" + name + ".seg");
        return p;
    }

    private static String range(long start, long length) {
        return start < 0 || length < 0 ? null : "bytes=" + start + "-" + (start + length - 1);
    }

    // ── fetching ──

    /** Returns null on success (or stop), else the failure message. */
    private String fetchAll(List<Part> all) {
        List<Part> pending = new ArrayList<>();
        for (Part p : all) if (!p.file.exists()) pending.add(p);
        if (pending.isEmpty()) return null;
        ExecutorService pool = Executors.newFixedThreadPool(Math.min(mConnections, pending.size()));
        AtomicReference<String> failure = new AtomicReference<>();
        AtomicInteger next = new AtomicInteger();
        int workers = Math.min(mConnections, pending.size());
        for (int w = 0; w < workers; w++) {
            pool.execute(
                    () -> {
                        while (!isStopping() && failure.get() == null) {
                            int i = next.getAndIncrement();
                            if (i >= pending.size()) return;
                            try {
                                fetchWithRetry(pending.get(i));
                            } catch (IOException e) {
                                failure.compareAndSet(
                                        null,
                                        e.getMessage() == null ? "connection failed" : e.getMessage());
                            }
                        }
                    });
        }
        pool.shutdown();
        try {
            while (!pool.awaitTermination(1, TimeUnit.SECONDS)) {
                if (mCancelRequested) pool.shutdownNow();
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            pool.shutdownNow();
        }
        return (mCancelRequested || mPauseRequested) ? null : failure.get();
    }

    private void fetchWithRetry(Part p) throws IOException {
        int attempt = 0;
        while (!isStopping()) {
            try {
                fetchOnce(p);
                return;
            } catch (IOException | GeneralSecurityException e) {
                if (isStopping()) return;
                String msg = e.getMessage() == null ? "" : e.getMessage();
                // Expired tokens and missing segments won't heal by retrying.
                boolean fatal = msg.startsWith("HTTP 4") && !msg.startsWith("HTTP 429")
                        && !msg.startsWith("HTTP 408");
                attempt++;
                if (fatal || attempt > MAX_RETRIES) {
                    throw e instanceof IOException ? (IOException) e : new IOException("could not decrypt a segment");
                }
                long backoff = Math.min(15_000L, 800L << Math.min(attempt, 5));
                try {
                    Thread.sleep(backoff);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
    }

    private void fetchOnce(Part p) throws IOException, GeneralSecurityException {
        File tmp = new File(p.file.getPath() + ".tmp");
        HttpURLConnection c = StreamHttp.open(p.url, mHeaders, p.range);
        long got = 0;
        try (InputStream in = c.getInputStream();
                OutputStream out = new FileOutputStream(tmp)) {
            if (p.range != null && c.getResponseCode() != 206) {
                throw new IOException("server ignored the byte range");
            }
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                if (isStopping()) break;
                out.write(buf, 0, n);
                got += n;
                mItem.streamBytes.addAndGet(n);
            }
        } catch (IOException e) {
            mItem.streamBytes.addAndGet(-got);
            tmp.delete();
            throw e;
        } finally {
            c.disconnect();
        }
        if (isStopping()) {
            mItem.streamBytes.addAndGet(-got);
            tmp.delete();
            return;
        }
        if (p.key != null) decryptInPlace(tmp, p);
        if (!tmp.renameTo(p.file)) throw new IOException("could not write a segment");
        mItem.markStreamPart(p.index);
    }

    private void decryptInPlace(File file, Part p) throws IOException, GeneralSecurityException {
        byte[] key = key(p.key.url);
        byte[] iv = p.key.iv;
        if (iv == null) {
            iv = new byte[16];
            long seq = p.sequence;
            for (int i = 15; i >= 8; i--) {
                iv[i] = (byte) (seq & 0xff);
                seq >>>= 8;
            }
        }
        byte[] data;
        try (InputStream in = new FileInputStream(file)) {
            data = StreamHttp.readAll(in, Integer.MAX_VALUE - 16);
        }
        Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
        cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new IvParameterSpec(iv));
        byte[] plain = cipher.doFinal(data);
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(plain);
        }
    }

    private byte[] key(String url) throws IOException {
        synchronized (mKeys) {
            byte[] k = mKeys.get(url);
            if (k != null) return k;
        }
        byte[] k = StreamHttp.getBytes(url, mHeaders, null, MAX_KEY_BYTES);
        if (k.length != 16) throw new IOException("unexpected AES key");
        synchronized (mKeys) {
            mKeys.put(url, k);
        }
        return k;
    }

    // ── joining ──

    private File assemble(List<Track> tracks) throws IOException {
        List<File> joined = new ArrayList<>();
        for (Track t : tracks) {
            File f = new File(mDir, "track_" + t.name + (t.ts ? ".ts" : ".mp4"));
            try (OutputStream out = new FileOutputStream(f)) {
                if (t.init != null) append(t.init.file, out);
                for (Part p : t.parts) {
                    if (isStopping()) return f;
                    append(p.file, out);
                }
            }
            joined.add(f);
        }
        boolean audioOnly = tracks.size() == 1 && "a".equals(tracks.get(0).name);
        boolean webm = MediaRemuxer.needsWebm(joined);
        String ext = webm ? "webm" : audioOnly ? "m4a" : "mp4";
        File out = new File(mDir, "output." + ext);
        try {
            MediaRemuxer.remux(joined, out, webm);
        } catch (IOException e) {
            out.delete();
            if (joined.size() > 1) {
                throw new IOException("could not merge the audio into the video (" + e.getMessage() + ")");
            }
            // One track: keep it as it came ("merge as TS" / fragmented MP4 as-is).
            Track t = tracks.get(0);
            Log.i(TAG, "remux failed (%s); keeping the joined %s", e.getMessage(), t.ts ? "TS" : "MP4");
            out = joined.get(0);
            ext = t.ts ? "ts" : audioOnly ? "m4a" : "mp4";
        }
        setOutputName(ext);
        for (Track t : tracks) {
            if (t.init != null) t.init.file.delete();
            for (Part p : t.parts) p.file.delete();
        }
        for (File f : joined) if (!f.equals(out)) f.delete();
        return out;
    }

    private void setOutputName(String ext) {
        String name = mItem.fileName;
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        mItem.fileName = name + "." + ext;
        switch (ext) {
            case "ts":
                mItem.mimeType = "video/mp2t";
                break;
            case "webm":
                mItem.mimeType = "video/webm";
                break;
            case "m4a":
                mItem.mimeType = "audio/mp4";
                break;
            default:
                mItem.mimeType = "video/mp4";
        }
    }

    private static void append(File f, OutputStream out) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[256 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private void finishStopped() {
        if (mCancelRequested) {
            mListener.onCancelled(mItem);
        } else {
            mListener.onPaused(mItem);
        }
    }
}
