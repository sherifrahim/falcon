/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.settings;

import android.os.Bundle;

import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.Preference.OnPreferenceChangeListener;

import org.chromium.base.supplier.MonotonicObservableSupplier;
import org.chromium.base.supplier.ObservableSuppliers;
import org.chromium.base.supplier.SettableMonotonicObservableSupplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.BraveRelaunchUtils;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.night_mode.NightModeMetrics;
import org.chromium.chrome.browser.night_mode.settings.ThemeSettingsFragment;
import org.chromium.chrome.browser.toolbar.bottom.BottomToolbarConfiguration;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.SettingsUtils;

/** falcon settings: bottom bar mode, downloader, look. Everything Falcon adds on Android. */
public class FalconPreferences extends BravePreferenceFragment
        implements OnPreferenceChangeListener {
    public static final String PREF_BOTTOM_BAR_MODE = "falcon_bottom_bar_mode";
    public static final String PREF_DOWNLOADER_ENABLED = "falcon_downloader_enabled";
    public static final String PREF_DOWNLOADER_CONNECTIONS = "falcon_downloader_connections";
    public static final String PREF_DOWNLOADER_VERIFY = "falcon_downloader_verify";
    public static final String PREF_DOWNLOADER_WIFI_ONLY = "falcon_downloader_wifi_only";
    public static final String PREF_PITCH_BLACK = "falcon_pitch_black";
    public static final String PREF_THEME = "falcon_theme";

    private final SettableMonotonicObservableSupplier<String> mPageTitle =
            ObservableSuppliers.createMonotonic();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mPageTitle.set(getString(R.string.falcon_settings));
        SettingsUtils.addPreferencesFromResource(this, R.xml.falcon_preferences);
    }

    @Override
    public MonotonicObservableSupplier<String> getPageTitle() {
        return mPageTitle;
    }

    @Override
    public String getMainMenuKey() {
        return BraveMainPreferencesBase.PREF_FALCON;
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        ListPreference mode = (ListPreference) findPreference(PREF_BOTTOM_BAR_MODE);
        if (mode != null) {
            mode.setValue(String.valueOf(FalconPrefs.getBottomBarMode()));
            mode.setSummary(mode.getEntry());
            mode.setOnPreferenceChangeListener(this);
        }

        ListPreference connections =
                (ListPreference) findPreference(PREF_DOWNLOADER_CONNECTIONS);
        if (connections != null) {
            connections.setValue(String.valueOf(FalconPrefs.getDownloaderConnections()));
            connections.setSummary(connections.getEntry());
            connections.setOnPreferenceChangeListener(this);
        }

        // The Theme row opens Chromium's picker, which asserts on its entry-point argument.
        Preference theme = findPreference(PREF_THEME);
        if (theme != null) {
            theme.getExtras()
                    .putInt(
                            ThemeSettingsFragment.KEY_THEME_SETTINGS_ENTRY,
                            NightModeMetrics.ThemeSettingsEntry.SETTINGS);
        }

        bindSwitch(PREF_DOWNLOADER_ENABLED, FalconPrefs.isDownloaderEnabled());
        bindSwitch(PREF_DOWNLOADER_VERIFY, FalconPrefs.isDownloaderVerifyEnabled());
        bindSwitch(PREF_DOWNLOADER_WIFI_ONLY, FalconPrefs.isDownloaderWifiOnly());
        bindSwitch(PREF_PITCH_BLACK, FalconPrefs.isPitchBlack());
    }

    private void bindSwitch(String key, boolean checked) {
        ChromeSwitchPreference pref = (ChromeSwitchPreference) findPreference(key);
        if (pref == null) return;
        pref.setChecked(checked);
        pref.setOnPreferenceChangeListener(this);
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        String key = preference.getKey();
        if (PREF_BOTTOM_BAR_MODE.equals(key)) {
            int mode = Integer.parseInt((String) newValue);
            ListPreference list = (ListPreference) preference;
            list.setSummary(list.getEntries()[list.findIndexOfValue((String) newValue)]);
            if (mode != FalconPrefs.getBottomBarMode()) {
                FalconPrefs.setBottomBarMode(mode);
                BottomToolbarConfiguration.applyFalconBottomBarMode(mode);
                // The toolbar layout is chosen when the activity is created.
                BraveRelaunchUtils.askForRelaunch(getActivity());
            }
            return true;
        } else if (PREF_DOWNLOADER_CONNECTIONS.equals(key)) {
            ListPreference list = (ListPreference) preference;
            list.setSummary(list.getEntries()[list.findIndexOfValue((String) newValue)]);
            FalconPrefs.setDownloaderConnections(Integer.parseInt((String) newValue));
            return true;
        } else if (PREF_DOWNLOADER_ENABLED.equals(key)) {
            FalconPrefs.setDownloaderEnabled((boolean) newValue);
            return true;
        } else if (PREF_DOWNLOADER_VERIFY.equals(key)) {
            FalconPrefs.setDownloaderVerifyEnabled((boolean) newValue);
            return true;
        } else if (PREF_DOWNLOADER_WIFI_ONLY.equals(key)) {
            FalconPrefs.setDownloaderWifiOnly((boolean) newValue);
            return true;
        } else if (PREF_PITCH_BLACK.equals(key)) {
            FalconPrefs.setPitchBlack((boolean) newValue);
            BraveRelaunchUtils.askForRelaunch(getActivity());
            return true;
        }
        return false;
    }
}
