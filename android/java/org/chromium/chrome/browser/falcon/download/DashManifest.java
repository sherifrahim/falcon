/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.util.Xml;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A static MPEG-DASH manifest reduced to what a downloader needs: every representation of the
 * first period with its init section and media segment URLs. Covers SegmentTemplate (with or
 * without SegmentTimeline), SegmentList and SegmentBase / single-file representations.
 */
public final class DashManifest {
    public static final class Seg {
        public final String url;
        public final long start; // byte range, -1 = whole resource
        public final long end;

        Seg(String url, long start, long end) {
            this.url = url;
            this.start = start;
            this.end = end;
        }

        public String range() {
            return start < 0 ? null : "bytes=" + start + "-" + (end < 0 ? "" : String.valueOf(end));
        }
    }

    public static final class Representation {
        public String id = "";
        public String contentType = ""; // video / audio / text
        public String mimeType = "";
        public String codecs = "";
        public String language = "";
        public long bandwidth;
        public int width;
        public int height;
        public boolean drm;
        public Seg init; // may be null
        public final List<Seg> segments = new ArrayList<>();
    }

    public boolean dynamic;
    public double durationSeconds = -1;
    public final List<Representation> representations = new ArrayList<>();

    public Representation find(String id) {
        for (Representation r : representations) if (r.id.equals(id)) return r;
        return null;
    }

    // ── a tiny DOM ──

    private static final class Node {
        final String name;
        final Map<String, String> attrs = new HashMap<>();
        final List<Node> children = new ArrayList<>();
        final StringBuilder text = new StringBuilder();

        Node(String name) {
            this.name = name;
        }

        String attr(String key) {
            return attrs.get(key);
        }

        Node child(String childName) {
            for (Node c : children) if (c.name.equals(childName)) return c;
            return null;
        }

        List<Node> all(String childName) {
            List<Node> out = new ArrayList<>();
            for (Node c : children) if (c.name.equals(childName)) out.add(c);
            return out;
        }
    }

    private static Node parseXml(String xml) throws IOException {
        try {
            XmlPullParser p = Xml.newPullParser();
            p.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false);
            p.setInput(new StringReader(xml));
            List<Node> stack = new ArrayList<>();
            Node root = null;
            for (int ev = p.getEventType(); ev != XmlPullParser.END_DOCUMENT; ev = p.next()) {
                if (ev == XmlPullParser.START_TAG) {
                    String name = p.getName();
                    int colon = name.indexOf(':');
                    Node n = new Node(colon >= 0 ? name.substring(colon + 1) : name);
                    for (int i = 0; i < p.getAttributeCount(); i++) {
                        String an = p.getAttributeName(i);
                        int c = an.indexOf(':');
                        n.attrs.put(c >= 0 && !an.startsWith("xmlns") ? an.substring(c + 1) : an,
                                p.getAttributeValue(i));
                    }
                    if (stack.isEmpty()) {
                        root = n;
                    } else {
                        stack.get(stack.size() - 1).children.add(n);
                    }
                    stack.add(n);
                } else if (ev == XmlPullParser.END_TAG) {
                    if (!stack.isEmpty()) stack.remove(stack.size() - 1);
                } else if (ev == XmlPullParser.TEXT && !stack.isEmpty()) {
                    stack.get(stack.size() - 1).text.append(p.getText());
                }
            }
            if (root == null) throw new IOException("empty manifest");
            return root;
        } catch (XmlPullParserException e) {
            throw new IOException("bad manifest: " + e.getMessage());
        }
    }

    // ── parsing ──

    public static DashManifest parse(String xml, String manifestUrl) throws IOException {
        Node mpd = parseXml(xml);
        if (!"MPD".equals(mpd.name)) throw new IOException("not a DASH manifest");
        DashManifest m = new DashManifest();
        m.dynamic = "dynamic".equals(mpd.attr("type"));
        String base = baseUrl(mpd, manifestUrl);
        Node period = mpd.child("Period");
        if (period == null) return m;
        double duration = parseDuration(period.attr("duration"));
        if (duration < 0) duration = parseDuration(mpd.attr("mediaPresentationDuration"));
        m.durationSeconds = duration;
        String periodBase = baseUrl(period, base);
        for (Node set : period.all("AdaptationSet")) {
            String setBase = baseUrl(set, periodBase);
            boolean setDrm = set.child("ContentProtection") != null;
            for (Node rep : set.all("Representation")) {
                Representation r = new Representation();
                r.id = attr(rep, set, "id");
                r.mimeType = attr(rep, set, "mimeType");
                r.codecs = attr(rep, set, "codecs");
                r.language = orEmpty(set.attr("lang"));
                r.bandwidth = parseLong(rep.attr("bandwidth"));
                r.width = (int) parseLong(attr(rep, set, "width"));
                r.height = (int) parseLong(attr(rep, set, "height"));
                String ct = orEmpty(set.attr("contentType"));
                if (ct.isEmpty()) ct = r.mimeType.contains("/") ? r.mimeType.substring(0, r.mimeType.indexOf('/')) : "";
                if (ct.isEmpty() && r.height > 0) ct = "video";
                r.contentType = ct;
                r.drm = setDrm || rep.child("ContentProtection") != null;
                String repBase = baseUrl(rep, setBase);
                buildSegments(r, rep, set, period, repBase, duration);
                m.representations.add(r);
            }
        }
        return m;
    }

    private static void buildSegments(
            Representation r, Node rep, Node set, Node period, String base, double duration) {
        Node tRep = rep.child("SegmentTemplate");
        Node tSet = set.child("SegmentTemplate");
        Node tPeriod = period.child("SegmentTemplate");
        if (tRep != null || tSet != null || tPeriod != null) {
            fromTemplate(r, merge(tPeriod, merge(tSet, tRep)), base, duration);
            return;
        }
        Node list = rep.child("SegmentList");
        if (list == null) list = set.child("SegmentList");
        if (list != null) {
            Node init = list.child("Initialization");
            if (init != null) r.init = seg(base, init.attr("sourceURL"), init.attr("range"));
            for (Node s : list.all("SegmentURL")) {
                r.segments.add(seg(base, s.attr("media"), s.attr("mediaRange")));
            }
            return;
        }
        // SegmentBase or nothing: the representation is one self-contained file.
        r.segments.add(new Seg(base, -1, -1));
    }

    private static void fromTemplate(Representation r, Node t, String base, double duration) {
        String media = orEmpty(t.attr("media"));
        String init = t.attr("initialization");
        long timescale = Math.max(1, parseLong(t.attr("timescale"), 1));
        long startNumber = parseLong(t.attr("startNumber"), 1);
        if (init != null) r.init = new Seg(StreamHttp.resolve(base, fill(init, r, 0, 0)), -1, -1);
        Node timeline = t.child("SegmentTimeline");
        if (timeline != null) {
            long time = 0;
            long number = startNumber;
            long end = duration > 0 ? (long) (duration * timescale) : Long.MAX_VALUE;
            List<Node> ss = timeline.all("S");
            for (int i = 0; i < ss.size(); i++) {
                Node s = ss.get(i);
                if (s.attr("t") != null) time = parseLong(s.attr("t"));
                long d = parseLong(s.attr("d"));
                long repeat = parseLong(s.attr("r"));
                if (d <= 0) continue;
                if (repeat < 0) {
                    // Until the next S (or the end of the period).
                    long until = i + 1 < ss.size() && ss.get(i + 1).attr("t") != null
                            ? parseLong(ss.get(i + 1).attr("t"))
                            : end;
                    repeat = until == Long.MAX_VALUE ? 0 : Math.max(0, (until - time + d - 1) / d - 1);
                }
                for (long k = 0; k <= repeat; k++) {
                    r.segments.add(new Seg(StreamHttp.resolve(base, fill(media, r, number, time)), -1, -1));
                    number++;
                    time += d;
                }
            }
            return;
        }
        long segDuration = parseLong(t.attr("duration"));
        if (segDuration <= 0 || duration <= 0) return;
        long count = (long) Math.ceil(duration * timescale / segDuration);
        for (long k = 0; k < count; k++) {
            long number = startNumber + k;
            r.segments.add(
                    new Seg(StreamHttp.resolve(base, fill(media, r, number, k * segDuration)), -1, -1));
        }
    }

    private static final Pattern IDENT = Pattern.compile("\\$(RepresentationID|Number|Time|Bandwidth)(%0(\\d+)d)?\\$");

    /** Expands $RepresentationID$, $Number%05d$, $Time$, $Bandwidth$ and $$. */
    static String fill(String template, Representation r, long number, long time) {
        Matcher mm = IDENT.matcher(template);
        StringBuilder sb = new StringBuilder();
        int last = 0;
        while (mm.find()) {
            String id = mm.group(1);
            String value;
            if ("RepresentationID".equals(id)) {
                value = r.id;
            } else {
                long v = "Number".equals(id) ? number : "Time".equals(id) ? time : r.bandwidth;
                value = mm.group(3) != null
                        ? String.format(Locale.US, "%0" + mm.group(3) + "d", v)
                        : String.valueOf(v);
            }
            sb.append(template, last, mm.start()).append(value);
            last = mm.end();
        }
        sb.append(template, last, template.length());
        return sb.toString().replace("$$", "$");
    }

    /** |over|'s attributes and children win over |under|'s. */
    private static Node merge(Node under, Node over) {
        if (under == null) return over;
        if (over == null) return under;
        Node n = new Node(over.name);
        n.attrs.putAll(under.attrs);
        n.attrs.putAll(over.attrs);
        n.children.addAll(over.children.isEmpty() ? under.children : over.children);
        return n;
    }

    private static Seg seg(String base, String url, String range) {
        String u = url == null || url.isEmpty() ? base : StreamHttp.resolve(base, url);
        if (range == null || !range.contains("-")) return new Seg(u, -1, -1);
        String[] se = range.split("-");
        return new Seg(u, parseLong(se[0]), se.length > 1 ? parseLong(se[1]) : -1);
    }

    private static String baseUrl(Node n, String parent) {
        Node b = n.child("BaseURL");
        if (b == null) return parent;
        String t = b.text.toString().trim();
        return t.isEmpty() ? parent : StreamHttp.resolve(parent, t);
    }

    private static String attr(Node rep, Node set, String key) {
        String v = rep.attr(key);
        if (v == null) v = set.attr(key);
        return v == null ? "" : v;
    }

    private static final Pattern ISO_DURATION =
            Pattern.compile("P(?:(\\d+(?:\\.\\d+)?)D)?(?:T(?:(\\d+(?:\\.\\d+)?)H)?(?:(\\d+(?:\\.\\d+)?)M)?(?:(\\d+(?:\\.\\d+)?)S)?)?");

    /** ISO 8601 "PT1H2M3.5S" → seconds; -1 if absent. */
    static double parseDuration(String s) {
        if (s == null) return -1;
        Matcher m = ISO_DURATION.matcher(s.trim());
        if (!m.matches()) return -1;
        double d = 0;
        if (m.group(1) != null) d += Double.parseDouble(m.group(1)) * 86400;
        if (m.group(2) != null) d += Double.parseDouble(m.group(2)) * 3600;
        if (m.group(3) != null) d += Double.parseDouble(m.group(3)) * 60;
        if (m.group(4) != null) d += Double.parseDouble(m.group(4));
        return d;
    }

    private static String orEmpty(String s) {
        return s == null ? "" : s;
    }

    private static long parseLong(String s) {
        return parseLong(s, 0);
    }

    private static long parseLong(String s, long fallback) {
        if (s == null) return fallback;
        try {
            return Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }
}
