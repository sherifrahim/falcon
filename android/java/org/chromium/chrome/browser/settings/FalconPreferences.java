/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.settings;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;

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
    public static final String PREF_ARCHIVE_TABS = "falcon_archive_tabs";
    public static final String PREF_SAVE_TO = "falcon_save_to";

    private ActivityResultLauncher<Uri> mPickFolder;

    private final SettableMonotonicObservableSupplier<String> mPageTitle =
            ObservableSuppliers.createMonotonic();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mPageTitle.set(getString(R.string.falcon_settings));
        SettingsUtils.addPreferencesFromResource(this, R.xml.falcon_preferences);
        mPickFolder =
                registerForActivityResult(
                        new ActivityResultContracts.OpenDocumentTree(),
                        uri -> {
                            if (uri == null) return;
                            try {
                                requireContext()
                                        .getContentResolver()
                                        .takePersistableUriPermission(
                                                uri,
                                                Intent.FLAG_GRANT_READ_URI_PERMISSION
                                                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                            } catch (SecurityException e) {
                                return;
                            }
                            FalconPrefs.setSaveTreeUri(uri.toString());
                            updateSaveToSummary();
                        });
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

        // Arc-style auto-archive rides on Chromium's tab declutter; the row maps
        // Never / 12 h / 1 d / 3 d / 1 w onto its enabled flag + hours.
        ListPreference archive = (ListPreference) findPreference(PREF_ARCHIVE_TABS);
        if (archive != null) {
            archive.setValue(String.valueOf(FalconPrefs.getArchiveHours()));
            if (archive.getEntry() == null) archive.setValue("0");
            archive.setSummary(archive.getEntry());
            archive.setOnPreferenceChangeListener(this);
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

        Preference saveTo = findPreference(PREF_SAVE_TO);
        if (saveTo != null) {
            updateSaveToSummary();
            saveTo.setOnPreferenceClickListener(
                    p -> {
                        String[] items = {
                            getString(R.string.falcon_save_to_choose),
                            getString(R.string.falcon_save_to_default)
                        };
                        new AlertDialog.Builder(requireContext())
                                .setTitle(R.string.falcon_save_to)
                                .setItems(
                                        items,
                                        (d, which) -> {
                                            if (which == 0) {
                                                mPickFolder.launch(null);
                                            } else {
                                                FalconPrefs.setSaveTreeUri("");
                                                updateSaveToSummary();
                                            }
                                        })
                                .show();
                        return true;
                    });
        }

        bindSwitch(PREF_DOWNLOADER_ENABLED, FalconPrefs.isDownloaderEnabled());
        bindSwitch(PREF_DOWNLOADER_VERIFY, FalconPrefs.isDownloaderVerifyEnabled());
        bindSwitch(PREF_DOWNLOADER_WIFI_ONLY, FalconPrefs.isDownloaderWifiOnly());
        bindSwitch(PREF_PITCH_BLACK, FalconPrefs.isPitchBlack());
    }

    private void updateSaveToSummary() {
        Preference saveTo = findPreference(PREF_SAVE_TO);
        if (saveTo == null) return;
        String tree = FalconPrefs.getSaveTreeUri();
        if (tree.isEmpty()) {
            saveTo.setSummary(R.string.falcon_save_to_default);
            return;
        }
        // "primary:Movies/Falcon" → "Movies/Falcon"
        String id = DocumentsContract.getTreeDocumentId(Uri.parse(tree));
        int colon = id.indexOf(':');
        saveTo.setSummary(colon >= 0 && colon < id.length() - 1 ? id.substring(colon + 1) : id);
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
        } else if (PREF_ARCHIVE_TABS.equals(key)) {
            ListPreference list = (ListPreference) preference;
            list.setSummary(list.getEntries()[list.findIndexOfValue((String) newValue)]);
            FalconPrefs.setArchiveHours(Integer.parseInt((String) newValue));
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
