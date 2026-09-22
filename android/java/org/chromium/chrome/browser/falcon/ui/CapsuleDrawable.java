/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ui;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

import org.chromium.build.annotations.Nullable;

/**
 * The Arc capsule: a floating rounded pill that can morph into a flat, edge-to-edge bar. {@code
 * morph} runs 0 (pill: inset, fully rounded, hairline) to 1 (flat: no inset, square, no hairline);
 * the toolbar animates it when the omnibox takes and drops focus.
 */
public class CapsuleDrawable extends Drawable {
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mRect = new RectF();
    private final float mRadius;
    private final float mInsetSide;
    private final float mInsetTop;
    private final float mInsetBottom;
    private final int mStrokeAlpha;
    private float mMorph;

    public CapsuleDrawable(
            int fillColor,
            int strokeColor,
            float strokeWidth,
            float radius,
            float insetSide,
            float insetTop,
            float insetBottom) {
        mFill.setStyle(Paint.Style.FILL);
        mFill.setColor(fillColor);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeWidth(strokeWidth);
        mStroke.setColor(strokeColor);
        mStrokeAlpha = mStroke.getAlpha();
        mRadius = radius;
        mInsetSide = insetSide;
        mInsetTop = insetTop;
        mInsetBottom = insetBottom;
    }

    /** 0 = pill, 1 = flat bar. */
    public void setMorph(float morph) {
        morph = Math.max(0f, Math.min(1f, morph));
        if (morph == mMorph) return;
        mMorph = morph;
        invalidateSelf();
    }

    public float getMorph() {
        return mMorph;
    }

    @Override
    public void draw(Canvas canvas) {
        Rect b = getBounds();
        float k = 1f - mMorph;
        float half = mStroke.getStrokeWidth() / 2f;
        mRect.set(
                b.left + mInsetSide * k + half,
                b.top + mInsetTop * k + half,
                b.right - mInsetSide * k - half,
                b.bottom - mInsetBottom * k - half);
        // Never more than a stadium: the inner location field is a stadium too, so equal
        // padding on every side keeps the two corners concentric.
        float r = Math.min(mRadius, mRect.height() / 2f) * k;
        canvas.drawRoundRect(mRect, r, r, mFill);
        int alpha = Math.round(mStrokeAlpha * k);
        if (alpha > 0) {
            mStroke.setAlpha(alpha);
            canvas.drawRoundRect(mRect, r, r, mStroke);
        }
    }

    @Override
    public void setAlpha(int alpha) {
        mFill.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(@Nullable ColorFilter colorFilter) {
        mFill.setColorFilter(colorFilter);
        mStroke.setColorFilter(colorFilter);
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
