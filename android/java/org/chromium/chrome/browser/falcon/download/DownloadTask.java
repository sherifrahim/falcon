/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

/** A running download (plain HTTP or a stream). Pause/cancel are cooperative. */
interface DownloadTask extends Runnable {
    void pause();

    void cancel();
}
