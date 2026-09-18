/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.toolbar.bottom;

import android.app.Activity;
import android.graphics.Point;
import android.view.Display;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.BravePreferenceKeys;
import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.toolbar.ToolbarPositionController.ToolbarPositionAndSource;
import org.chromium.chrome.browser.toolbar.settings.AddressBarPreference;
import org.chromium.chrome.browser.toolbar.settings.AddressBarPreference;
import org.chromium.ui.base.DeviceFormFactor;

public class BottomToolbarConfiguration {
    private static final int SMALL_SCREEN_WIDTH = 360;
    private static final int SMALL_SCREEN_HEIGHT = 640;

    /**
     * Whether upstream's Android bottom bar is enabled. It replaces Brave's bottom navigation
     * controls, so all of them must be off while it is on, with any of the flag's variations.
     */
    public static boolean isAndroidBottomBarEnabled() {
        return ChromeFeatureList.sAndroidBottomBar.isEnabled();
    }

    public static boolean isBraveBottomControlsEnabled() {
        // Upstream's bottom bar owns the bottom controls when it is enabled.
        if (isAndroidBottomBarEnabled()) {
            return false;
        }
        // Falcon: only the Classic layout keeps Brave's button row. Arc uses the
        // Falcon capsule (until it ships, it behaves like Reach); Reach anchors
        // Chromium's address bar at the bottom instead.
        if (FalconPrefs.getBottomBarMode() != FalconPrefs.BottomBarMode.CLASSIC) {
            return false;
        }
        // We do not use the bottom controls on tablets.
        if (DeviceFormFactor.isNonMultiDisplayContextOnTablet(
                ContextUtils.getApplicationContext())) {
            return false;
        }
        // We do not use the bottom controls with address bar on bottom.
        if (isToolbarBottomAnchored()) {
            return false;
        }
        if (ChromeSharedPreferences.getInstance()
                .readBoolean(BravePreferenceKeys.BRAVE_BOTTOM_TOOLBAR_SET_KEY, false)) {
            return ChromeSharedPreferences.getInstance()
                    .readBoolean(BravePreferenceKeys.BRAVE_BOTTOM_TOOLBAR_ENABLED_KEY, true);
        } else {
            ChromeSharedPreferences.getInstance()
                    .writeBoolean(BravePreferenceKeys.BRAVE_BOTTOM_TOOLBAR_SET_KEY, true);
            boolean enable = true;
            if (isSmallScreen()) {
                enable = false;
            }
            ChromeSharedPreferences.getInstance()
                    .writeBoolean(BravePreferenceKeys.BRAVE_BOTTOM_TOOLBAR_ENABLED_KEY, enable);

            return enable;
        }
    }

    private static boolean isSmallScreen() {
        Activity currentActivity = null;
        for (Activity ref : ApplicationStatus.getRunningActivities()) {
            currentActivity = ref;
            if (!(ref instanceof ChromeActivity)) continue;

            break;
        }
        Display screensize = currentActivity.getWindowManager().getDefaultDisplay();
        Point size = new Point();
        screensize.getSize(size);
        int width = size.x;
        int height = size.y;

        return (width <= SMALL_SCREEN_WIDTH) && (height <= SMALL_SCREEN_HEIGHT);
    }

    public static boolean isToolbarTopAnchored() {
        return AddressBarPreference.isToolbarConfiguredToShowOnTop();
    }

    /**
     * Falcon: writes the toolbar position that goes with a bottom bar mode. Called when the
     * mode changes in settings and once at startup so a fresh install starts in Reach.
     */
    public static void applyFalconBottomBarMode(@FalconPrefs.BottomBarMode int mode) {
        boolean top = mode == FalconPrefs.BottomBarMode.CLASSIC;
        if (AddressBarPreference.isToolbarConfiguredToShowOnTop() != top) {
            AddressBarPreference.setToolbarPositionAndSource(
                    top
                            ? ToolbarPositionAndSource.TOP_SETTINGS
                            : ToolbarPositionAndSource.BOTTOM_SETTINGS);
        }
    }

    public static boolean isToolbarBottomAnchored() {
        return !isToolbarTopAnchored();
    }
}
