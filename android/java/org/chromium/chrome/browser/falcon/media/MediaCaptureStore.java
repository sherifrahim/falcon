/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.media;

import android.net.Uri;
import android.os.Handler;
import android.os.Looper;

import org.chromium.base.Log;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DashManifest;
import org.chromium.chrome.browser.falcon.download.HlsPlaylist;
import org.chromium.chrome.browser.falcon.download.StreamHttp;
import org.chromium.chrome.browser.falcon.media.CapturedMedia.Probe;
import org.chromium.chrome.browser.falcon.media.CapturedMedia.Variant;
import org.chromium.content_public.browser.WebContents;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The media grabber's memory: what each tab's current page has loaded (1DM's "captured links"),
 * with playlists probed in the background for their qualities, duration and DRM. UI thread.
 */
public final class MediaCaptureStore {
    private static final String TAG = "FalconGrabber";
    private static final int MAX_PER_PAGE = 60;

    public interface Observer {
        void onCapturesChanged(WebContents webContents);
    }

    private static final class Page {
        final String url;
        final List<CapturedMedia> items = new ArrayList<>();

        Page(String url) {
            this.url = url;
        }
    }

    private static MediaCaptureStore sInstance;

    private final Map<WebContents, Page> mPages = new WeakHashMap<>();
    private final ObserverList<Observer> mObservers = new ObserverList<>();
    private final ExecutorService mProbePool = Executors.newFixedThreadPool(2);
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    public static MediaCaptureStore getInstance() {
        ThreadUtils.assertOnUiThread();
        if (sInstance == null) sInstance = new MediaCaptureStore();
        return sInstance;
    }

    public void addObserver(Observer o) {
        mObservers.addObserver(o);
    }

    public void removeObserver(Observer o) {
        mObservers.removeObserver(o);
    }

    /** What the grabber lists for this tab: newest first, child playlists folded away. */
    public List<CapturedMedia> visible(WebContents webContents) {
        Page page = webContents == null ? null : mPages.get(webContents);
        List<CapturedMedia> out = new ArrayList<>();
        if (page == null) return out;
        for (CapturedMedia m : page.items) if (!m.hidden) out.add(m);
        Collections.reverse(out);
        return out;
    }

    public int count(WebContents webContents) {
        return visible(webContents).size();
    }

    public void clear(WebContents webContents) {
        Page page = mPages.get(webContents);
        if (page == null) return;
        page.items.clear();
        notifyChanged(webContents);
    }

    void onPageChanged(WebContents webContents, String pageUrl) {
        Page old = mPages.put(webContents, new Page(pageUrl));
        if (old != null && !old.items.isEmpty()) notifyChanged(webContents);
    }

    void onCaptured(WebContents webContents, CapturedMedia media) {
        if (!FalconPrefs.isMediaCaptureEnabled()) return;
        if (FalconPrefs.isMediaCaptureOffFor(Uri.parse(media.pageUrl).getHost())) return;
        Page page = mPages.get(webContents);
        if (page == null) {
            page = new Page(media.pageUrl);
            mPages.put(webContents, page);
        }
        String key = media.key();
        for (CapturedMedia m : page.items) {
            if (m.key().equals(key)) return;
        }
        if (page.items.size() >= MAX_PER_PAGE) return;
        media.hidden = isChildOfListed(page, key);
        page.items.add(media);
        notifyChanged(webContents);
        probe(webContents, media);
    }

    private static boolean isChildOfListed(Page page, String key) {
        for (CapturedMedia m : page.items) {
            if (m.childKeys.contains(key)) return true;
        }
        return false;
    }

    private void notifyChanged(WebContents webContents) {
        for (Observer o : mObservers) o.onCapturesChanged(webContents);
    }

    // ── probing (background) ──

    private void probe(WebContents webContents, CapturedMedia media) {
        mProbePool.execute(
                () -> {
                    try {
                        switch (media.kind) {
                            case CapturedMedia.Kind.HLS:
                                probeHls(media);
                                break;
                            case CapturedMedia.Kind.DASH:
                                probeDash(media);
                                break;
                            default:
                                probeFile(media);
                        }
                    } catch (IOException | RuntimeException e) {
                        Log.i(TAG, "probe failed for %s: %s", media.host(), e.getMessage());
                        media.probe = Probe.FAILED;
                        media.note = "couldn't read the stream";
                    }
                    mHandler.post(() -> onProbed(webContents, media));
                });
    }

    private void onProbed(WebContents webContents, CapturedMedia media) {
        Page page = mPages.get(webContents);
        if (page == null || !page.items.contains(media)) return;
        // A master lists its variant playlists: fold those that were captured separately.
        if (!media.childKeys.isEmpty()) {
            Set<String> children = new HashSet<>(media.childKeys);
            for (CapturedMedia m : page.items) {
                if (m != media && children.contains(m.key())) m.hidden = true;
            }
        }
        notifyChanged(webContents);
    }

    private static StreamHttp.Headers headers(CapturedMedia m) {
        return new StreamHttp.Headers(m.referer, m.userAgent, m.cookies, m.url);
    }

    private static void probeFile(CapturedMedia m) {
        long size = StreamHttp.probeSize(m.url, headers(m));
        if (size > 0) m.size = size;
        m.variants = Collections.singletonList(new Variant("Original", 0, 0, m.isAudio(), "", m.size));
        m.probe = Probe.READY;
    }

    private static void probeHls(CapturedMedia m) throws IOException {
        StreamHttp.Headers h = headers(m);
        StreamHttp.Text text = StreamHttp.getText(m.url, h);
        HlsPlaylist pl = HlsPlaylist.parse(text.body, text.finalUrl);
        List<Variant> variants = new ArrayList<>();
        if (!pl.isMaster) {
            if (!describeMedia(m, pl)) return;
            variants.add(new Variant("Original", 0, 0, false, hlsSpec(m.url, ""), -1));
            m.variants = variants;
            m.probe = Probe.READY;
            return;
        }
        List<String> children = new ArrayList<>();
        for (HlsPlaylist.Variant v : pl.variants) children.add(CapturedMedia.key(v.url));
        for (HlsPlaylist.Rendition r : pl.renditions) {
            if (!r.url.isEmpty()) children.add(CapturedMedia.key(r.url));
        }
        m.childKeys = children;
        if (pl.variants.isEmpty() && pl.renditions.isEmpty()) {
            m.probe = Probe.FAILED;
            m.note = "empty playlist";
            return;
        }

        // Duration, DRM and live-ness come from a media playlist; the best variant's will do.
        List<HlsPlaylist.Variant> sorted = new ArrayList<>(pl.variants);
        Collections.sort(
                sorted,
                (a, b) -> a.height != b.height
                        ? Integer.compare(b.height, a.height)
                        : Long.compare(b.bandwidth, a.bandwidth));
        if (!sorted.isEmpty()) {
            StreamHttp.Text media = StreamHttp.getText(sorted.get(0).url, h);
            if (!describeMedia(m, HlsPlaylist.parse(media.body, media.finalUrl))) return;
        }

        Set<String> labels = new HashSet<>();
        for (HlsPlaylist.Variant v : sorted) {
            if (v.url == null) continue;
            HlsPlaylist.Rendition audio = pl.audioFor(v.audioGroup);
            String label = qualityLabel(v.height, v.bandwidth);
            if (!labels.add(label)) continue;
            variants.add(
                    new Variant(label, v.height, v.bandwidth, false,
                            hlsSpec(v.url, audio == null ? "" : audio.url),
                            estimate(v.bandwidth, m.durationSeconds)));
        }
        Set<String> audioUrls = new HashSet<>();
        for (HlsPlaylist.Rendition r : pl.renditions) {
            if (!"AUDIO".equals(r.type) || r.url.isEmpty() || !audioUrls.add(r.url)) continue;
            String name = r.name.isEmpty() ? r.language : r.name;
            variants.add(
                    new Variant(name.isEmpty() ? "Audio only" : "Audio · " + name, 0, 0, true,
                            hlsSpec("", r.url), -1));
        }
        m.variants = variants;
        m.probe = variants.isEmpty() ? Probe.FAILED : Probe.READY;
    }

    /** Fills duration; returns false (and marks the capture) when it can't be downloaded. */
    private static boolean describeMedia(CapturedMedia m, HlsPlaylist media) {
        m.durationSeconds = media.totalDuration();
        if (media.isDrm()) {
            m.probe = Probe.UNSUPPORTED;
            m.note = "DRM-protected";
            return false;
        }
        if (!media.endList) {
            m.probe = Probe.UNSUPPORTED;
            m.note = "live stream";
            return false;
        }
        return true;
    }

    private static void probeDash(CapturedMedia m) throws IOException {
        StreamHttp.Text text = StreamHttp.getText(m.url, headers(m));
        DashManifest mpd = DashManifest.parse(text.body, text.finalUrl);
        m.durationSeconds = mpd.durationSeconds;
        if (mpd.dynamic) {
            m.probe = Probe.UNSUPPORTED;
            m.note = "live stream";
            return;
        }
        List<DashManifest.Representation> video = new ArrayList<>();
        DashManifest.Representation audio = null;
        boolean sawDrm = false;
        for (DashManifest.Representation r : mpd.representations) {
            if (r.drm) {
                sawDrm = true;
                continue;
            }
            if (r.segments.isEmpty()) continue;
            if ("video".equals(r.contentType)) {
                video.add(r);
            } else if ("audio".equals(r.contentType)
                    && (audio == null || r.bandwidth > audio.bandwidth)) {
                audio = r;
            }
        }
        if (video.isEmpty() && audio == null) {
            m.probe = Probe.UNSUPPORTED;
            m.note = sawDrm ? "DRM-protected" : "nothing downloadable";
            return;
        }
        Collections.sort(
                video,
                (a, b) -> a.height != b.height
                        ? Integer.compare(b.height, a.height)
                        : Long.compare(b.bandwidth, a.bandwidth));
        List<Variant> variants = new ArrayList<>();
        Set<String> labels = new HashSet<>();
        long audioBw = audio == null ? 0 : audio.bandwidth;
        for (DashManifest.Representation v : video) {
            String label = qualityLabel(v.height, v.bandwidth);
            if (!labels.add(label)) continue;
            variants.add(
                    new Variant(label, v.height, v.bandwidth, false,
                            dashSpec(m.url, v.id, audio == null ? "" : audio.id),
                            estimate(v.bandwidth + audioBw, m.durationSeconds)));
        }
        if (audio != null) {
            variants.add(
                    new Variant("Audio only", 0, audio.bandwidth, true, dashSpec(m.url, "", audio.id),
                            estimate(audio.bandwidth, m.durationSeconds)));
        }
        m.variants = variants;
        m.probe = Probe.READY;
    }

    private static String qualityLabel(int height, long bandwidth) {
        String mbps = bandwidth > 0 ? String.format(Locale.US, "%.1f Mbps", bandwidth / 1e6) : "";
        if (height > 0) return mbps.isEmpty() ? height + "p" : height + "p · " + mbps;
        return mbps.isEmpty() ? "Original" : mbps;
    }

    private static long estimate(long bandwidth, double seconds) {
        return bandwidth > 0 && seconds > 0 ? (long) (bandwidth / 8.0 * seconds) : -1;
    }

    private static String hlsSpec(String video, String audio) {
        try {
            return new JSONObject().put("t", "hls").put("v", video).put("a", audio).toString();
        } catch (JSONException e) {
            return "";
        }
    }

    private static String dashSpec(String mpd, String video, String audio) {
        try {
            return new JSONObject()
                    .put("t", "dash")
                    .put("m", mpd)
                    .put("v", video)
                    .put("a", audio)
                    .toString();
        } catch (JSONException e) {
            return "";
        }
    }
}
