/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon;

import androidx.annotation.IntDef;

import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.tab.TabArchiveSettings;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * Falcon's Android-side settings. These are UI-layer choices (shell layout, downloader
 * behaviour) kept in shared preferences so they are available before native is up.
 */
public final class FalconPrefs {
    private FalconPrefs() {}

    /** How the bottom of the screen is laid out. See docs/PLAN.md §4.4 (mock v6, "Control"). */
    @IntDef({BottomBarMode.ARC, BottomBarMode.REACH, BottomBarMode.CLASSIC})
    @Retention(RetentionPolicy.SOURCE)
    public @interface BottomBarMode {
        /** Falcon capsule: search field, tab count and + at the thumb; toolbar hidden. */
        int ARC = 0;
        /** One UI reach: Chromium's address bar anchored at the bottom, no button row. */
        int REACH = 1;
        /** Brave's default: address bar on top, Brave's bottom button row. */
        int CLASSIC = 2;
    }

    /**
     * Falcon ships without Brave's services on Android (Rewards, Wallet, VPN, Leo, News). Their
     * policy helpers report "disabled" from this constant so every Brave surface hides them even
     * before a profile exists (the toolbar asks with a null profile at startup); the native side is
     * disabled by LoadFalconPolicies and, in Release, compiled out.
     */
    public static final boolean BRAVE_SERVICES = false;

    public static final String BOTTOM_BAR_MODE = "falcon_bottom_bar_mode";
    public static final String DOWNLOADER_ENABLED = "falcon_downloader_enabled";
    public static final String DOWNLOADER_CONNECTIONS = "falcon_downloader_connections";
    public static final String DOWNLOADER_VERIFY = "falcon_downloader_verify";
    public static final String DOWNLOADER_WIFI_ONLY = "falcon_downloader_wifi_only";
    public static final String PITCH_BLACK = "falcon_pitch_black";

    public static final @BottomBarMode int DEFAULT_BOTTOM_BAR_MODE = BottomBarMode.ARC;
    public static final int DEFAULT_DOWNLOADER_CONNECTIONS = 8;

    private static SharedPreferencesManager prefs() {
        return ChromeSharedPreferences.getInstance();
    }

    /**
     * readInt that survives a String stored under the key (a persistent ListPreference did that
     * in the first Falcon APK and every later launch crashed); the value is rewritten as an int.
     */
    private static int readIntSafe(String key, int defaultValue) {
        try {
            return prefs().readInt(key, defaultValue);
        } catch (ClassCastException e) {
            int value = defaultValue;
            try {
                value = Integer.parseInt(prefs().readString(key, ""));
            } catch (NumberFormatException | ClassCastException ignored) {
                // fall through to the default
            }
            prefs().removeKey(key);
            prefs().writeInt(key, value);
            return value;
        }
    }

    public static @BottomBarMode int getBottomBarMode() {
        int mode = readIntSafe(BOTTOM_BAR_MODE, DEFAULT_BOTTOM_BAR_MODE);
        return mode >= BottomBarMode.ARC && mode <= BottomBarMode.CLASSIC
                ? mode
                : DEFAULT_BOTTOM_BAR_MODE;
    }

    public static void setBottomBarMode(@BottomBarMode int mode) {
        prefs().writeInt(BOTTOM_BAR_MODE, mode);
    }

    /** Whether Falcon's downloader takes over page downloads (falls back to Chromium's if off). */
    public static boolean isDownloaderEnabled() {
        return prefs().readBoolean(DOWNLOADER_ENABLED, true);
    }

    public static void setDownloaderEnabled(boolean enabled) {
        prefs().writeBoolean(DOWNLOADER_ENABLED, enabled);
    }

    private static final String USER_NAME = "falcon_user_name";

    /** Name used in the new tab greeting; empty = none. */
    public static String getUserName() {
        return prefs().readString(USER_NAME, "");
    }

    public static void setUserName(String name) {
        prefs().writeString(USER_NAME, name == null ? "" : name.trim());
    }

    private static final String SAVE_TREE_URI = "falcon_save_tree_uri";

    /** SAF tree URI finished downloads are copied into; empty = the Downloads collection. */
    public static String getSaveTreeUri() {
        return prefs().readString(SAVE_TREE_URI, "");
    }

    public static void setSaveTreeUri(String uri) {
        prefs().writeString(SAVE_TREE_URI, uri == null ? "" : uri);
    }

    /** "Downloads" or the picked folder's path ("Movies/Falcon") for labels. */
    public static String saveToLabel() {
        String tree = getSaveTreeUri();
        if (tree.isEmpty()) return "Downloads";
        try {
            String id = android.provider.DocumentsContract.getTreeDocumentId(
                    android.net.Uri.parse(tree));
            int colon = id.indexOf(':');
            return colon >= 0 && colon < id.length() - 1 ? id.substring(colon + 1) : id;
        } catch (IllegalArgumentException e) {
            return "Downloads";
        }
    }

    /** Hours before an idle tab is archived (Chromium tab declutter); 0 = never. */
    public static int getArchiveHours() {
        TabArchiveSettings settings = new TabArchiveSettings(ChromeSharedPreferences.getInstance());
        try {
            return settings.getArchiveEnabled() ? settings.getArchiveTimeDeltaHours() : 0;
        } finally {
            settings.destroy();
        }
    }

    public static void setArchiveHours(int hours) {
        TabArchiveSettings settings = new TabArchiveSettings(ChromeSharedPreferences.getInstance());
        try {
            settings.setArchiveEnabled(hours > 0);
            if (hours > 0) settings.setArchiveTimeDeltaHours(hours);
        } finally {
            settings.destroy();
        }
    }

    public static int getDownloaderConnections() {
        int n = readIntSafe(DOWNLOADER_CONNECTIONS, DEFAULT_DOWNLOADER_CONNECTIONS);
        return Math.max(1, Math.min(16, n));
    }

    public static void setDownloaderConnections(int connections) {
        prefs().writeInt(DOWNLOADER_CONNECTIONS, Math.max(1, Math.min(16, connections)));
    }

    public static boolean isDownloaderVerifyEnabled() {
        return prefs().readBoolean(DOWNLOADER_VERIFY, true);
    }

    public static void setDownloaderVerifyEnabled(boolean enabled) {
        prefs().writeBoolean(DOWNLOADER_VERIFY, enabled);
    }

    public static boolean isDownloaderWifiOnly() {
        return prefs().readBoolean(DOWNLOADER_WIFI_ONLY, false);
    }

    public static void setDownloaderWifiOnly(boolean wifiOnly) {
        prefs().writeBoolean(DOWNLOADER_WIFI_ONLY, wifiOnly);
    }

    public static boolean isPitchBlack() {
        return prefs().readBoolean(PITCH_BLACK, false);
    }

    public static void setPitchBlack(boolean pitchBlack) {
        prefs().writeBoolean(PITCH_BLACK, pitchBlack);
    }

    private static final String MEDIA_CAPTURE = "falcon_media_capture";
    private static final String MEDIA_CAPTURE_OFF_SITES = "falcon_media_capture_off_sites";
    private static final String GRABBER_BUTTON_Y = "falcon_grabber_button_y";
    private static final String GRABBER_BUTTON_LEFT = "falcon_grabber_button_left";

    /** Media grabber: list the audio/video pages load (Settings › Falcon › Capture audio & video). */
    public static boolean isMediaCaptureEnabled() {
        return prefs().readBoolean(MEDIA_CAPTURE, true);
    }

    public static void setMediaCaptureEnabled(boolean enabled) {
        prefs().writeBoolean(MEDIA_CAPTURE, enabled);
    }

    /** Per-site switch, keyed on host: capture is on everywhere except the sites listed. */
    public static boolean isMediaCaptureOffFor(String host) {
        if (host == null || host.isEmpty()) return false;
        return prefs().readStringSet(MEDIA_CAPTURE_OFF_SITES).contains(host);
    }

    public static void setMediaCaptureFor(String host, boolean on) {
        if (host == null || host.isEmpty()) return;
        if (on) {
            prefs().removeFromStringSet(MEDIA_CAPTURE_OFF_SITES, host);
        } else {
            prefs().addToStringSet(MEDIA_CAPTURE_OFF_SITES, host);
        }
    }

    /** Where the floating grab button was dragged to: fraction of the height, and which edge. */
    public static float getGrabberButtonY() {
        return prefs().readFloat(GRABBER_BUTTON_Y, 0.62f);
    }

    public static boolean isGrabberButtonLeft() {
        return prefs().readBoolean(GRABBER_BUTTON_LEFT, false);
    }

    public static void setGrabberButtonPosition(float y, boolean left) {
        prefs().writeFloat(GRABBER_BUTTON_Y, y);
        prefs().writeBoolean(GRABBER_BUTTON_LEFT, left);
    }
}
