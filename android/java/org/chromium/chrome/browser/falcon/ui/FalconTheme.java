/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ui;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;

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

    /** Call from an activity's applyThemeOverlays(), before any view is inflated. */
    public static void applyPitchBlack(Activity activity) {
        if (!isPitchBlack(activity)) return;
        activity.getTheme().applyStyle(R.style.ThemeOverlay_Falcon_PitchBlack, true);
    }

    /** {@code falcon_bg} or black. */
    public static int bg(Context context) {
        return isPitchBlack(context) ? Color.BLACK : context.getColor(R.color.falcon_bg);
    }

    /** {@code falcon_glass_solid} (the capsule fill) or black. */
    public static int glassSolid(Context context) {
        return isPitchBlack(context) ? Color.BLACK : context.getColor(R.color.falcon_glass_solid);
    }
}
