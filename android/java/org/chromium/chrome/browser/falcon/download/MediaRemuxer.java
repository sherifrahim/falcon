/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMuxer;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

/**
 * Rewrites downloaded stream tracks (MPEG-TS, fragmented MP4, WebM) into one plain MP4 (or WebM
 * for VP8/VP9/Vorbis) without re-encoding, using the platform's extractor and muxer. The first
 * video track and the first audio track found across the inputs are kept.
 */
final class MediaRemuxer {
    private static final int BUFFER_BYTES = 8 * 1024 * 1024;

    private MediaRemuxer() {}

    /** Output container for these inputs: WebM when a codec only WebM can carry is present. */
    static boolean needsWebm(List<File> inputs) {
        for (File f : inputs) {
            MediaExtractor ex = new MediaExtractor();
            try {
                ex.setDataSource(f.getPath());
                for (int t = 0; t < ex.getTrackCount(); t++) {
                    String mime = ex.getTrackFormat(t).getString(MediaFormat.KEY_MIME);
                    if (MediaFormat.MIMETYPE_VIDEO_VP8.equals(mime)
                            || MediaFormat.MIMETYPE_VIDEO_VP9.equals(mime)
                            || MediaFormat.MIMETYPE_AUDIO_VORBIS.equals(mime)) {
                        return true;
                    }
                }
            } catch (IOException | RuntimeException e) {
                // Unreadable here means unreadable in remux() too; let it report.
            } finally {
                ex.release();
            }
        }
        return false;
    }

    /** Throws if the platform cannot read or mux these tracks; |out| is then incomplete. */
    static void remux(List<File> inputs, File out, boolean webm) throws IOException {
        List<MediaExtractor> extractors = new ArrayList<>();
        // [extractor][source track] → muxer track, -1 when not copied.
        List<int[]> map = new ArrayList<>();
        MediaMuxer muxer = null;
        boolean started = false;
        try {
            muxer =
                    new MediaMuxer(
                            out.getPath(),
                            webm
                                    ? MediaMuxer.OutputFormat.MUXER_OUTPUT_WEBM
                                    : MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            boolean haveVideo = false;
            boolean haveAudio = false;
            for (File f : inputs) {
                MediaExtractor ex = new MediaExtractor();
                ex.setDataSource(f.getPath());
                int[] tracks = new int[ex.getTrackCount()];
                for (int t = 0; t < tracks.length; t++) {
                    tracks[t] = -1;
                    MediaFormat format = ex.getTrackFormat(t);
                    String mime = format.getString(MediaFormat.KEY_MIME);
                    if (mime == null) continue;
                    boolean video = mime.startsWith("video/");
                    boolean audio = mime.startsWith("audio/");
                    if ((video && !haveVideo) || (audio && !haveAudio)) {
                        ex.selectTrack(t);
                        tracks[t] = muxer.addTrack(format);
                        if (video) haveVideo = true;
                        if (audio) haveAudio = true;
                    }
                }
                extractors.add(ex);
                map.add(tracks);
            }
            if (!haveVideo && !haveAudio) throw new IOException("no audio or video track");
            muxer.start();
            started = true;

            long base = Long.MAX_VALUE;
            for (MediaExtractor ex : extractors) {
                long t = ex.getSampleTime();
                if (t >= 0) base = Math.min(base, t);
            }
            if (base == Long.MAX_VALUE) base = 0;

            ByteBuffer buffer = ByteBuffer.allocate(BUFFER_BYTES);
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            while (true) {
                // Interleave by time: always write the earliest pending sample.
                int pick = -1;
                long pickTime = Long.MAX_VALUE;
                for (int i = 0; i < extractors.size(); i++) {
                    long t = extractors.get(i).getSampleTime();
                    if (t >= 0 && t < pickTime) {
                        pick = i;
                        pickTime = t;
                    }
                }
                if (pick < 0) break;
                MediaExtractor ex = extractors.get(pick);
                int dst = map.get(pick)[ex.getSampleTrackIndex()];
                int size = ex.readSampleData(buffer, 0);
                if (size >= 0 && dst >= 0) {
                    int flags =
                            (ex.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0
                                    ? MediaCodec.BUFFER_FLAG_KEY_FRAME
                                    : 0;
                    info.set(0, size, Math.max(0, pickTime - base), flags);
                    muxer.writeSampleData(dst, buffer, info);
                }
                ex.advance();
            }
        } catch (RuntimeException e) {
            throw new IOException(e.getMessage() == null ? "remux failed" : e.getMessage());
        } finally {
            for (MediaExtractor ex : extractors) ex.release();
            if (muxer != null) {
                try {
                    if (started) muxer.stop();
                } catch (RuntimeException e) {
                    // stop() throws when nothing usable was written; caught below.
                    started = false;
                }
                muxer.release();
            }
        }
        if (!started) throw new IOException("remux failed");
    }
}
