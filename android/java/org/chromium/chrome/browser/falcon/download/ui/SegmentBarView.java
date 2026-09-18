/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.DownloadItem.Segment;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;

import java.util.List;

/**
 * The 16-bar "equaliser" under a download row: the file split into 16 equal buckets, each bar
 * filled by how much of its bucket has arrived. Finished files show as quiet grey bars.
 */
public class SegmentBarView extends View {
    private static final int BARS = 16;

    private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mRect = new RectF();
    private final float[] mFill = new float[BARS];
    private final int mIdle;
    private final int mDone;
    private final int mDoneDim;
    private boolean mFinished;
    private boolean mIndeterminate;

    public SegmentBarView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mIdle = context.getColor(R.color.falcon_segment_idle);
        mDone = context.getColor(R.color.falcon_segment_done);
        mDoneDim = context.getColor(R.color.falcon_segment_done_dim);
    }

    public void setItem(DownloadItem item) {
        mFinished = item.state == State.DONE;
        computeFill(item, mFill);
        mIndeterminate = item.totalBytes <= 0 && !mFinished;
        invalidate();
    }

    /** Fraction [0,1] of each of the 16 buckets that has been downloaded. */
    static void computeFill(DownloadItem item, float[] out) {
        long total = item.totalBytes;
        List<Segment> segments = item.segments();
        if (item.state == State.DONE) {
            java.util.Arrays.fill(out, 1f);
            return;
        }
        if (total <= 0) {
            java.util.Arrays.fill(out, 0f);
            return;
        }
        double bucket = total / (double) BARS;
        for (int i = 0; i < BARS; i++) {
            double bStart = i * bucket;
            double bEnd = (i + 1) * bucket;
            double covered = 0;
            for (Segment s : segments) {
                double dStart = s.start;
                double dEnd = s.start + s.done; // exclusive
                double lo = Math.max(bStart, dStart);
                double hi = Math.min(bEnd, dEnd);
                if (hi > lo) covered += hi - lo;
            }
            out[i] = (float) Math.min(1.0, covered / bucket);
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth();
        float h = getHeight();
        float gap = 3 * getResources().getDisplayMetrics().density;
        float bw = (w - gap * (BARS - 1)) / BARS;
        float r = h / 2;
        for (int i = 0; i < BARS; i++) {
            float x = i * (bw + gap);
            mRect.set(x, 0, x + bw, h);
            mPaint.setColor(mIdle);
            canvas.drawRoundRect(mRect, r, r, mPaint);
            float f = mIndeterminate ? 0f : mFill[i];
            if (f > 0f) {
                mPaint.setColor(mFinished ? mDoneDim : mDone);
                mPaint.setAlpha(mFinished ? 0x66 : (int) (0x66 + 0x99 * f));
                mRect.set(x, 0, x + bw * (mFinished ? 1f : Math.max(0.15f, f)), h);
                canvas.drawRoundRect(mRect, r, r, mPaint);
                mPaint.setAlpha(0xFF);
            }
        }
    }
}
