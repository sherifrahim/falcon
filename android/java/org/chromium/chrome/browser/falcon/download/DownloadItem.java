/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import androidx.annotation.IntDef;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicLong;

/** One download: what to fetch, where it is, and how far each segment got. */
public final class DownloadItem {
    @IntDef({
        State.QUEUED,
        State.ACTIVE,
        State.PAUSED,
        State.WAITING_WIFI,
        State.DONE,
        State.FAILED,
        State.CANCELLED
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface State {
        int QUEUED = 0;
        int ACTIVE = 1;
        int PAUSED = 2;
        int WAITING_WIFI = 3;
        int DONE = 4;
        int FAILED = 5;
        int CANCELLED = 6;
    }

    /** A byte range of the file fetched by one connection. */
    public static final class Segment {
        public final long start;
        public final long end; // inclusive; -1 = open-ended (size unknown)
        public volatile long done;

        public Segment(long start, long end, long done) {
            this.start = start;
            this.end = end;
            this.done = done;
        }

        public long length() {
            return end < 0 ? -1 : end - start + 1;
        }

        public boolean isComplete() {
            return end >= 0 && done >= length();
        }
    }

    public final String id;
    public final String url;
    public final String referer;
    public final String userAgent;
    public final String cookies;
    public volatile String mimeType;
    public final long createdAt;

    public volatile String fileName;
    public volatile long totalBytes; // -1 while unknown
    public volatile @State int state;
    public volatile String error = "";
    public volatile String etag = "";
    public volatile boolean resumable;
    public volatile String sha256Expected = "";
    public volatile String sha256Actual = "";
    /** content:// URI once exported to the device's Downloads, else "". */
    public volatile String savedUri = "";
    public volatile long finishedAt;

    /**
     * HLS/DASH download ({@link StreamDownloadTask}): JSON naming the playlists or manifest and
     * the chosen tracks; "" for a plain HTTP file.
     */
    public volatile String streamSpec = "";
    /** Stream progress: segments in total / fetched, and bytes fetched. */
    public volatile int streamParts;
    public volatile int streamPartsDone;
    public final AtomicLong streamBytes = new AtomicLong();
    private boolean[] mStreamDoneMap;

    /** Live telemetry (not persisted). */
    public volatile long speedBps;
    public volatile long etaSeconds = -1;

    private final Object mLock = new Object();
    private final List<Segment> mSegments = new ArrayList<>();

    public DownloadItem(
            String id,
            String url,
            String referer,
            String userAgent,
            String cookies,
            String fileName,
            String mimeType,
            long totalBytes,
            long createdAt) {
        this.id = id;
        this.url = url;
        this.referer = referer == null ? "" : referer;
        this.userAgent = userAgent == null ? "" : userAgent;
        this.cookies = cookies == null ? "" : cookies;
        this.fileName = fileName;
        this.mimeType = mimeType == null ? "" : mimeType;
        this.totalBytes = totalBytes;
        this.createdAt = createdAt;
        this.state = State.QUEUED;
    }

    public List<Segment> segments() {
        synchronized (mLock) {
            return new ArrayList<>(mSegments);
        }
    }

    public void setSegments(List<Segment> segments) {
        synchronized (mLock) {
            mSegments.clear();
            mSegments.addAll(segments);
        }
    }

    public boolean hasSegments() {
        synchronized (mLock) {
            return !mSegments.isEmpty();
        }
    }

    public boolean isStream() {
        return !streamSpec.isEmpty();
    }

    /** A stream run starts: |parts| segments, none known to be fetched yet. */
    void startStream(int parts) {
        synchronized (mLock) {
            mStreamDoneMap = new boolean[parts];
            streamParts = parts;
            streamPartsDone = 0;
        }
    }

    void markStreamPart(int index) {
        synchronized (mLock) {
            if (mStreamDoneMap == null || index < 0 || index >= mStreamDoneMap.length) return;
            if (mStreamDoneMap[index]) return;
            mStreamDoneMap[index] = true;
            int done = streamPartsDone + 1;
            streamPartsDone = done;
            // Size is unknown until the end: extrapolate from what has landed.
            long bytes = streamBytes.get();
            if (done > 0 && bytes > 0) totalBytes = bytes * streamParts / done;
        }
    }

    /** Which segments are fetched (null before a run has listed them). */
    public boolean[] streamDoneMap() {
        synchronized (mLock) {
            return mStreamDoneMap == null ? null : mStreamDoneMap.clone();
        }
    }

    void resetStream() {
        synchronized (mLock) {
            mStreamDoneMap = null;
            streamParts = 0;
            streamPartsDone = 0;
            streamBytes.set(0);
        }
    }

    public long doneBytes() {
        if (isStream()) return streamBytes.get();
        long done = 0;
        for (Segment s : segments()) done += s.done;
        return done;
    }

    public int progressPercent() {
        if (isStream()) {
            if (state == State.DONE) return 100;
            int parts = streamParts;
            return parts <= 0 ? 0 : Math.min(100, streamPartsDone * 100 / parts);
        }
        long total = totalBytes;
        if (total <= 0) return 0;
        return (int) Math.min(100, doneBytes() * 100 / total);
    }

    public boolean isActive() {
        return state == State.ACTIVE;
    }

    public boolean isTerminal() {
        return state == State.DONE || state == State.FAILED || state == State.CANCELLED;
    }

    public String partFileName() {
        return id + ".falcon-part";
    }

    // ── persistence ──

    public JSONObject toJson() throws JSONException {
        JSONObject o = new JSONObject();
        o.put("id", id);
        o.put("url", url);
        o.put("referer", referer);
        o.put("userAgent", userAgent);
        o.put("cookies", cookies);
        o.put("fileName", fileName);
        o.put("mimeType", mimeType);
        o.put("totalBytes", totalBytes);
        o.put("createdAt", createdAt);
        o.put("state", state);
        o.put("error", error);
        o.put("etag", etag);
        o.put("resumable", resumable);
        o.put("sha256Expected", sha256Expected);
        o.put("sha256Actual", sha256Actual);
        o.put("savedUri", savedUri);
        o.put("finishedAt", finishedAt);
        if (isStream()) {
            o.put("streamSpec", streamSpec);
            o.put("streamParts", streamParts);
            o.put("streamPartsDone", streamPartsDone);
            o.put("streamBytes", streamBytes.get());
        }
        JSONArray segs = new JSONArray();
        for (Segment s : segments()) {
            JSONObject so = new JSONObject();
            so.put("start", s.start);
            so.put("end", s.end);
            so.put("done", s.done);
            segs.put(so);
        }
        o.put("segments", segs);
        return o;
    }

    public static DownloadItem fromJson(JSONObject o) throws JSONException {
        DownloadItem item =
                new DownloadItem(
                        o.getString("id"),
                        o.getString("url"),
                        o.optString("referer", ""),
                        o.optString("userAgent", ""),
                        o.optString("cookies", ""),
                        o.getString("fileName"),
                        o.optString("mimeType", ""),
                        o.optLong("totalBytes", -1),
                        o.optLong("createdAt", 0));
        int state = o.optInt("state", State.QUEUED);
        // A download that was running when the process died resumes as paused.
        item.state = state == State.ACTIVE || state == State.QUEUED ? State.PAUSED : state;
        item.error = o.optString("error", "");
        item.etag = o.optString("etag", "");
        item.resumable = o.optBoolean("resumable", false);
        item.sha256Expected = o.optString("sha256Expected", "");
        item.sha256Actual = o.optString("sha256Actual", "");
        item.savedUri = o.optString("savedUri", "");
        item.finishedAt = o.optLong("finishedAt", 0);
        item.streamSpec = o.optString("streamSpec", "");
        item.streamParts = o.optInt("streamParts", 0);
        item.streamPartsDone = o.optInt("streamPartsDone", 0);
        item.streamBytes.set(o.optLong("streamBytes", 0));
        JSONArray segs = o.optJSONArray("segments");
        List<Segment> list = new ArrayList<>();
        if (segs != null) {
            for (int i = 0; i < segs.length(); i++) {
                JSONObject so = segs.getJSONObject(i);
                list.add(
                        new Segment(
                                so.getLong("start"), so.getLong("end"), so.optLong("done", 0)));
            }
        }
        item.setSegments(list);
        return item;
    }

    /** "4.1 GB of 6.6 GB" style text for notifications. */
    public static String formatBytes(long bytes) {
        if (bytes < 0) return "?";
        if (bytes < 1024) return bytes + " B";
        double kb = bytes / 1024.0;
        if (kb < 1024) return String.format(Locale.US, "%.0f KB", kb);
        double mb = kb / 1024.0;
        if (mb < 1024) return String.format(Locale.US, "%.1f MB", mb);
        return String.format(Locale.US, "%.2f GB", mb / 1024.0);
    }
}
