/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.net.Uri;
import android.text.TextUtils;

import org.chromium.net.ChromiumNetworkAdapter;
import org.chromium.net.NetworkTrafficAnnotationTag;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * Plain HTTP for the media grabber: playlists, keys and segments, fetched with the headers the
 * page used. The page's cookies only ever go to the host they were collected for.
 */
public final class StreamHttp {
    private static final int CONNECT_TIMEOUT_MS = 20_000;
    private static final int READ_TIMEOUT_MS = 30_000;
    private static final int MAX_REDIRECTS = 6;
    private static final int MAX_TEXT_BYTES = 8 * 1024 * 1024;
    private static final NetworkTrafficAnnotationTag TRAFFIC_ANNOTATION =
            NetworkTrafficAnnotationTag.createComplete(
                    "falcon_media_grabber",
                    "semantics {"
                            + "  sender: 'Falcon media grabber'"
                            + "  description: 'Fetches the playlist, keys and segments of a"
                            + " video or audio stream the page played, to list its qualities and"
                            + " to download the one the user picked.'"
                            + "  trigger: 'A page loaded an HLS or DASH stream, or the user"
                            + " chose to download a captured stream.'"
                            + "  data: 'The stream URLs, referer, user agent and the cookies"
                            + " for the stream host.'"
                            + "  destination: WEBSITE"
                            + "}"
                            + "policy {"
                            + "  cookies_allowed: YES"
                            + "  setting: 'Settings > Falcon > Capture audio & video.'"
                            + "  policy_exception_justification: 'Personal fork; no policy.'"
                            + "}");

    /** Request headers borrowed from the page. */
    public static final class Headers {
        public final String referer;
        public final String userAgent;
        public final String cookies;
        public final String cookieHost;

        public Headers(String referer, String userAgent, String cookies, String cookieUrl) {
            this.referer = referer == null ? "" : referer;
            this.userAgent = userAgent == null ? "" : userAgent;
            this.cookies = cookies == null ? "" : cookies;
            String host = cookieUrl == null ? null : Uri.parse(cookieUrl).getHost();
            this.cookieHost = host == null ? "" : host;
        }
    }

    /** A text body and the URL it finally came from (relative links resolve against it). */
    public static final class Text {
        public final String body;
        public final String finalUrl;

        Text(String body, String finalUrl) {
            this.body = body;
            this.finalUrl = finalUrl;
        }
    }

    private StreamHttp() {}

    /** Opens |url|, following redirects across http/https; the caller must disconnect. */
    public static HttpURLConnection open(String url, Headers headers, String range)
            throws IOException {
        String current = url;
        for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
            HttpURLConnection c =
                    (HttpURLConnection)
                            ChromiumNetworkAdapter.openConnection(new URL(current), TRAFFIC_ANNOTATION);
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(CONNECT_TIMEOUT_MS);
            c.setReadTimeout(READ_TIMEOUT_MS);
            c.setUseCaches(false);
            if (!TextUtils.isEmpty(headers.userAgent)) {
                c.setRequestProperty("User-Agent", headers.userAgent);
            }
            if (!TextUtils.isEmpty(headers.referer)) c.setRequestProperty("Referer", headers.referer);
            String host = Uri.parse(current).getHost();
            if (!TextUtils.isEmpty(headers.cookies) && host != null
                    && host.equalsIgnoreCase(headers.cookieHost)) {
                c.setRequestProperty("Cookie", headers.cookies);
            }
            c.setRequestProperty("Accept-Encoding", "identity");
            if (range != null) c.setRequestProperty("Range", range);
            int code = c.getResponseCode();
            if (code >= 300 && code < 400 && code != 304) {
                String location = c.getHeaderField("Location");
                c.disconnect();
                if (TextUtils.isEmpty(location)) throw new IOException("HTTP " + code);
                current = new URL(new URL(current), location).toString();
                continue;
            }
            if (code >= 400) {
                c.disconnect();
                throw new IOException("HTTP " + code);
            }
            return c;
        }
        throw new IOException("too many redirects");
    }

    public static Text getText(String url, Headers headers) throws IOException {
        HttpURLConnection c = open(url, headers, null);
        try (InputStream in = c.getInputStream()) {
            byte[] bytes = readAll(in, MAX_TEXT_BYTES);
            return new Text(new String(bytes, StandardCharsets.UTF_8), c.getURL().toString());
        } finally {
            c.disconnect();
        }
    }

    /** A whole (small) body, e.g. an AES key or an init section; |range| may be null. */
    public static byte[] getBytes(String url, Headers headers, String range, int limit)
            throws IOException {
        HttpURLConnection c = open(url, headers, range);
        try (InputStream in = c.getInputStream()) {
            return readAll(in, limit);
        } finally {
            c.disconnect();
        }
    }

    /** Total size from a one-byte range probe, or -1. */
    public static long probeSize(String url, Headers headers) {
        HttpURLConnection c = null;
        try {
            c = open(url, headers, "bytes=0-0");
            String range = c.getHeaderField("Content-Range");
            if (c.getResponseCode() == 206 && range != null && range.contains("/")) {
                String size = range.substring(range.lastIndexOf('/') + 1).trim();
                return "*".equals(size) ? -1 : Long.parseLong(size);
            }
            return c.getContentLengthLong();
        } catch (IOException | NumberFormatException e) {
            return -1;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    static byte[] readAll(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[64 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (out.size() > limit) throw new IOException("response too large");
        }
        return out.toByteArray();
    }

    /** Resolves |ref| against |base| (lenient about the characters real playlists contain). */
    public static String resolve(String base, String ref) {
        try {
            return new URL(new URL(base), ref.trim()).toString();
        } catch (IOException e) {
            return ref.trim();
        }
    }
}
