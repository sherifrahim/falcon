/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon;

import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.night_mode.ThemeType;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;
import org.chromium.chrome.browser.settings.BackgroundImagesPreferences;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.toolbar.bottom.BottomToolbarConfiguration;

/**
 * Falcon's out-of-the-box choices on Android, written once per install so the user's later
 * changes (settings, long-press toolbar moves) always win: dark first, address bar at the thumb.
 */
public final class FalconDefaults {
    private static final String APPLIED_KEY = "falcon_defaults_applied";

    private FalconDefaults() {}

    /** Safe to call from the application's onCreate (browser process) and again later. */
    public static void applyOnce() {
        SharedPreferencesManager prefs = ChromeSharedPreferences.getInstance();
        if (prefs.readBoolean(APPLIED_KEY, false)) return;
        prefs.writeBoolean(APPLIED_KEY, true);
        // Dark-first (docs/design/android/mock-v6.html is a dark design; light comes later).
        if (prefs.readInt(ChromePreferenceKeys.UI_THEME_SETTING, -1) == -1) {
            prefs.writeInt(ChromePreferenceKeys.UI_THEME_SETTING, ThemeType.DARK);
        }
        BottomToolbarConfiguration.applyFalconBottomBarMode(FalconPrefs.getBottomBarMode());
        // The space home (mock v6 screen 1) has no stats widget.
        prefs.writeBoolean(BackgroundImagesPreferences.PREF_SHOW_BRAVE_STATS, false);
        // The space colour field is the backdrop; no wallpaper photos.
        prefs.writeBoolean(BackgroundImagesPreferences.PREF_SHOW_BACKGROUND_IMAGES, false);
        OnboardingPrefManager.getInstance().setBraveStatsEnabled(false);
    }
}
