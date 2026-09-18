/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.text.TextUtils;
import android.text.format.DateFormat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.TextView;

import androidx.recyclerview.widget.DiffUtil;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/** Rows of the Downloads page. */
class DownloadRowAdapter extends RecyclerView.Adapter<DownloadRowAdapter.Holder> {
    interface Listener {
        void onRowClicked(DownloadItem item);

        void onActionClicked(DownloadItem item);
    }

    private final Listener mListener;
    private List<DownloadItem> mItems = new ArrayList<>();

    DownloadRowAdapter(Listener listener) {
        mListener = listener;
        setHasStableIds(true);
    }

    void submit(List<DownloadItem> items) {
        final List<DownloadItem> old = mItems;
        final List<DownloadItem> fresh = new ArrayList<>(items);
        DiffUtil.DiffResult diff =
                DiffUtil.calculateDiff(
                        new DiffUtil.Callback() {
                            @Override
                            public int getOldListSize() {
                                return old.size();
                            }

                            @Override
                            public int getNewListSize() {
                                return fresh.size();
                            }

                            @Override
                            public boolean areItemsTheSame(int a, int b) {
                                return old.get(a).id.equals(fresh.get(b).id);
                            }

                            @Override
                            public boolean areContentsTheSame(int a, int b) {
                                // Live rows re-bind every tick; that's the point.
                                DownloadItem x = old.get(a);
                                return !x.isActive() && x.state == fresh.get(b).state;
                            }
                        });
        mItems = fresh;
        diff.dispatchUpdatesTo(this);
    }

    @Override
    public long getItemId(int position) {
        return mItems.get(position).id.hashCode();
    }

    @Override
    public int getItemCount() {
        return mItems.size();
    }

    @Override
    public Holder onCreateViewHolder(ViewGroup parent, int viewType) {
        View v =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.falcon_download_row, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(Holder h, int position) {
        DownloadItem item = mItems.get(position);
        h.tag.setText(typeTag(item.fileName));
        h.name.setText(item.fileName);
        h.stat.setText(statLine(item));
        h.segments.setItem(item);
        h.action.setImageResource(actionIcon(item));
        h.action.setContentDescription(h.itemView.getContext().getString(actionLabel(item)));
        h.action.setBackgroundResource(
                item.isActive() ? R.drawable.falcon_bg_accent : R.drawable.falcon_bg_button);
        h.action.setImageTintList(
                android.content.res.ColorStateList.valueOf(
                        h.itemView.getContext()
                                .getColor(item.isActive() ? R.color.falcon_accent_ink : R.color.falcon_ink)));
        boolean dim = item.state == State.QUEUED || item.state == State.WAITING_WIFI
                || item.state == State.CANCELLED;
        float alpha = dim ? 0.55f : 1f;
        h.name.setAlpha(alpha);
        h.stat.setAlpha(alpha);
        h.itemView.setOnClickListener(v -> mListener.onRowClicked(item));
        h.action.setOnClickListener(v -> mListener.onActionClicked(item));
    }

    static class Holder extends RecyclerView.ViewHolder {
        final TextView tag;
        final TextView name;
        final TextView stat;
        final ImageButton action;
        final SegmentBarView segments;

        Holder(View v) {
            super(v);
            tag = v.findViewById(R.id.falcon_row_tag);
            name = v.findViewById(R.id.falcon_row_name);
            stat = v.findViewById(R.id.falcon_row_stat);
            action = v.findViewById(R.id.falcon_row_action);
            segments = v.findViewById(R.id.falcon_row_segments);
        }
    }

    // ── text ──

    static String typeTag(String fileName) {
        int dot = fileName.lastIndexOf('.');
        if (dot < 0 || dot == fileName.length() - 1) return "FILE";
        String ext = fileName.substring(dot + 1).toUpperCase(Locale.US);
        return ext.length() > 4 ? ext.substring(0, 4) : ext;
    }

    static String statLine(DownloadItem item) {
        String sep = " · ";
        long done = item.doneBytes();
        switch (item.state) {
            case State.ACTIVE: {
                StringBuilder sb = new StringBuilder();
                if (item.totalBytes > 0) {
                    sb.append(item.progressPercent()).append('%').append(sep);
                    sb.append(DownloadItem.formatBytes(done)).append('/')
                            .append(DownloadItem.formatBytes(item.totalBytes));
                } else {
                    sb.append(DownloadItem.formatBytes(done));
                }
                sb.append(sep).append(formatSpeed(item.speedBps));
                if (item.etaSeconds >= 0) sb.append(sep).append(formatEta(item.etaSeconds));
                int segs = item.segments().size();
                if (segs > 1) sb.append(sep).append(segs).append(" seg");
                return sb.toString();
            }
            case State.PAUSED:
                return "paused" + sep + DownloadItem.formatBytes(done)
                        + (item.totalBytes > 0 ? "/" + DownloadItem.formatBytes(item.totalBytes) : "")
                        + (item.resumable ? sep + "resumable" : "");
            case State.QUEUED:
                return "queued" + (item.totalBytes > 0 ? sep + DownloadItem.formatBytes(item.totalBytes) : "");
            case State.WAITING_WIFI:
                return "waiting for Wi-Fi" + sep + DownloadItem.formatBytes(item.totalBytes);
            case State.DONE: {
                StringBuilder sb = new StringBuilder("done");
                sb.append(sep).append(DownloadItem.formatBytes(item.totalBytes));
                if (!TextUtils.isEmpty(item.sha256Actual)) sb.append(sep).append("SHA ✓");
                if (item.finishedAt > 0) {
                    sb.append(sep).append(DateFormat.format("HH:mm", new Date(item.finishedAt)));
                }
                return sb.toString();
            }
            case State.FAILED:
                return "failed" + (TextUtils.isEmpty(item.error) ? "" : sep + item.error);
            default:
                return "cancelled";
        }
    }

    static int actionIcon(DownloadItem item) {
        switch (item.state) {
            case State.ACTIVE:
                return R.drawable.falcon_ic_pause;
            case State.PAUSED:
            case State.FAILED:
            case State.CANCELLED:
                return R.drawable.falcon_ic_play;
            case State.QUEUED:
            case State.WAITING_WIFI:
                return R.drawable.falcon_ic_clock;
            default:
                return R.drawable.falcon_ic_folder;
        }
    }

    static int actionLabel(DownloadItem item) {
        switch (item.state) {
            case State.ACTIVE:
                return R.string.falcon_pause;
            case State.PAUSED:
            case State.FAILED:
            case State.CANCELLED:
                return R.string.falcon_resume;
            case State.QUEUED:
            case State.WAITING_WIFI:
                return R.string.falcon_pause;
            default:
                return R.string.falcon_open;
        }
    }

    static String formatSpeed(long bps) {
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
