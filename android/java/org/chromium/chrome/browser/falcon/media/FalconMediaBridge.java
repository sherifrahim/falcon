/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.media;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.content_public.browser.WebContents;

/** Native → Java: media a tab loaded (FalconMediaCaptureTabHelper). UI thread. */
@JNINamespace("falcon")
public final class FalconMediaBridge {
    private FalconMediaBridge() {}

    @CalledByNative
    static void onPageChanged(WebContents webContents, String pageUrl) {
        MediaCaptureStore.getInstance().onPageChanged(webContents, pageUrl);
    }

    @CalledByNative
    static void onMediaCaptured(
            WebContents webContents,
            int kind,
            String url,
            String mimeType,
            long size,
            String pageUrl,
            String pageTitle,
            String referer,
            String userAgent,
            String cookies) {
        if (kind < CapturedMedia.Kind.FILE || kind > CapturedMedia.Kind.DASH) return;
        CapturedMedia media =
                new CapturedMedia(
                        kind, url, mimeType, size, pageUrl, pageTitle, referer, userAgent,
                        cookies);
        MediaCaptureStore.getInstance().onCaptured(webContents, media);
    }
}
