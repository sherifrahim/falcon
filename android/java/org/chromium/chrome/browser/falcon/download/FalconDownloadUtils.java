/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.app.Activity;

import org.chromium.base.ApplicationStatus;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.app.download.home.DownloadActivityLauncher;
import org.chromium.chrome.browser.download.DownloadOpenSource;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.ui.FalconDownloadsActivity;
import org.chromium.chrome.browser.profiles.OtrProfileId;
import org.chromium.chrome.browser.tab.Tab;

/**
 * Every {@code DownloadUtils.showDownloadManager} call in the app is redirected here by
 * BraveDownloadUtilsClassAdapter (ChromeTabbedActivity handles the Downloads menu item itself,
 * before BraveActivity ever sees it). Falcon's Downloads page opens when its downloader is on;
 * otherwise Chromium's download home, as upstream does on phones.
 */
public final class FalconDownloadUtils {
    private FalconDownloadUtils() {}

    public static boolean showDownloadManager(
            @Nullable Activity activity,
            @Nullable Tab tab,
            @Nullable OtrProfileId otrProfileId,
            @DownloadOpenSource int source) {
        return showDownloadManager(activity, tab, otrProfileId, source, false);
    }

    public static boolean showDownloadManager(
            @Nullable Activity activity,
            @Nullable Tab tab,
            @Nullable OtrProfileId otrProfileId,
            @DownloadOpenSource int source,
            boolean showPrefetchedContent) {
        if (activity == null) activity = ApplicationStatus.getLastTrackedFocusedActivity();
        if (otrProfileId == null && tab != null) {
            otrProfileId = tab.getProfile().getOtrProfileId();
        }
        if (activity != null
                && FalconPrefs.isDownloaderEnabled()
                && !OtrProfileId.isOffTheRecord(otrProfileId)) {
            FalconDownloadsActivity.launch(activity);
            return true;
        }
        DownloadActivityLauncher.getInstance()
                .showDownloadActivity(activity, otrProfileId, showPrefetchedContent);
        return true;
    }
}
