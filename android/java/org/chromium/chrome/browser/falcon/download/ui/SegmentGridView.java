/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.DownloadItem.Segment;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;
import org.chromium.chrome.browser.falcon.ui.FalconTheme;

import java.util.List;
import java.util.Locale;

/**
 * The detail sheet's segment grid: one tile per connection with "#n" and its state — done, its
 * current MB/s while active, or wait (mock v6 screen 5). Speeds come from the deltas between
 * consecutive {@link #setItem} calls.
 */
public class SegmentGridView extends View {
    private static final int COLUMNS = 8;

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLabel = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mValue = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mRect = new RectF();
    private final float mDensity;
    private final int mAccent;
    private final int mSelection;
    private final int mSelectionLine;
    private final int mHair;
    private final int mSurface;
    private final int mInk;
    private final int mInk3;

    private List<Segment> mSegments = java.util.Collections.emptyList();
    private boolean mActive;
    private long[] mLastDone = new long[0];
    private long mLastTime;
    private double[] mSpeeds = new double[0];

    public SegmentGridView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mDensity = getResources().getDisplayMetrics().density;
        mAccent = context.getColor(R.color.falcon_accent);
        mSelection = context.getColor(R.color.falcon_selection);
        mSelectionLine = context.getColor(R.color.falcon_selection_line);
        mHair = context.getColor(R.color.falcon_hair);
        mSurface = FalconTheme.color(context, R.attr.falconSurface);
        mInk = context.getColor(R.color.falcon_ink);
        mInk3 = context.getColor(R.color.falcon_ink_3);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeWidth(1.2f * mDensity);
        mLabel.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        mLabel.setTextSize(10 * mDensity);
        mValue.setTypeface(Typeface.MONOSPACE);
        mValue.setTextSize(11 * mDensity);
    }

    public void setItem(DownloadItem item) {
        List<Segment> segments = item.segments();
        long now = System.currentTimeMillis();
        if (segments.size() != mLastDone.length) {
            mLastDone = new long[segments.size()];
            mSpeeds = new double[segments.size()];
            for (int i = 0; i < segments.size(); i++) mLastDone[i] = segments.get(i).done;
            mLastTime = now;
        } else if (now > mLastTime) {
            double dt = (now - mLastTime) / 1000.0;
            for (int i = 0; i < segments.size(); i++) {
                long done = segments.get(i).done;
                double bps = (done - mLastDone[i]) / dt;
                // Smooth a little so tiles don't flicker between ticks.
                mSpeeds[i] = mSpeeds[i] * 0.5 + Math.max(0, bps) * 0.5;
                mLastDone[i] = done;
            }
            mLastTime = now;
        }
        mSegments = segments;
        mActive = item.state == State.ACTIVE;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int n = mSegments.size();
        if (n == 0) return;
        int cols = Math.min(COLUMNS, n);
        int rows = (n + cols - 1) / cols;
        float gap = 6 * mDensity;
        float w = getWidth();
        float h = getHeight();
        float tw = (w - gap * (cols - 1)) / cols;
        float th = Math.min(46 * mDensity, (h - gap * (rows - 1)) / rows);
        float r = 8 * mDensity;
        for (int i = 0; i < n; i++) {
            Segment s = mSegments.get(i);
            int col = i % cols;
            int row = i / cols;
            float x = col * (tw + gap);
            float y = row * (th + gap);
            mRect.set(x, y, x + tw, y + th);
            boolean done = s.isComplete();
            boolean running = mActive && !done && s.done > 0 && mSpeeds.length > i && mSpeeds[i] > 0;
            boolean waiting = !done && !running;
            mFill.setColor(done ? mSelection : mSurface);
            canvas.drawRoundRect(mRect, r, r, mFill);
            mStroke.setColor(done ? mSelectionLine : running ? mAccent : mHair);
            canvas.drawRoundRect(mRect, r, r, mStroke);
            mLabel.setColor(waiting ? mInk3 : mInk);
            canvas.drawText("#" + (i + 1), x + 6 * mDensity, y + 14 * mDensity, mLabel);
            String value;
            if (done) {
                value = "done";
            } else if (running) {
                value = String.format(Locale.US, "%.1f", mSpeeds[i] / (1024.0 * 1024.0));
            } else {
                value = "wait";
            }
            mValue.setColor(waiting ? mInk3 : done ? mInk : mAccent);
            mValue.setTypeface(
                    running ? Typeface.create(Typeface.MONOSPACE, Typeface.BOLD) : Typeface.MONOSPACE);
            canvas.drawText(value, x + 6 * mDensity, y + th - 8 * mDensity, mValue);
        }
    }
}
