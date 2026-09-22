/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ntp;

import android.text.TextUtils;

import org.chromium.base.Callback;
import org.chromium.base.Log;
import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.net.ChromiumNetworkAdapter;
import org.chromium.net.NetworkTrafficAnnotationTag;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Ambient weather for the space home, same source as the desktop NTP widget: Open-Meteo
 * (keyless). The city is geocoded once; the reading is cached for 30 minutes in shared prefs so
 * every new tab does not hit the network.
 */
public final class FalconWeather {
    private static final String TAG = "FalconWeather";
    private static final String CITY = "falcon_weather_city";
    private static final String NAME = "falcon_weather_name";
    private static final String LAT = "falcon_weather_lat";
    private static final String LON = "falcon_weather_lon";
    private static final String TEMP = "falcon_weather_temp";
    private static final String CODE = "falcon_weather_code";
    private static final String FETCHED = "falcon_weather_fetched";
    private static final long FRESH_MS = 30 * 60 * 1000L;

    private static final NetworkTrafficAnnotationTag TRAFFIC_ANNOTATION =
            NetworkTrafficAnnotationTag.createComplete(
                    "falcon_weather",
                    "semantics {"
                            + "  sender: 'Falcon new tab page'"
                            + "  description: 'Current weather for the city the user typed in"
                            + " Settings, from the keyless Open-Meteo API.'"
                            + "  trigger: 'Opening a new tab, at most every 30 minutes.'"
                            + "  data: 'The city name (geocoding) or its coordinates.'"
                            + "  destination: OTHER"
                            + "}"
                            + "policy {"
                            + "  cookies_allowed: NO"
                            + "  setting: 'Settings > Falcon > Weather city (empty = off).'"
                            + "  policy_exception_justification: 'Personal fork; no policy.'"
                            + "}");

    /** A cached or freshly fetched reading. */
    public static final class Reading {
        public final String place;
        public final int temp;
        public final int code;

        Reading(String place, int temp, int code) {
            this.place = place;
            this.temp = temp;
            this.code = code;
        }

        public String label() {
            return String.format(Locale.US, "%d° · %s", temp, describe(code));
        }
    }

    private FalconWeather() {}

    private static SharedPreferencesManager prefs() {
        return ChromeSharedPreferences.getInstance();
    }

    public static String getCity() {
        return prefs().readString(CITY, "");
    }

    /** Setting a new city drops the cached coordinates and reading. */
    public static void setCity(String city) {
        String c = city == null ? "" : city.trim();
        if (c.equals(getCity())) return;
        prefs().writeString(CITY, c);
        prefs().removeKey(NAME);
        prefs().removeKey(LAT);
        prefs().removeKey(LON);
        prefs().removeKey(FETCHED);
    }

    /** The cached reading if there is one (possibly stale), else null. */
    public static Reading cached() {
        if (getCity().isEmpty() || !prefs().contains(FETCHED)) return null;
        return new Reading(
                prefs().readString(NAME, getCity()),
                prefs().readInt(TEMP, 0),
                prefs().readInt(CODE, 0));
    }

    /**
     * Calls back on the UI thread with a fresh reading when the cache is older than 30 minutes
     * (nothing happens when the cache is fresh or no city is set).
     */
    public static void refresh(Callback<Reading> onFresh) {
        final String city = getCity();
        if (city.isEmpty()) return;
        if (System.currentTimeMillis() - prefs().readLong(FETCHED, 0) < FRESH_MS) return;
        // Stamp first so parallel new tabs do not all fetch.
        prefs().writeLong(FETCHED, System.currentTimeMillis() - FRESH_MS + 60_000);
        PostTask.postTask(
                TaskTraits.BEST_EFFORT_MAY_BLOCK,
                () -> {
                    Reading r = fetch(city);
                    if (r == null) return;
                    PostTask.postTask(TaskTraits.UI_DEFAULT, () -> onFresh.onResult(r));
                });
    }

    private static Reading fetch(String city) {
        try {
            String name = prefs().readString(NAME, "");
            String lat = prefs().readString(LAT, "");
            String lon = prefs().readString(LON, "");
            if (TextUtils.isEmpty(lat) || TextUtils.isEmpty(lon)) {
                JSONObject geo =
                        new JSONObject(
                                get(
                                        "https://geocoding-api.open-meteo.com/v1/search?name="
                                                + URLEncoder.encode(city, "UTF-8")
                                                + "&count=1&language=en&format=json"));
                JSONArray results = geo.optJSONArray("results");
                if (results == null || results.length() == 0) return null;
                JSONObject first = results.getJSONObject(0);
                name = first.optString("name", city);
                lat = String.valueOf(first.getDouble("latitude"));
                lon = String.valueOf(first.getDouble("longitude"));
                prefs().writeString(NAME, name);
                prefs().writeString(LAT, lat);
                prefs().writeString(LON, lon);
            }
            JSONObject wx =
                    new JSONObject(
                            get(
                                    "https://api.open-meteo.com/v1/forecast?latitude="
                                            + lat
                                            + "&longitude="
                                            + lon
                                            + "&current=temperature_2m,weather_code&timezone=auto"));
            JSONObject current = wx.getJSONObject("current");
            int temp = (int) Math.round(current.getDouble("temperature_2m"));
            int code = current.getInt("weather_code");
            prefs().writeInt(TEMP, temp);
            prefs().writeInt(CODE, code);
            prefs().writeLong(FETCHED, System.currentTimeMillis());
            return new Reading(name, temp, code);
        } catch (Exception e) {
            Log.w(TAG, "weather fetch failed", e);
            return null;
        }
    }

    private static String get(String url) throws Exception {
        HttpURLConnection c =
                (HttpURLConnection) ChromiumNetworkAdapter.openConnection(new URL(url), TRAFFIC_ANNOTATION);
        c.setConnectTimeout(10_000);
        c.setReadTimeout(10_000);
        try (InputStream in = c.getInputStream()) {
            byte[] buf = new byte[8192];
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } finally {
            c.disconnect();
        }
    }

    /** WMO weather code → short text (same table as the desktop widget). */
    public static String describe(int code) {
        switch (code) {
            case 0: return "Clear";
            case 1: return "Mostly clear";
            case 2: return "Partly cloudy";
            case 3: return "Overcast";
            case 45: return "Fog";
            case 48: return "Icy fog";
            case 51: return "Light drizzle";
            case 53: return "Drizzle";
            case 55: return "Heavy drizzle";
            case 56:
            case 57: return "Freezing drizzle";
            case 61: return "Light rain";
            case 63: return "Rain";
            case 65: return "Heavy rain";
            case 66:
            case 67: return "Freezing rain";
            case 71: return "Light snow";
            case 73: return "Snow";
            case 75: return "Heavy snow";
            case 77: return "Snow grains";
            case 80:
            case 81: return "Showers";
            case 82: return "Violent showers";
            case 85:
            case 86: return "Snow showers";
            case 95: return "Thunderstorm";
            case 96:
            case 99: return "Thunderstorm, hail";
            default: return "";
        }
    }
}
