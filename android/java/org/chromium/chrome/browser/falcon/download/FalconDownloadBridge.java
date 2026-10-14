/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.app.Activity;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.ui.DownloadPromptSheet;

/** Native → Java entry points for downloads Chromium hands over to Falcon. */
@JNINamespace("falcon")
public final class FalconDownloadBridge {
    private FalconDownloadBridge() {}

    /** Decides synchronously whether Falcon takes this download (called on the UI thread). */
    @CalledByNative
    static boolean shouldIntercept(String url, String fileName, String mimeType, long contentLength) {
        return FalconDownloadManager.wantsDownload(url, fileName, mimeType, contentLength);
    }

    /** Starts the download once the page's cookies have been collected. */
    @CalledByNative
    static void start(
            String url,
            String referer,
            String userAgent,
            String fileName,
            String mimeType,
            long contentLength,
            String cookies) {
        ThreadUtils.runOnUiThread(
                () -> {
                    Activity activity = ApplicationStatus.getLastTrackedFocusedActivity();
                    if (FalconPrefs.isDownloaderAskFirst()
                            && activity != null
                            && !activity.isFinishing()) {
                        DownloadPromptSheet.show(
                                activity, url, referer, userAgent, cookies, fileName, mimeType,
                                contentLength);
                        return;
                    }
                    FalconDownloadManager.getInstance()
                            .enqueue(url, referer, userAgent, cookies, fileName, mimeType,
                                    contentLength);
                });
    }

    /** A blob:/data: download Chromium finished itself; |location| is a content URI or path. */
    @CalledByNative
    static void adopt(String location, String url, String fileName, String mimeType, long size) {
        ThreadUtils.runOnUiThread(
                () ->
                        FalconDownloadManager.getInstance()
                                .adoptFinished(location, url, fileName, mimeType, size));
    }
}
