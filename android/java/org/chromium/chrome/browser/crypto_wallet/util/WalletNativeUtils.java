/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.crypto_wallet.util;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.chrome.browser.BraveConfig;
import org.chromium.chrome.browser.profiles.Profile;

@JNINamespace("chrome::android")
public class WalletNativeUtils {
    public static void resetWallet(Profile profile) {
        if (BraveConfig.ENABLE_WALLET) WalletNativeUtilsJni.get().resetWallet(profile);
    }

    public static boolean isUnstoppableDomainsTld(String domain) {
        return BraveConfig.ENABLE_WALLET ? WalletNativeUtilsJni.get().isUnstoppableDomainsTld(domain) : false;
    }

    public static boolean isEnsTld(String domain) {
        return BraveConfig.ENABLE_WALLET ? WalletNativeUtilsJni.get().isEnsTld(domain) : false;
    }

    public static boolean isSnsTld(String domain) {
        return BraveConfig.ENABLE_WALLET ? WalletNativeUtilsJni.get().isSnsTld(domain) : false;
    }

    @NativeMethods
    interface Natives {
        void resetWallet(Profile profile);

        boolean isUnstoppableDomainsTld(String domain);

        boolean isEnsTld(String domain);

        boolean isSnsTld(String domain);
    }
}
