/**
 * Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package org.chromium.chrome.browser;

import android.os.Handler;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;
import org.json.JSONException;

import org.chromium.brave_rewards.mojom.PublisherStatus;
import org.chromium.chrome.browser.rewards.adaptive_captcha.AdaptiveCaptchaHelper;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.embedder_support.util.UrlConstants;

import java.util.ArrayList;
import java.util.List;

@JNINamespace("chrome::android")
public class BraveRewardsNativeWorker {
    /** Allows to monitor a front tab publisher changes. */
    public interface PublisherObserver {
        void onFrontTabPublisherChanged(boolean verified, String publisherId);
    }

    // Rewards notifications
    // Taken from components/brave_rewards/content/rewards_notification_service.h
    public static final int REWARDS_NOTIFICATION_INVALID = 0;
    public static final int REWARDS_NOTIFICATION_AUTO_CONTRIBUTE = 1;
    public static final int REWARDS_NOTIFICATION_FAILED_CONTRIBUTION = 4;
    public static final int REWARDS_NOTIFICATION_IMPENDING_CONTRIBUTION = 5;
    public static final int REWARDS_NOTIFICATION_TIPS_PROCESSED = 8;
    public static final int REWARDS_NOTIFICATION_ADS_ONBOARDING = 9;
    public static final int REWARDS_NOTIFICATION_VERIFIED_PUBLISHER = 10;
    public static final int REWARDS_NOTIFICATION_PENDING_NOT_ENOUGH_FUNDS = 11;
    public static final int REWARDS_NOTIFICATION_GENERAL = 12;

    public static final int OK = 0;
    public static final int FAILED = 1;
    public static final int BAT_NOT_ALLOWED = 25;
    public static final int SAFETYNET_ATTESTATION_FAILED = 27;

    private String mFrontTabUrl;
    private static final Handler sHandler = new Handler();

    private final List<BraveRewardsObserver> mObservers;
    private final List<PublisherObserver> mFrontTabPublisherObservers;
    private long mNativeBraveRewardsNativeWorker;

    private static BraveRewardsNativeWorker sInstance;
    private static final Object sLock = new Object();

    public static BraveRewardsNativeWorker getInstance() {
        synchronized (sLock) {
            if (sInstance == null) {
                sInstance = new BraveRewardsNativeWorker();
                sInstance.init();
                if (sInstance.mNativeBraveRewardsNativeWorker == 0) {
                    sInstance = null;
                }
            }
        }
        return sInstance;
    }

    private BraveRewardsNativeWorker() {
        mObservers = new ArrayList<BraveRewardsObserver>();
        mFrontTabPublisherObservers = new ArrayList<PublisherObserver>();
    }

    private void init() {
      if (mNativeBraveRewardsNativeWorker == 0) {
          if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().init(BraveRewardsNativeWorker.this);
      }
    }

    /**
     * A finalizer is required to ensure that the native object associated with this descriptor gets
     * torn down, otherwise there would be a memory leak.
     */
    @SuppressWarnings("Finalize")
    @Override
    protected void finalize() {
        destroy();
    }

    private void destroy() {
        if (mNativeBraveRewardsNativeWorker != 0) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().destroy(mNativeBraveRewardsNativeWorker);
            mNativeBraveRewardsNativeWorker = 0;
        }
    }

    public void addObserver(BraveRewardsObserver observer) {
        synchronized (sLock) {
            mObservers.add(observer);
        }
    }

    public void removeObserver(BraveRewardsObserver observer) {
        synchronized (sLock) {
            mObservers.remove(observer);
        }
    }

    public void addPublisherObserver(PublisherObserver observer) {
        synchronized (sLock) {
            mFrontTabPublisherObservers.add(observer);
        }
    }

    public void removePublisherObserver(PublisherObserver observer) {
        synchronized (sLock) {
            mFrontTabPublisherObservers.remove(observer);
        }
    }

    public void onNotifyFrontTabUrlChanged(int tabId, String url) {
        boolean internalUrl = url.startsWith(UrlConstants.CHROME_SCHEME);
        boolean newUrl = (mFrontTabUrl == null || !mFrontTabUrl.equals(url));
        if (internalUrl) {
            // Don't query 'GetPublisherInfo' and post response now.
            sHandler.post(
                    new Runnable() {
                        @Override
                        public void run() {
                            notifyPublisherObservers(false, "");
                        }
                    });
        } else if (newUrl) {
            getPublisherInfo(tabId, url);
        }

        mFrontTabUrl = url;
    }

    private void notifyPublisherObservers(boolean verified, String publisherId) {
        for (PublisherObserver observer : mFrontTabPublisherObservers) {
            observer.onFrontTabPublisherChanged(verified, publisherId);
        }
    }

    public void triggerOnNotifyFrontTabUrlChanged() {
        // Clear mFrontTabUrl so that all observers are updated.
        mFrontTabUrl = "";
        sHandler.post(
                new Runnable() {
                    @Override
                    public void run() {
                        Tab tab = BraveRewardsHelper.currentActiveChromeTabbedActivityTab();
                        if (tab != null && !tab.isIncognito()) {
                            onNotifyFrontTabUrlChanged(tab.getId(), tab.getUrl().getSpec());
                        }
                    }
                });
    }

    public boolean isSupported() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().isSupported(mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public boolean isSupportedSkipRegionCheck() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().isSupportedSkipRegionCheck(
                    mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public boolean isRewardsEnabled() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().isRewardsEnabled(
                    mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public boolean shouldShowSelfCustodyInvite() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get()
                    .shouldShowSelfCustodyInvite(mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public void createRewardsWallet(String countryCode) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().createRewardsWallet(
                    mNativeBraveRewardsNativeWorker, countryCode);
        }
    }

    public void getRewardsParameters() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getRewardsParameters(mNativeBraveRewardsNativeWorker);
        }
    }

    public double getVbatDeadline() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getVbatDeadline(
                    mNativeBraveRewardsNativeWorker) : 0d;
        }
    }

    public void getUserType() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getUserType(mNativeBraveRewardsNativeWorker);
        }
    }

    public void fetchBalance() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().fetchBalance(mNativeBraveRewardsNativeWorker);
        }
    }

    @Nullable
    public BraveRewardsBalance getWalletBalance() {
        synchronized (sLock) {
            String json = (BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getWalletBalance(
                    mNativeBraveRewardsNativeWorker) : "");
            BraveRewardsBalance balance = null;
            try{
                balance = new BraveRewardsBalance(json);
            }
            catch (JSONException e) {
                balance = null;
            }
            return balance;
        }
    }

    public String getExternalWalletType() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getExternalWalletType(
                    mNativeBraveRewardsNativeWorker) : "";
        }
    }

    public boolean canConnectAccount() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().canConnectAccount(
                    mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public double[] getTipChoices() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getTipChoices(mNativeBraveRewardsNativeWorker) : new double[0];
        }
    }

    public double getWalletRate() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getWalletRate(mNativeBraveRewardsNativeWorker) : 0d;
        }
    }

    public void getPublisherInfo(int tabId, String host) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getPublisherInfo(
                    mNativeBraveRewardsNativeWorker, tabId, host);
        }
    }

    public String getPublisherURL(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherURL(
                    mNativeBraveRewardsNativeWorker, tabId) : "";
        }
    }

    public void startAttestation(String captchaId, String paymentId) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .startAttestation(mNativeBraveRewardsNativeWorker, captchaId, paymentId);
        }
    }

    public void attestPaymentId(
            String captchaId,
            String paymentId,
            String integrityToken,
            String uniqueValue,
            String packageName) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .attestPaymentId(
                            mNativeBraveRewardsNativeWorker,
                            captchaId,
                            paymentId,
                            integrityToken,
                            uniqueValue,
                            packageName);
        }
    }

    @CalledByNative
    public void onAttestationNeedsIntegrityToken(String captchaId, String paymentId, String nonce) {
        AdaptiveCaptchaHelper.fetchPlayIntegrityToken(captchaId, paymentId, nonce);
    }

    public String getPublisherFavIconURL(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherFavIconURL(
                    mNativeBraveRewardsNativeWorker, tabId) : "";
        }
    }

    public String getPublisherName(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherName(
                    mNativeBraveRewardsNativeWorker, tabId) : "";
        }
    }

    public String getPublisherId(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherId(
                    mNativeBraveRewardsNativeWorker, tabId) : "";
        }
    }

    public int getPublisherPercent(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherPercent(
                    mNativeBraveRewardsNativeWorker, tabId) : 0;
        }
    }

    public boolean getPublisherExcluded(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherExcluded(
                    mNativeBraveRewardsNativeWorker, tabId) : false;
        }
    }

    public int getPublisherStatus(int tabId) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherStatus(
                    mNativeBraveRewardsNativeWorker, tabId) : 0;
        }
    }

    public void removePublisherFromMap(int tabId) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().removePublisherFromMap(
                    mNativeBraveRewardsNativeWorker, tabId);
        }
    }

    public void getCurrentBalanceReport() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getCurrentBalanceReport(
                    mNativeBraveRewardsNativeWorker);
        }
    }

    public void donate(String publisherKey, double amount, boolean recurring) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .donate(mNativeBraveRewardsNativeWorker, publisherKey, amount, recurring);
        }
    }

    public void getAllNotifications() {
        sHandler.post(
                new Runnable() {
                    @Override
                    public void run() {
                        synchronized (sLock) {
                            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                                    .getAllNotifications(mNativeBraveRewardsNativeWorker);
                        }
                    }
                });
    }

    public void deleteNotification(String notificationId) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .deleteNotification(mNativeBraveRewardsNativeWorker, notificationId);
        }
    }

    public void getRecurringDonations() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getRecurringDonations(
                    mNativeBraveRewardsNativeWorker);
        }
    }

    public boolean isCurrentPublisherInRecurrentDonations(String publisher) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().isCurrentPublisherInRecurrentDonations(
                    mNativeBraveRewardsNativeWorker, publisher) : false;
        }
    }

    public double getPublisherRecurrentDonationAmount(String publisher) {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPublisherRecurrentDonationAmount(
                    mNativeBraveRewardsNativeWorker, publisher) : 0d;
        }
    }

    public void getReconcileStamp() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getReconcileStamp(mNativeBraveRewardsNativeWorker);
        }
    }

    public void removeRecurring(String publisher) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().removeRecurring(
                    mNativeBraveRewardsNativeWorker, publisher);
        }
    }

    public void resetTheWholeState() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().resetTheWholeState(mNativeBraveRewardsNativeWorker);
        }
    }

    public int getAdsPerHour() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getAdsPerHour(mNativeBraveRewardsNativeWorker) : 0;
        }
    }

    public void setAdsPerHour(int value) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().setAdsPerHour(mNativeBraveRewardsNativeWorker, value);
        }
    }

    public void getExternalWallet() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getExternalWallet(mNativeBraveRewardsNativeWorker);
        }
    }

    public boolean isTermsOfServiceUpdateRequired() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get()
                    .isTermsOfServiceUpdateRequired(mNativeBraveRewardsNativeWorker) : false;
        }
    }

    public void acceptTermsOfServiceUpdate() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .acceptTermsOfServiceUpdate(mNativeBraveRewardsNativeWorker);
        }
    }

    public String getCountryCode() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getCountryCode(
                    mNativeBraveRewardsNativeWorker) : "";
        }
    }

    public void getAvailableCountries() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getAvailableCountries(
                    mNativeBraveRewardsNativeWorker);
        }
    }

    public void getPublisherBanner(String publisherKey) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get()
                    .getPublisherBanner(mNativeBraveRewardsNativeWorker, publisherKey);
        }
    }

    public void getPublishersVisitedCount() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getPublishersVisitedCount(
                    mNativeBraveRewardsNativeWorker);
        }
    }

    @CalledByNative
    public void onGetPublishersVisitedCount(int count) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetPublishersVisitedCount(count);
        }
    }

    public void disconnectWallet() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().disconnectWallet(mNativeBraveRewardsNativeWorker);
        }
    }

    public void getAdsAccountStatement() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().getAdsAccountStatement(
                    mNativeBraveRewardsNativeWorker);
        }
    }

    @CalledByNative
    public void onCreateRewardsWallet(String result) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onCreateRewardsWallet(result);
        }
    }

    public void refreshPublisher(String publisherKey) {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().refreshPublisher(
                    mNativeBraveRewardsNativeWorker, publisherKey);
        }
    }

    public void recordPanelTrigger() {
        synchronized (sLock) {
            if (BraveConfig.ENABLE_REWARDS) BraveRewardsNativeWorkerJni.get().recordPanelTrigger(mNativeBraveRewardsNativeWorker);
        }
    }

    public String getPayoutStatus() {
        synchronized (sLock) {
            return BraveConfig.ENABLE_REWARDS ? BraveRewardsNativeWorkerJni.get().getPayoutStatus(
                    mNativeBraveRewardsNativeWorker) : "";
        }
    }

    @CalledByNative
    public void onRefreshPublisher(int status, String publisherKey) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onRefreshPublisher(status, publisherKey);
        }
    }

    @CalledByNative
    public void onRewardsParameters() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onRewardsParameters();
        }
    }

    @CalledByNative
    public void onTermsOfServiceUpdateAccepted() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onTermsOfServiceUpdateAccepted();
        }
    }

    @CalledByNative
    public void onBalance(boolean success) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onBalance(success);
        }
    }

    @CalledByNative
    public void onGetCurrentBalanceReport(double[] report) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetCurrentBalanceReport(report);
        }
    }

    @CalledByNative
    private void setNativePtr(long nativePtr) {
        assert mNativeBraveRewardsNativeWorker == 0;
        mNativeBraveRewardsNativeWorker = nativePtr;
    }

    @CalledByNative
    public void onPublisherInfo(int tabId, String publisherId) {
        int pubStatus = getPublisherStatus(tabId);
        boolean verified = pubStatus != PublisherStatus.NOT_VERIFIED;
        notifyPublisherObservers(verified, publisherId);

        // Notify BraveRewardsObserver (panel).
        for (BraveRewardsObserver observer : mObservers) {
            observer.onPublisherInfo(tabId);
        }
    }

    @CalledByNative
    public void onNotificationAdded(String id, int type, long timestamp, String[] args) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onNotificationAdded(id, type, timestamp, args);
        }
    }

    @CalledByNative
    public void onNotificationsCount(int count) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onNotificationsCount(count);
        }
    }

    @CalledByNative
    public void onGetLatestNotification(String id, int type, long timestamp, String[] args) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetLatestNotification(id, type, timestamp, args);
        }
    }

    @CalledByNative
    public void onNotificationDeleted(String id) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onNotificationDeleted(id);
        }
    }

    @CalledByNative
    public void onGetReconcileStamp(long timestamp) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetReconcileStamp(timestamp);
        }
    }

    @CalledByNative
    public void onRecurringDonationUpdated() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onRecurringDonationUpdated();
        }
    }

    @CalledByNative
    public void onCompleteReset(boolean success) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onCompleteReset(success);
        }
    }

    @CalledByNative
    public void onResetTheWholeState(boolean success) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onResetTheWholeState(success);
        }
    }

    @CalledByNative
    public void onGetExternalWallet(String externalWallet) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetExternalWallet(externalWallet);
        }
    }

    @CalledByNative
    public void onGetAvailableCountries(String[] countries) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetAvailableCountries(countries);
        }
    }

    @CalledByNative
    public void onGetAdsAccountStatement(
            boolean success,
            double nextPaymentDate,
            int adsReceivedThisMonth,
            double minEarningsThisMonth,
            double maxEarningsThisMonth,
            double minEarningsLastMonth,
            double maxEarningsLastMonth) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetAdsAccountStatement(
                    success,
                    nextPaymentDate,
                    adsReceivedThisMonth,
                    minEarningsThisMonth,
                    maxEarningsThisMonth,
                    minEarningsLastMonth,
                    maxEarningsLastMonth);
        }
    }

    @CalledByNative
    public void onExternalWalletConnected() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onExternalWalletConnected();
        }
    }

    @CalledByNative
    public void onExternalWalletLoggedOut() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onExternalWalletLoggedOut();
        }
    }

    @CalledByNative
    public void onExternalWalletReconnected() {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onExternalWalletReconnected();
        }
    }

    @CalledByNative
    public void onSendContribution(boolean result) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onSendContribution(result);
        }
    }

    @CalledByNative
    public void onReconcileComplete(int resultCode, int rewardsType, double amount) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onReconcileComplete(resultCode, rewardsType, amount);
        }
    }

    @CalledByNative
    public void onPublisherBanner(String jsonBannerInfo) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onPublisherBanner(jsonBannerInfo);
        }
    }

    @CalledByNative
    public void onGetUserType(int userType) {
        for (BraveRewardsObserver observer : mObservers) {
            observer.onGetUserType(userType);
        }
    }

    // Public so that tests outside this package can mock the rewards natives.
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    @NativeMethods
    public interface Natives {
        void init(BraveRewardsNativeWorker caller);

        void destroy(long nativeBraveRewardsNativeWorker);

        boolean isSupported(long nativeBraveRewardsNativeWorker);

        boolean isSupportedSkipRegionCheck(long nativeBraveRewardsNativeWorker);

        boolean isRewardsEnabled(long nativeBraveRewardsNativeWorker);

        boolean shouldShowSelfCustodyInvite(long nativeBraveRewardsNativeWorker);

        String getWalletBalance(long nativeBraveRewardsNativeWorker);
        String getExternalWalletType(long nativeBraveRewardsNativeWorker);

        void getPublisherBanner(long nativeBraveRewardsNativeWorker, String publisherKey);

        void getPublishersVisitedCount(long nativeBraveRewardsNativeWorker);
        boolean canConnectAccount(long nativeBraveRewardsNativeWorker);

        double[] getTipChoices(long nativeBraveRewardsNativeWorker);

        double getWalletRate(long nativeBraveRewardsNativeWorker);

        void getPublisherInfo(long nativeBraveRewardsNativeWorker, int tabId, String host);

        String getPublisherURL(long nativeBraveRewardsNativeWorker, int tabId);

        void startAttestation(
                long nativeBraveRewardsNativeWorker, String captchaId, String paymentId);

        void attestPaymentId(
                long nativeBraveRewardsNativeWorker,
                String captchaId,
                String paymentId,
                String integrityToken,
                String uniqueValue,
                String packageName);

        String getPublisherFavIconURL(long nativeBraveRewardsNativeWorker, int tabId);

        String getPublisherName(long nativeBraveRewardsNativeWorker, int tabId);

        String getPublisherId(long nativeBraveRewardsNativeWorker, int tabId);

        int getPublisherPercent(long nativeBraveRewardsNativeWorker, int tabId);

        boolean getPublisherExcluded(long nativeBraveRewardsNativeWorker, int tabId);

        int getPublisherStatus(long nativeBraveRewardsNativeWorker, int tabId);

        void removePublisherFromMap(long nativeBraveRewardsNativeWorker, int tabId);

        void getCurrentBalanceReport(long nativeBraveRewardsNativeWorker);

        void donate(
                long nativeBraveRewardsNativeWorker,
                String publisherKey,
                double amount,
                boolean recurring);

        void getAllNotifications(long nativeBraveRewardsNativeWorker);

        void deleteNotification(long nativeBraveRewardsNativeWorker, String notificationId);

        void getRecurringDonations(long nativeBraveRewardsNativeWorker);

        boolean isCurrentPublisherInRecurrentDonations(
                long nativeBraveRewardsNativeWorker, String publisher);

        void getReconcileStamp(long nativeBraveRewardsNativeWorker);

        double getPublisherRecurrentDonationAmount(
                long nativeBraveRewardsNativeWorker, String publisher);

        void removeRecurring(long nativeBraveRewardsNativeWorker, String publisher);

        void resetTheWholeState(long nativeBraveRewardsNativeWorker);

        int getAdsPerHour(long nativeBraveRewardsNativeWorker);

        void setAdsPerHour(long nativeBraveRewardsNativeWorker, int value);

        void getExternalWallet(long nativeBraveRewardsNativeWorker);

        boolean isTermsOfServiceUpdateRequired(long nativeBraveRewardsNativeWorker);

        void acceptTermsOfServiceUpdate(long nativeBraveRewardsNativeWorker);

        String getCountryCode(long nativeBraveRewardsNativeWorker);
        void getAvailableCountries(long nativeBraveRewardsNativeWorker);
        void disconnectWallet(long nativeBraveRewardsNativeWorker);
        void refreshPublisher(long nativeBraveRewardsNativeWorker, String publisherKey);

        void recordPanelTrigger(long nativeBraveRewardsNativeWorker);

        void createRewardsWallet(long nativeBraveRewardsNativeWorker, String countryCode);

        void getRewardsParameters(long nativeBraveRewardsNativeWorker);

        double getVbatDeadline(long nativeBraveRewardsNativeWorker);

        void getUserType(long nativeBraveRewardsNativeWorker);

        void fetchBalance(long nativeBraveRewardsNativeWorker);

        void getAdsAccountStatement(long nativeBraveRewardsNativeWorker);

        String getPayoutStatus(long nativeBraveRewardsNativeWorker);
    }
}
