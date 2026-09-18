/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

import org.chromium.chrome.R;

import java.util.Locale;

/**
 * The telemetry card: the live speed as the headline, the queue counts beside it, and the last
 * 60 seconds as a spark line with its ticks under the graph (mock v6 screen 4).
 */
public class TelemetryView extends View {
    private static final int SAMPLES = 60;

    private final Paint mBig = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mUnit = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mKv = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mTick = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path mPath = new Path();
    private final Path mArea = new Path();

    private long[] mHistory = new long[SAMPLES];
    private long mSpeedBps;
    private int mActive;
    private int mQueued;
    private int mDone;
    private int mConnections;
    private final float mDensity;

    public TelemetryView(Context context, AttributeSet attrs) {
        super(context, attrs);
        mDensity = getResources().getDisplayMetrics().density;
        int ink = context.getColor(R.color.falcon_ink);
        int accent = context.getColor(R.color.falcon_accent);

        mBig.setColor(ink);
        mBig.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        mBig.setTextSize(40 * mDensity);
        mBig.setLetterSpacing(-0.05f);

        mUnit.setColor(accent);
        mUnit.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        mUnit.setTextSize(12 * mDensity);

        mKv.setColor(context.getColor(R.color.falcon_ink_2));
        mKv.setTypeface(Typeface.MONOSPACE);
        mKv.setTextSize(10.5f * mDensity);
        mKv.setTextAlign(Paint.Align.RIGHT);

        mTick.setColor(context.getColor(R.color.falcon_ink_3));
        mTick.setTypeface(Typeface.MONOSPACE);
        mTick.setTextSize(9.5f * mDensity);

        mLine.setColor(accent);
        mLine.setStyle(Paint.Style.STROKE);
        mLine.setStrokeWidth(2.2f * mDensity);
        mLine.setStrokeJoin(Paint.Join.ROUND);
        mLine.setStrokeCap(Paint.Cap.ROUND);

        mFill.setStyle(Paint.Style.FILL);
    }

    public void setData(
            long[] history, long speedBps, int active, int queued, int done, int connections) {
        if (history != null && history.length == SAMPLES) mHistory = history;
        mSpeedBps = speedBps;
        mActive = active;
        mQueued = queued;
        mDone = done;
        mConnections = connections;
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        int accent = getContext().getColor(R.color.falcon_accent);
        mFill.setShader(
                new LinearGradient(
                        0, h * 0.35f, 0, h, (accent & 0x00FFFFFF) | 0x73000000, accent & 0x00FFFFFF,
                        Shader.TileMode.CLAMP));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth();
        float h = getHeight();
        float pad = 16 * mDensity;
        float graphBottom = h - 20 * mDensity;
        float graphTop = h * 0.36f;

        // ── spark (behind the text) ──
        long max = 1;
        for (long v : mHistory) max = Math.max(max, v);
        mPath.reset();
        mArea.reset();
        float step = w / (SAMPLES - 1);
        for (int i = 0; i < SAMPLES; i++) {
            float x = i * step;
            float y = graphBottom - (graphBottom - graphTop) * (mHistory[i] / (float) max);
            if (i == 0) {
                mPath.moveTo(x, y);
                mArea.moveTo(x, graphBottom);
                mArea.lineTo(x, y);
            } else {
                mPath.lineTo(x, y);
                mArea.lineTo(x, y);
            }
        }
        mArea.lineTo(w, graphBottom);
        mArea.close();
        canvas.drawPath(mArea, mFill);
        canvas.drawPath(mPath, mLine);

        // ── ticks under the graph ──
        float tickY = h - 6 * mDensity;
        canvas.drawText("−60 s", pad, tickY, mTick);
        String mid = "−30 s";
        canvas.drawText(mid, w / 2 - mTick.measureText(mid) / 2, tickY, mTick);
        String now = "now";
        canvas.drawText(now, w - pad - mTick.measureText(now), tickY, mTick);

        // ── headline ──
        float baseline = pad + 40 * mDensity;
        String speed = formatSpeedNumber(mSpeedBps);
        canvas.drawText(speed, pad, baseline, mBig);
        canvas.drawText(
                formatSpeedUnit(mSpeedBps), pad + mBig.measureText(speed) + 8 * mDensity, baseline, mUnit);

        // ── counts, right aligned ──
        float kvY = pad + 12 * mDensity;
        float lh = 14 * mDensity;
        canvas.drawText(
                String.format(Locale.US, "%d active · %d queued · %d done", mActive, mQueued, mDone),
                w - pad, kvY + lh, mKv);
        canvas.drawText(
                String.format(Locale.US, "up to %d conn", mConnections), w - pad, kvY + 2 * lh, mKv);
    }

    static String formatSpeedNumber(long bps) {
        double kb = bps / 1024.0;
        if (kb < 1024) return String.format(Locale.US, "%.0f", kb);
        return String.format(Locale.US, "%.1f", kb / 1024.0);
    }

    static String formatSpeedUnit(long bps) {
        return bps / 1024.0 < 1024 ? "KB/s" : "MB/s";
    }
}
