/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.brave.bytecode;

import org.objectweb.asm.ClassVisitor;

/** Routes every DownloadUtils.showDownloadManager call to Falcon's Downloads page. */
public class BraveDownloadUtilsClassAdapter extends BraveClassVisitor {
    static String sDownloadUtilsClassName = "org/chromium/chrome/browser/download/DownloadUtils";
    static String sFalconDownloadUtilsClassName =
            "org/chromium/chrome/browser/falcon/download/FalconDownloadUtils";

    public BraveDownloadUtilsClassAdapter(ClassVisitor visitor) {
        super(visitor);
        changeMethodOwner(
                sDownloadUtilsClassName, "showDownloadManager", sFalconDownloadUtilsClassName);
    }
}
