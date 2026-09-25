/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.media;

import android.annotation.SuppressLint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import org.chromium.base.Callback;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.fullscreen.FullscreenOptions;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.content_public.browser.WebContents;

/**
 * 1DM's floating download button: appears over a page once it has played something worth
 * grabbing, shows how many, and opens the grabber. It can be dragged anywhere along either edge
 * and stays where it was put. Hidden in fullscreen video and on pages with nothing captured.
 */
public final class MediaGrabberController implements MediaCaptureStore.Observer {
    private static final int SIZE_DP = 52;
    private static final int EDGE_DP = 12;
    private static final long SHOW_MS = 180;

    private final ChromeActivity mActivity;
    private final FrameLayout mButton;
    private final TextView mBadge;
    private final float mDp;
    private final Callback<Tab> mTabObserver = this::onTabChanged;
    private final FullscreenManager.Observer mFullscreenObserver =
            new FullscreenManager.Observer() {
                @Override
                public void onEnterFullscreen(Tab tab, FullscreenOptions options) {
                    mFullscreen = true;
                    update();
                }

                @Override
                public void onExitFullscreen(Tab tab) {
                    mFullscreen = false;
                    update();
                }
            };
    private Tab mTab;
    private boolean mFullscreen;
    private boolean mShown;
    private boolean mDestroyed;

    public MediaGrabberController(ChromeActivity activity) {
        mActivity = activity;
        mDp = activity.getResources().getDisplayMetrics().density;

        mButton = new FrameLayout(activity);
        GradientDrawable circle = new GradientDrawable();
        circle.setShape(GradientDrawable.OVAL);
        circle.setColor(activity.getColor(R.color.falcon_accent));
        mButton.setBackground(circle);
        mButton.setElevation(6 * mDp);
        mButton.setContentDescription("Download media on this page");
        ImageView icon = new ImageView(activity);
        icon.setImageResource(R.drawable.falcon_ic_download);
        icon.setColorFilter(activity.getColor(R.color.falcon_accent_ink));
        FrameLayout.LayoutParams iconLp = new FrameLayout.LayoutParams(dp(24), dp(24), Gravity.CENTER);
        mButton.addView(icon, iconLp);

        mBadge = new TextView(activity);
        mBadge.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f);
        mBadge.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        mBadge.setTextColor(activity.getColor(R.color.falcon_button_primary_ink));
        mBadge.setGravity(Gravity.CENTER);
        mBadge.setMinWidth(dp(18));
        mBadge.setPadding(dp(5), 0, dp(5), 0);
        GradientDrawable pill = new GradientDrawable();
        pill.setCornerRadius(dp(9));
        pill.setColor(activity.getColor(R.color.falcon_button_primary));
        mBadge.setBackground(pill);
        mButton.addView(mBadge, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(18), Gravity.TOP | Gravity.END));

        mButton.setVisibility(View.GONE);
        mButton.setOnClickListener(v -> openGrabber());
        attachDrag();

        ViewGroup content = activity.findViewById(android.R.id.content);
        content.addView(mButton, new FrameLayout.LayoutParams(dp(SIZE_DP), dp(SIZE_DP)));
        content.addOnLayoutChangeListener(
                (v, l, t, r, b, ol, ot, or, ob) -> {
                    if (r - l != or - ol || b - t != ob - ot) place();
                });

        MediaCaptureStore.getInstance().addObserver(this);
        activity.getActivityTabProvider().asObservable().addSyncObserverAndCall(mTabObserver);
        FullscreenManager fullscreen = activity.getFullscreenManager();
        if (fullscreen != null) fullscreen.addObserver(mFullscreenObserver);
    }

    public void destroy() {
        if (mDestroyed) return;
        mDestroyed = true;
        MediaCaptureStore.getInstance().removeObserver(this);
        mActivity.getActivityTabProvider().asObservable().removeObserver(mTabObserver);
        FullscreenManager fullscreen = mActivity.getFullscreenManager();
        if (fullscreen != null) fullscreen.removeObserver(mFullscreenObserver);
        ViewGroup parent = (ViewGroup) mButton.getParent();
        if (parent != null) parent.removeView(mButton);
    }

    private void onTabChanged(Tab tab) {
        mTab = tab;
        update();
    }

    @Override
    public void onCapturesChanged(WebContents webContents) {
        if (webContents != null && webContents == currentWebContents()) update();
    }

    private WebContents currentWebContents() {
        return mTab == null || mTab.isDestroyed() ? null : mTab.getWebContents();
    }

    private void update() {
        if (mDestroyed) return;
        WebContents wc = currentWebContents();
        int count =
                wc == null || !FalconPrefs.isMediaCaptureEnabled()
                        ? 0
                        : MediaCaptureStore.getInstance().count(wc);
        boolean show = count > 0 && !mFullscreen && !mTab.isIncognito();
        mBadge.setText(count > 99 ? "99+" : String.valueOf(count));
        if (show == mShown) return;
        mShown = show;
        mButton.animate().cancel();
        if (show) {
            place();
            mButton.bringToFront();
            mButton.setVisibility(View.VISIBLE);
            mButton.setScaleX(0.6f);
            mButton.setScaleY(0.6f);
            mButton.setAlpha(0f);
            mButton.animate()
                    .scaleX(1f)
                    .scaleY(1f)
                    .alpha(1f)
                    .setDuration(animationsOff() ? 0 : SHOW_MS)
                    .setInterpolator(new DecelerateInterpolator())
                    .start();
        } else {
            mButton.animate()
                    .scaleX(0.6f)
                    .scaleY(0.6f)
                    .alpha(0f)
                    .setDuration(animationsOff() ? 0 : SHOW_MS)
                    .withEndAction(() -> mButton.setVisibility(View.GONE))
                    .start();
        }
    }

    private void openGrabber() {
        WebContents wc = currentWebContents();
        if (wc == null) return;
        GrabberSheet.show(mActivity, wc, mTab.getUrl().getSpec());
    }

    // ── position & drag ──

    /** Puts the button at the saved spot: an edge, and a fraction of the height. */
    private void place() {
        View parent = (View) mButton.getParent();
        if (parent == null || parent.getWidth() == 0) return;
        float size = dp(SIZE_DP);
        float edge = dp(EDGE_DP);
        float x = FalconPrefs.isGrabberButtonLeft() ? edge : parent.getWidth() - size - edge;
        float y = FalconPrefs.getGrabberButtonY() * (parent.getHeight() - size);
        mButton.setX(x);
        mButton.setY(clampY(parent, y));
    }

    private float clampY(View parent, float y) {
        float top = dp(72);
        float bottom = parent.getHeight() - dp(SIZE_DP) - dp(96); // clear of the bottom bar
        return Math.max(top, Math.min(bottom, y));
    }

    @SuppressLint("ClickableViewAccessibility") // a tap still goes through performClick()
    private void attachDrag() {
        final int slop = ViewConfiguration.get(mActivity).getScaledTouchSlop();
        mButton.setOnTouchListener(
                new View.OnTouchListener() {
                    private float mDownRawX;
                    private float mDownRawY;
                    private float mStartX;
                    private float mStartY;
                    private boolean mDragging;

                    @Override
                    public boolean onTouch(View v, MotionEvent e) {
                        View parent = (View) v.getParent();
                        switch (e.getActionMasked()) {
                            case MotionEvent.ACTION_DOWN:
                                mDownRawX = e.getRawX();
                                mDownRawY = e.getRawY();
                                mStartX = v.getX();
                                mStartY = v.getY();
                                mDragging = false;
                                v.setPressed(true);
                                return true;
                            case MotionEvent.ACTION_MOVE: {
                                float dx = e.getRawX() - mDownRawX;
                                float dy = e.getRawY() - mDownRawY;
                                if (!mDragging && Math.hypot(dx, dy) > slop) {
                                    mDragging = true;
                                    v.setPressed(false);
                                }
                                if (mDragging && parent != null) {
                                    v.setX(Math.max(0, Math.min(parent.getWidth() - v.getWidth(), mStartX + dx)));
                                    v.setY(clampY(parent, mStartY + dy));
                                }
                                return true;
                            }
                            case MotionEvent.ACTION_UP:
                                v.setPressed(false);
                                if (!mDragging) {
                                    v.performClick();
                                } else if (parent != null) {
                                    boolean left = v.getX() + v.getWidth() / 2f < parent.getWidth() / 2f;
                                    float range = parent.getHeight() - v.getHeight();
                                    FalconPrefs.setGrabberButtonPosition(
                                            range > 0 ? v.getY() / range : 0.62f, left);
                                    float edge = dp(EDGE_DP);
                                    v.animate()
                                            .x(left ? edge : parent.getWidth() - v.getWidth() - edge)
                                            .setDuration(animationsOff() ? 0 : SHOW_MS)
                                            .setInterpolator(new DecelerateInterpolator())
                                            .start();
                                }
                                return true;
                            case MotionEvent.ACTION_CANCEL:
                                v.setPressed(false);
                                if (mDragging) place();
                                return true;
                            default:
                                return false;
                        }
                    }
                });
    }

    private boolean animationsOff() {
        return android.provider.Settings.Global.getFloat(
                        mActivity.getContentResolver(),
                        android.provider.Settings.Global.ANIMATOR_DURATION_SCALE,
                        1f)
                == 0f;
    }

    private int dp(float v) {
        return Math.round(v * mDp);
    }
}
