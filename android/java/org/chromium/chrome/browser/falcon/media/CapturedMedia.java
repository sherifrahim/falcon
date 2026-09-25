/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.media;

import android.net.Uri;
import android.text.TextUtils;

import androidx.annotation.IntDef;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * One audio/video resource a page loaded: a plain file, an HLS playlist or a DASH manifest. Once
 * probed, playlists carry the qualities the user can pick from.
 */
public final class CapturedMedia {
    /** Matches JavaKind() in falcon_media_capture_tab_helper.cc. */
    @IntDef({Kind.FILE, Kind.HLS, Kind.DASH})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Kind {
        int FILE = 0;
        int HLS = 1;
        int DASH = 2;
    }

    @IntDef({Probe.PENDING, Probe.READY, Probe.FAILED, Probe.UNSUPPORTED})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Probe {
        int PENDING = 0;
        int READY = 1;
        int FAILED = 2;
        /** DRM, SAMPLE-AES or a live-only stream: listed but not downloadable. */
        int UNSUPPORTED = 3;
    }

    /** One downloadable choice: a quality of a stream, or the file itself. */
    public static final class Variant {
        /** "1080p", "720p · 2.8 Mbps", "Audio · English". */
        public final String label;
        public final int height;
        public final long bandwidth;
        public final boolean audioOnly;
        /** Stream spec for the downloader (JSON), or "" for a plain file. */
        public final String spec;
        /** Rough size from bandwidth × duration; -1 when unknown. */
        public final long estimatedBytes;

        public Variant(
                String label,
                int height,
                long bandwidth,
                boolean audioOnly,
                String spec,
                long estimatedBytes) {
            this.label = label;
            this.height = height;
            this.bandwidth = bandwidth;
            this.audioOnly = audioOnly;
            this.spec = spec;
            this.estimatedBytes = estimatedBytes;
        }
    }

    public final @Kind int kind;
    public final String url;
    public final String mimeType;
    public final String pageUrl;
    public final String referer;
    public final String userAgent;
    /** Cookies for {@link #url}'s host only; never sent to other hosts. */
    public final String cookies;
    public final long capturedAt = System.currentTimeMillis();

    public volatile String pageTitle;
    public volatile long size; // bytes; -1 / 0 when unknown
    public volatile @Probe int probe = Probe.PENDING;
    public volatile String note = "";
    public volatile double durationSeconds = -1;
    /** A variant/rendition playlist of a master that is also listed: shown under the master. */
    public volatile boolean hidden;
    /** For a master playlist: host+path of the child playlists it references. */
    public volatile List<String> childKeys = new ArrayList<>();
    public volatile List<Variant> variants = new ArrayList<>();

    public CapturedMedia(
            @Kind int kind,
            String url,
            String mimeType,
            long size,
            String pageUrl,
            String pageTitle,
            String referer,
            String userAgent,
            String cookies) {
        this.kind = kind;
        this.url = url;
        this.mimeType = mimeType == null ? "" : mimeType;
        this.size = size;
        this.pageUrl = pageUrl == null ? "" : pageUrl;
        this.pageTitle = pageTitle == null ? "" : pageTitle;
        this.referer = referer == null ? "" : referer;
        this.userAgent = userAgent == null ? "" : userAgent;
        this.cookies = cookies == null ? "" : cookies;
    }

    /** host+path: the same resource with a fresh token query is the same resource. */
    public static String key(String url) {
        Uri u = Uri.parse(url);
        return (u.getHost() == null ? "" : u.getHost()) + (u.getPath() == null ? "" : u.getPath());
    }

    public String key() {
        return key(url);
    }

    public boolean isStream() {
        return kind != Kind.FILE;
    }

    public boolean isAudio() {
        if (mimeType.startsWith("audio/")) return true;
        String ext = extension();
        return ext.equals("mp3") || ext.equals("m4a") || ext.equals("aac") || ext.equals("ogg")
                || ext.equals("oga") || ext.equals("opus") || ext.equals("flac")
                || ext.equals("wav") || ext.equals("wma");
    }

    public boolean isDownloadable() {
        return probe != Probe.UNSUPPORTED && (kind == Kind.FILE || !variants.isEmpty());
    }

    /** "HLS", "DASH", "MP4", "MP3"... for the row tag. */
    public String typeTag() {
        if (kind == Kind.HLS) return "HLS";
        if (kind == Kind.DASH) return "DASH";
        String ext = extension();
        if (!ext.isEmpty() && ext.length() <= 4) return ext.toUpperCase(Locale.US);
        if (mimeType.contains("/")) {
            String sub = mimeType.substring(mimeType.indexOf('/') + 1);
            return (sub.length() > 4 ? sub.substring(0, 4) : sub).toUpperCase(Locale.US);
        }
        return isAudio() ? "AUDIO" : "VIDEO";
    }

    public String host() {
        String h = Uri.parse(url).getHost();
        return h == null ? "" : h;
    }

    private String extension() {
        String last = Uri.parse(url).getLastPathSegment();
        if (last == null) return "";
        int dot = last.lastIndexOf('.');
        return dot < 0 ? "" : last.substring(dot + 1).toLowerCase(Locale.US);
    }

    /**
     * A file name for the download: the page title for streams (their URLs say "index.m3u8"),
     * the URL's own name for files that have one.
     */
    public String suggestedName(Variant variant) {
        String base;
        String last = Uri.parse(url).getLastPathSegment();
        if (kind == Kind.FILE && !TextUtils.isEmpty(last) && last.contains(".")) {
            return sanitize(last);
        }
        base = !TextUtils.isEmpty(pageTitle) ? pageTitle : host();
        base = sanitize(base);
        if (base.length() > 120) base = base.substring(0, 120).trim();
        if (variant != null && variant.height > 0) base += " " + variant.height + "p";
        String ext =
                kind == Kind.FILE
                        ? (isAudio() ? "mp3" : "mp4")
                        : (variant != null && variant.audioOnly ? "m4a" : "mp4");
        return base + "." + ext;
    }

    static String sanitize(String name) {
        String n = name.replaceAll("[\\\\/:*?\"<>|\\x00-\\x1f]", "_").trim();
        while (n.startsWith(".")) n = n.substring(1);
        return n.isEmpty() ? "video" : n;
    }

    /** "12:04", "1:02:33". */
    public static String formatDuration(double seconds) {
        if (seconds < 0) return "";
        long s = Math.round(seconds);
        long h = s / 3600;
        long m = (s % 3600) / 60;
        long sec = s % 60;
        return h > 0
                ? String.format(Locale.US, "%d:%02d:%02d", h, m, sec)
                : String.format(Locale.US, "%d:%02d", m, sec);
    }
}
