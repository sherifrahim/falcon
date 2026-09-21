/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ui;

import android.app.Activity;
import android.content.Context;
import android.content.res.Resources;
import android.util.TypedValue;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.ui.util.ColorUtils;

/**
 * Pitch black (Falcon settings › Look): in the dark theme the ink surfaces become true black for
 * OLED panels. Material surfaces go through a theme overlay; Falcon's own token-painted surfaces
 * ask here for the substitute colour.
 */
public final class FalconTheme {
    private FalconTheme() {}

    public static boolean isPitchBlack(Context context) {
        return FalconPrefs.isPitchBlack() && ColorUtils.inNightMode(context);
    }

    /**
     * Call from an activity's applyThemeOverlays(), before any view is inflated: gives a
     * Chromium-themed activity the Falcon surface tokens, then pitch black on top if it is on.
     */
    public static void applyTokens(Activity activity) {
        activity.getTheme().applyStyle(R.style.ThemeOverlay_Falcon_Tokens, true);
        applyPitchBlack(activity);
    }

    /** Pitch black on a theme that already carries the tokens (the Falcon themes). */
    public static void applyPitchBlack(Activity activity) {
        applyPitchBlack(activity.getTheme(), activity);
    }

    public static void applyPitchBlack(Resources.Theme theme, Context context) {
        if (!isPitchBlack(context)) return;
        theme.applyStyle(R.style.ThemeOverlay_Falcon_PitchBlack, true);
    }

    /** A Falcon surface token from the context's theme (falcon_attrs.xml). */
    public static int color(Context context, int attr) {
        TypedValue v = new TypedValue();
        if (context.getTheme().resolveAttribute(attr, v, true)) return v.data;
        // A context without the tokens (should not happen): the plain colour set.
        return attr == R.attr.falconGlassSolid
                ? context.getColor(R.color.falcon_glass_solid)
                : context.getColor(R.color.falcon_bg);
    }

    public static int bg(Context context) {
        return color(context, R.attr.falconBg);
    }

    public static int glassSolid(Context context) {
        return color(context, R.attr.falconGlassSolid);
    }
}
