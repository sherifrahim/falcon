/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ntp;

import org.json.JSONException;
import org.json.JSONObject;

import org.chromium.base.Log;
import org.chromium.chrome.browser.preferences.BravePref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.components.user_prefs.UserPrefs;

/**
 * The home page configuration, shared with the desktop and carried between devices by sync.
 *
 * <p>The desktop new tab page owns the schema (components/falcon_newtab_ui/components/state.ts) and
 * stores the whole thing as one JSON string in the syncable pref {@code falcon.ntp.config}. Android
 * reads the fields it can honour and leaves the rest alone, so a value it does not understand
 * survives a round trip through this device.
 */
public final class FalconNtpConfig {
    private static final String TAG = "FalconNtpConfig";

    private FalconNtpConfig() {}

    private static JSONObject read() {
        Profile profile = ProfileManager.getLastUsedRegularProfile();
        String json = UserPrefs.get(profile).getString(BravePref.NTP_CONFIG);
        if (json == null || json.isEmpty()) {
            return new JSONObject();
        }
        try {
            return new JSONObject(json);
        } catch (JSONException e) {
            Log.w(TAG, "home page config is not valid JSON; ignoring it");
            return new JSONObject();
        }
    }

    /** Merges one top-level value in, leaving every other key untouched. */
    private static void write(String key, Object value) {
        JSONObject root = read();
        try {
            root.put(key, value);
        } catch (JSONException e) {
            return;
        }
        Profile profile = ProfileManager.getLastUsedRegularProfile();
        UserPrefs.get(profile).setString(BravePref.NTP_CONFIG, root.toString());
    }

    /** True when the desktop has never written a config, so local settings still apply. */
    public static boolean isEmpty() {
        return read().length() == 0;
    }

    public static String name() {
        return read().optString("name", "");
    }

    public static void setName(String name) {
        write("name", name == null ? "" : name.trim());
    }

    public static boolean showGreeting() {
        return read().optBoolean("showGreeting", true);
    }

    public static boolean showQuote() {
        return read().optBoolean("showQuote", true);
    }

    public static boolean weatherEnabled() {
        JSONObject w = read().optJSONObject("weather");
        return w == null || w.optBoolean("enabled", true);
    }

    public static String weatherCity() {
        JSONObject w = read().optJSONObject("weather");
        return w == null ? "" : w.optString("city", "");
    }

    public static void setWeatherCity(String city) {
        JSONObject root = read();
        JSONObject w = root.optJSONObject("weather");
        if (w == null) {
            w = new JSONObject();
        }
        try {
            w.put("city", city == null ? "" : city.trim());
        } catch (JSONException e) {
            return;
        }
        write("weather", w);
    }

    /** True when the desktop is set to Fahrenheit. */
    public static boolean fahrenheit() {
        JSONObject w = read().optJSONObject("weather");
        return w != null && "f".equals(w.optString("unit", "c"));
    }
}
