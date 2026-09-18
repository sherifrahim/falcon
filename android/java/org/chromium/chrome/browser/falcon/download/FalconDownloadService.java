/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;

import androidx.core.app.NotificationCompat;
import androidx.core.app.ServiceCompat;
import androidx.core.content.ContextCompat;

import org.chromium.base.Log;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;

import java.util.Locale;

/**
 * Keeps the process alive while downloads run and mirrors them into notifications: one
 * foreground summary plus a progress notification per download with pause / resume / cancel.
 */
public class FalconDownloadService extends Service implements FalconDownloadManager.Observer {
    private static final String TAG = "FalconDownload";
    private static final String CHANNEL_ID = "falcon_downloads";
    private static final int SUMMARY_ID = 0x7a1c0001;
    private static final int ITEM_ID_BASE = 0x7a1c1000;

    static final String ACTION_START = "falcon.download.START";
    static final String ACTION_PAUSE = "falcon.download.PAUSE";
    static final String ACTION_RESUME = "falcon.download.RESUME";
    static final String ACTION_CANCEL = "falcon.download.CANCEL";
    static final String EXTRA_ID = "id";

    private static boolean sRunning;

    private NotificationManager mNotifications;
    private long mLastSummaryUpdate;

    static void ensureRunning(Context context) {
        if (sRunning) return;
        Intent intent = new Intent(context, FalconDownloadService.class).setAction(ACTION_START);
        try {
            ContextCompat.startForegroundService(context, intent);
        } catch (RuntimeException e) {
            // Background start restrictions: the download still runs in-process; only the
            // notification is missing until the app is foregrounded.
            Log.w(TAG, "could not start the download service: %s", e.getMessage());
        }
    }

    static void onIdle(Context context) {
        if (!sRunning) return;
        Intent intent = new Intent(context, FalconDownloadService.class);
        context.stopService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        sRunning = true;
        mNotifications = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        createChannel();
        FalconDownloadManager.getInstance().addObserver(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? ACTION_START : intent.getAction();
        String id = intent == null ? null : intent.getStringExtra(EXTRA_ID);
        FalconDownloadManager manager = FalconDownloadManager.getInstance();
        if (ACTION_PAUSE.equals(action) && id != null) {
            manager.pause(id);
        } else if (ACTION_RESUME.equals(action) && id != null) {
            manager.resume(id);
        } else if (ACTION_CANCEL.equals(action) && id != null) {
            manager.cancel(id);
            mNotifications.cancel(itemNotificationId(id));
        }
        goForeground(SUMMARY_ID, buildSummary(manager));
        onDownloadsChanged();
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        FalconDownloadManager.getInstance().removeObserver(this);
        sRunning = false;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // ── observer ──

    @Override
    public void onDownloadsChanged() {
        FalconDownloadManager manager = FalconDownloadManager.getInstance();
        long now = System.currentTimeMillis();
        boolean throttle = now - mLastSummaryUpdate < 900;
        if (!throttle) {
            mLastSummaryUpdate = now;
            mNotifications.notify(SUMMARY_ID, buildSummary(manager));
            for (DownloadItem item : manager.items()) {
                if (item.isActive() || item.state == State.PAUSED
                        || item.state == State.WAITING_WIFI || item.state == State.QUEUED) {
                    mNotifications.notify(itemNotificationId(item.id), buildItem(item));
                }
            }
        }
        if (manager.activeCount() == 0 && manager.queuedCount() == 0) {
            // Nothing running: leave the per-item notifications (paused ones are actionable),
            // drop the foreground summary.
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE);
            stopSelf();
        }
    }

    @Override
    public void onDownloadFinished(DownloadItem item) {
        mNotifications.notify(itemNotificationId(item.id), buildFinished(item));
    }

    // ── notifications ──

    private void createChannel() {
        NotificationChannel channel =
                new NotificationChannel(
                        CHANNEL_ID,
                        getString(R.string.falcon_downloads_section),
                        NotificationManager.IMPORTANCE_LOW);
        channel.setShowBadge(false);
        mNotifications.createNotificationChannel(channel);
    }

    private void goForeground(int id, Notification n) {
        ServiceCompat.startForeground(this, id, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    }

    private Notification buildSummary(FalconDownloadManager manager) {
        int active = manager.activeCount();
        int queued = manager.queuedCount();
        String title =
                active == 0
                        ? getString(R.string.falcon_downloads_section)
                        : String.format(Locale.US, "%d downloading", active);
        String text =
                active == 0
                        ? (queued > 0 ? queued + " queued" : "")
                        : formatSpeed(manager.totalSpeedBps())
                                + (queued > 0 ? " · " + queued + " queued" : "");
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle(title)
                .setContentText(text)
                .setOnlyAlertOnce(true)
                .setOngoing(active > 0)
                .setGroup(CHANNEL_ID)
                .setGroupSummary(true)
                .setContentIntent(openPage())
                .build();
    }

    private Notification buildItem(DownloadItem item) {
        long done = item.doneBytes();
        boolean indeterminate = item.totalBytes <= 0;
        String status;
        switch (item.state) {
            case State.ACTIVE:
                status =
                        DownloadItem.formatBytes(done)
                                + (indeterminate ? "" : " of " + DownloadItem.formatBytes(item.totalBytes))
                                + " · "
                                + formatSpeed(item.speedBps)
                                + (item.etaSeconds >= 0 ? " · " + formatEta(item.etaSeconds) : "");
                break;
            case State.PAUSED:
                status = "Paused · " + DownloadItem.formatBytes(done);
                break;
            case State.WAITING_WIFI:
                status = "Waiting for Wi-Fi";
                break;
            default:
                status = "Queued";
        }
        NotificationCompat.Builder b =
                new NotificationCompat.Builder(this, CHANNEL_ID)
                        .setSmallIcon(
                                item.state == State.ACTIVE
                                        ? android.R.drawable.stat_sys_download
                                        : android.R.drawable.stat_sys_download_done)
                        .setContentTitle(item.fileName)
                        .setContentText(status)
                        .setOnlyAlertOnce(true)
                        .setOngoing(item.state == State.ACTIVE)
                        .setGroup(CHANNEL_ID)
                        .setContentIntent(openPage())
                        .setProgress(100, item.progressPercent(), indeterminate && item.isActive());
        if (item.state == State.ACTIVE) {
            b.addAction(0, "Pause", action(ACTION_PAUSE, item.id));
        } else {
            b.addAction(0, "Resume", action(ACTION_RESUME, item.id));
        }
        b.addAction(0, "Cancel", action(ACTION_CANCEL, item.id));
        return b.build();
    }

    private Notification buildFinished(DownloadItem item) {
        NotificationCompat.Builder b =
                new NotificationCompat.Builder(this, CHANNEL_ID)
                        .setSmallIcon(android.R.drawable.stat_sys_download_done)
                        .setContentTitle(item.fileName)
                        .setContentText(
                                "Download complete · " + DownloadItem.formatBytes(item.totalBytes))
                        .setAutoCancel(true)
                        .setGroup(CHANNEL_ID);
        Intent open = FalconDownloadManager.getInstance().openIntent(item);
        if (open != null) {
            b.setContentIntent(
                    PendingIntent.getActivity(
                            this,
                            item.id.hashCode(),
                            open,
                            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }
        return b.build();
    }

    private PendingIntent openPage() {
        Intent intent =
                new Intent(this, org.chromium.chrome.browser.falcon.download.ui.FalconDownloadsActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(
                this, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private PendingIntent action(String action, String id) {
        Intent intent =
                new Intent(this, FalconDownloadService.class)
                        .setAction(action)
                        .putExtra(EXTRA_ID, id);
        return PendingIntent.getService(
                this,
                (action + id).hashCode(),
                intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private static int itemNotificationId(String id) {
        return ITEM_ID_BASE + (id.hashCode() & 0x0fff);
    }

    static String formatSpeed(long bps) {
        if (bps < 1024) return bps + " B/s";
        double kb = bps / 1024.0;
        if (kb < 1024) return String.format(Locale.US, "%.0f KB/s", kb);
        return String.format(Locale.US, "%.1f MB/s", kb / 1024.0);
    }

    static String formatEta(long seconds) {
        if (seconds < 60) return seconds + " s";
        if (seconds < 3600) return String.format(Locale.US, "%d:%02d", seconds / 60, seconds % 60);
        return String.format(Locale.US, "%dh %02dm", seconds / 3600, (seconds % 3600) / 60);
    }
}
