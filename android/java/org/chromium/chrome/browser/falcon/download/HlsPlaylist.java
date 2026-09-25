/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** An HLS playlist (RFC 8216): either a master (variants) or a media playlist (segments). */
public final class HlsPlaylist {
    public static final class Variant {
        public String url;
        public long bandwidth;
        public int width;
        public int height;
        public String codecs = "";
        public String audioGroup = "";
    }

    /** An EXT-X-MEDIA rendition (alternate audio etc.). */
    public static final class Rendition {
        public String type = "";
        public String groupId = "";
        public String name = "";
        public String language = "";
        public String url = ""; // empty: muxed into the variant
        public boolean isDefault;
    }

    public static final class Key {
        public String method = "NONE";
        public String url = "";
        public byte[] iv; // null: derive from the media sequence number
    }

    public static final class Segment {
        public String url;
        public double duration;
        public long byteStart = -1;
        public long byteLength = -1;
        public long sequence;
        public Key key;
        /** EXT-X-MAP in effect for this segment (fMP4 init section), or null. */
        public Segment map;
    }

    public boolean isMaster;
    public boolean endList;
    public final List<Variant> variants = new ArrayList<>();
    public final List<Rendition> renditions = new ArrayList<>();
    public final List<Segment> segments = new ArrayList<>();

    public double totalDuration() {
        double d = 0;
        for (Segment s : segments) d += s.duration;
        return d;
    }

    /** SAMPLE-AES and key systems (FairPlay, Widevine) are DRM: not downloadable. */
    public boolean isDrm() {
        for (Segment s : segments) {
            if (s.key != null && !"NONE".equals(s.key.method) && !"AES-128".equals(s.key.method)) {
                return true;
            }
        }
        return false;
    }

    public boolean isFragmentedMp4() {
        return !segments.isEmpty() && segments.get(0).map != null;
    }

    /** The default (else first) audio rendition with its own playlist in |group|, or null. */
    public Rendition audioFor(String group) {
        if (group == null || group.isEmpty()) return null;
        Rendition first = null;
        for (Rendition r : renditions) {
            if (!"AUDIO".equals(r.type) || !group.equals(r.groupId) || r.url.isEmpty()) continue;
            if (r.isDefault) return r;
            if (first == null) first = r;
        }
        return first;
    }

    public static HlsPlaylist parse(String text, String baseUrl) {
        HlsPlaylist p = new HlsPlaylist();
        String[] lines = text.replace("\r", "").split("\n");
        Variant pendingVariant = null;
        double pendingDuration = 0;
        long pendingByteStart = -1;
        long pendingByteLength = -1;
        long nextByteStart = 0;
        long sequence = 0;
        Key key = null;
        Segment map = null;
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty()) continue;
            if (line.startsWith("#EXT-X-STREAM-INF:")) {
                p.isMaster = true;
                Map<String, String> a = attributes(line.substring(line.indexOf(':') + 1));
                pendingVariant = new Variant();
                pendingVariant.bandwidth = parseLong(a.get("BANDWIDTH"));
                String res = a.get("RESOLUTION");
                if (res != null && res.contains("x")) {
                    String[] wh = res.toLowerCase(Locale.US).split("x");
                    pendingVariant.width = (int) parseLong(wh[0]);
                    pendingVariant.height = (int) parseLong(wh[1]);
                }
                pendingVariant.codecs = orEmpty(a.get("CODECS"));
                pendingVariant.audioGroup = orEmpty(a.get("AUDIO"));
            } else if (line.startsWith("#EXT-X-MEDIA:")) {
                p.isMaster = true;
                Map<String, String> a = attributes(line.substring(line.indexOf(':') + 1));
                Rendition r = new Rendition();
                r.type = orEmpty(a.get("TYPE"));
                r.groupId = orEmpty(a.get("GROUP-ID"));
                r.name = orEmpty(a.get("NAME"));
                r.language = orEmpty(a.get("LANGUAGE"));
                String uri = a.get("URI");
                r.url = uri == null ? "" : StreamHttp.resolve(baseUrl, uri);
                r.isDefault = "YES".equals(a.get("DEFAULT"));
                p.renditions.add(r);
            } else if (line.startsWith("#EXT-X-MEDIA-SEQUENCE:")) {
                sequence = parseLong(line.substring(line.indexOf(':') + 1));
            } else if (line.startsWith("#EXTINF:")) {
                String v = line.substring(line.indexOf(':') + 1);
                int comma = v.indexOf(',');
                pendingDuration = parseDouble(comma >= 0 ? v.substring(0, comma) : v);
            } else if (line.startsWith("#EXT-X-BYTERANGE:")) {
                String v = line.substring(line.indexOf(':') + 1);
                int at = v.indexOf('@');
                pendingByteLength = parseLong(at >= 0 ? v.substring(0, at) : v);
                pendingByteStart = at >= 0 ? parseLong(v.substring(at + 1)) : nextByteStart;
            } else if (line.startsWith("#EXT-X-KEY:")) {
                Map<String, String> a = attributes(line.substring(line.indexOf(':') + 1));
                key = new Key();
                key.method = orEmpty(a.get("METHOD")).toUpperCase(Locale.US);
                String uri = a.get("URI");
                key.url = uri == null ? "" : StreamHttp.resolve(baseUrl, uri);
                key.iv = parseIv(a.get("IV"));
                if ("NONE".equals(key.method)) key = null;
            } else if (line.startsWith("#EXT-X-MAP:")) {
                Map<String, String> a = attributes(line.substring(line.indexOf(':') + 1));
                map = new Segment();
                map.url = StreamHttp.resolve(baseUrl, orEmpty(a.get("URI")));
                String br = a.get("BYTERANGE");
                if (br != null) {
                    int at = br.indexOf('@');
                    map.byteLength = parseLong(at >= 0 ? br.substring(0, at) : br);
                    map.byteStart = at >= 0 ? parseLong(br.substring(at + 1)) : 0;
                }
            } else if (line.startsWith("#EXT-X-ENDLIST")) {
                p.endList = true;
            } else if (!line.startsWith("#")) {
                String url = StreamHttp.resolve(baseUrl, line);
                if (pendingVariant != null) {
                    pendingVariant.url = url;
                    p.variants.add(pendingVariant);
                    pendingVariant = null;
                } else {
                    Segment s = new Segment();
                    s.url = url;
                    s.duration = pendingDuration;
                    s.byteStart = pendingByteStart;
                    s.byteLength = pendingByteLength;
                    s.sequence = sequence++;
                    s.key = key;
                    s.map = map;
                    p.segments.add(s);
                    if (pendingByteLength >= 0) nextByteStart = pendingByteStart + pendingByteLength;
                    pendingDuration = 0;
                    pendingByteStart = -1;
                    pendingByteLength = -1;
                }
            }
        }
        if (p.isMaster) p.endList = true; // masters are not live, their media playlists may be
        return p;
    }

    /** Parses an attribute list: KEY=value,KEY="quoted, with commas". */
    static Map<String, String> attributes(String s) {
        Map<String, String> out = new HashMap<>();
        int i = 0;
        int n = s.length();
        while (i < n) {
            int eq = s.indexOf('=', i);
            if (eq < 0) break;
            String name = s.substring(i, eq).trim().toUpperCase(Locale.US);
            int j = eq + 1;
            String value;
            if (j < n && s.charAt(j) == '"') {
                int end = s.indexOf('"', j + 1);
                if (end < 0) end = n;
                value = s.substring(j + 1, end);
                j = end + 1;
                int comma = s.indexOf(',', j);
                j = comma < 0 ? n : comma + 1;
            } else {
                int comma = s.indexOf(',', j);
                int end = comma < 0 ? n : comma;
                value = s.substring(j, end).trim();
                j = comma < 0 ? n : comma + 1;
            }
            out.put(name, value);
            i = j;
        }
        return out;
    }

    private static byte[] parseIv(String hex) {
        if (hex == null) return null;
        String h = hex.trim();
        if (h.startsWith("0x") || h.startsWith("0X")) h = h.substring(2);
        if (h.isEmpty() || h.length() > 32) return null;
        while (h.length() < 32) h = "0" + h;
        byte[] iv = new byte[16];
        try {
            for (int i = 0; i < 16; i++) {
                iv[i] = (byte) Integer.parseInt(h.substring(i * 2, i * 2 + 2), 16);
            }
        } catch (NumberFormatException e) {
            return null;
        }
        return iv;
    }

    private static String orEmpty(String s) {
        return s == null ? "" : s;
    }

    private static long parseLong(String s) {
        if (s == null) return 0;
        try {
            return Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static double parseDouble(String s) {
        try {
            return Double.parseDouble(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}
