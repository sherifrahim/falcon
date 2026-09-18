/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.ThreadUtils;

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
                () ->
                        FalconDownloadManager.getInstance()
                                .enqueue(url, referer, userAgent, cookies, fileName, mimeType,
                                        contentLength));
    }
}
