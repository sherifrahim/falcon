/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.annotation.SuppressLint;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.bottomsheet.BottomSheetDialogFragment;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;
import org.chromium.chrome.browser.falcon.download.FalconDownloadManager;

import java.util.Locale;

/** Download detail (mock v6 screen 5): live segments, the three tiles, actions at the thumb. */
@SuppressLint("SetTextI18n") // live numeric readouts; Falcon is English-only for now
public class DownloadDetailSheet extends BottomSheetDialogFragment
        implements FalconDownloadManager.Observer {
    private static final String ARG_ID = "id";

    private String mId;
    private TextView mName;
    private TextView mSource;
    private TextView mEta;
    private TextView mEtaLabel;
    private ProgressBar mProgress;
    private TextView mBytes;
    private TextView mSegmentsLabel;
    private SegmentGridView mGrid;
    private TextView mError;
    private TextView mSecondary;
    private TextView mPrimary;

    public static DownloadDetailSheet newInstance(String id) {
        DownloadDetailSheet f = new DownloadDetailSheet();
        Bundle b = new Bundle();
        b.putString(ARG_ID, id);
        f.setArguments(b);
        return f;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setStyle(STYLE_NORMAL, R.style.Theme_Falcon_BottomSheet);
        mId = getArguments() == null ? "" : getArguments().getString(ARG_ID, "");
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle saved) {
        View v = inflater.inflate(R.layout.falcon_download_detail_sheet, container, false);
        mName = v.findViewById(R.id.falcon_detail_name);
        mSource = v.findViewById(R.id.falcon_detail_source);
        mEta = v.findViewById(R.id.falcon_detail_eta);
        mEtaLabel = v.findViewById(R.id.falcon_detail_eta_label);
        mProgress = v.findViewById(R.id.falcon_detail_progress);
        mBytes = v.findViewById(R.id.falcon_detail_bytes);
        mSegmentsLabel = v.findViewById(R.id.falcon_detail_segments_label);
        mGrid = v.findViewById(R.id.falcon_detail_grid);
        mError = v.findViewById(R.id.falcon_detail_error);
        mSecondary = v.findViewById(R.id.falcon_detail_secondary);
        mPrimary = v.findViewById(R.id.falcon_detail_primary);

        tile(v, R.id.falcon_tile_limit, R.string.falcon_connections,
                String.valueOf(FalconPrefs.getDownloaderConnections()));
        tile(v, R.id.falcon_tile_when_done, R.string.falcon_when_done,
                FalconPrefs.isDownloaderVerifyEnabled() ? "Verify · keep" : "Keep");
        tile(v, R.id.falcon_tile_save_to, R.string.falcon_save_to, "Downloads");

        v.findViewById(R.id.falcon_detail_copy).setOnClickListener(x -> copyUrl());
        mSecondary.setOnClickListener(x -> onSecondary());
        mPrimary.setOnClickListener(x -> onPrimary());
        return v;
    }

    private void tile(View root, int id, int label, String value) {
        View t = root.findViewById(id);
        ((TextView) t.findViewById(R.id.falcon_tile_label)).setText(label);
        ((TextView) t.findViewById(R.id.falcon_tile_value)).setText(value);
    }

    @Override
    public void onStart() {
        super.onStart();
        FalconDownloadManager.getInstance().addObserver(this);
        bind();
    }

    @Override
    public void onStop() {
        FalconDownloadManager.getInstance().removeObserver(this);
        super.onStop();
    }

    @Override
    public void onDownloadsChanged() {
        bind();
    }

    private DownloadItem item() {
        return FalconDownloadManager.getInstance().get(mId);
    }

    private void bind() {
        DownloadItem item = item();
        if (item == null) {
            dismissAllowingStateLoss();
            return;
        }
        mName.setText(item.fileName);
        String host = Uri.parse(item.url).getHost();
        String sep = " · ";
        mSource.setText(
                (host == null ? "" : host)
                        + (item.totalBytes > 0 ? sep + DownloadItem.formatBytes(item.totalBytes) : "")
                        + (item.resumable ? sep + "resumable" : ""));

        int pct = item.progressPercent();
        switch (item.state) {
            case State.ACTIVE:
                mEta.setText(item.etaSeconds >= 0 ? DownloadRowAdapter.formatEta(item.etaSeconds) : "—");
                mEtaLabel.setText(String.format(Locale.US, "ETA · %d%%", pct));
                break;
            case State.DONE:
                mEta.setText("✓");
                mEtaLabel.setText("DONE");
                break;
            case State.PAUSED:
                mEta.setText(String.format(Locale.US, "%d%%", pct));
                mEtaLabel.setText("PAUSED");
                break;
            case State.FAILED:
                mEta.setText("!");
                mEtaLabel.setText("FAILED");
                break;
            default:
                mEta.setText(String.format(Locale.US, "%d%%", pct));
                mEtaLabel.setText(item.state == State.WAITING_WIFI ? "WI-FI" : "QUEUED");
        }
        mProgress.setProgress(
                item.totalBytes > 0 ? (int) (item.doneBytes() * 1000 / item.totalBytes) : 0,
                /* animate= */ true);
        mBytes.setText(
                DownloadItem.formatBytes(item.doneBytes())
                        + (item.totalBytes > 0 ? " of " + DownloadItem.formatBytes(item.totalBytes) : ""));
        int segs = item.segments().size();
        mSegmentsLabel.setText(
                segs > 0 ? String.format(Locale.US, "%d segments · MB/s", segs) : "probing…");
        mGrid.setItem(item);

        if (!TextUtils.isEmpty(item.error) && item.state == State.FAILED) {
            mError.setText(item.error);
            mError.setVisibility(View.VISIBLE);
        } else {
            mError.setVisibility(View.GONE);
        }

        boolean terminal = item.isTerminal();
        mSecondary.setText(terminal ? R.string.falcon_remove : R.string.falcon_cancel);
        mSecondary.setCompoundDrawablesRelativeWithIntrinsicBounds(
                terminal ? R.drawable.falcon_ic_trash : R.drawable.falcon_ic_close, 0, 0, 0);
        int primaryText;
        int primaryIcon;
        switch (item.state) {
            case State.ACTIVE:
                primaryText = R.string.falcon_pause;
                primaryIcon = R.drawable.falcon_ic_pause;
                break;
            case State.DONE:
                primaryText = R.string.falcon_open;
                primaryIcon = R.drawable.falcon_ic_folder;
                break;
            case State.FAILED:
            case State.CANCELLED:
                primaryText = R.string.falcon_retry;
                primaryIcon = R.drawable.falcon_ic_retry;
                break;
            default:
                primaryText = R.string.falcon_resume;
                primaryIcon = R.drawable.falcon_ic_play;
        }
        mPrimary.setText(primaryText);
        mPrimary.setCompoundDrawablesRelativeWithIntrinsicBounds(primaryIcon, 0, 0, 0);
        android.graphics.drawable.Drawable[] ds = mPrimary.getCompoundDrawablesRelative();
        if (ds[0] != null) ds[0].setTint(requireContext().getColor(R.color.falcon_button_primary_ink));
        android.graphics.drawable.Drawable[] ss = mSecondary.getCompoundDrawablesRelative();
        if (ss[0] != null) ss[0].setTint(requireContext().getColor(R.color.falcon_ink));
    }

    private void onPrimary() {
        DownloadItem item = item();
        if (item == null) return;
        FalconDownloadManager m = FalconDownloadManager.getInstance();
        switch (item.state) {
            case State.ACTIVE:
                m.pause(item.id);
                break;
            case State.DONE: {
                Intent open = m.openIntent(item);
                if (open != null) {
                    try {
                        startActivity(open);
                    } catch (android.content.ActivityNotFoundException e) {
                        Toast.makeText(requireContext(), R.string.falcon_no_app_to_open, Toast.LENGTH_SHORT).show();
                    }
                }
                break;
            }
            case State.CANCELLED:
                m.retry(item.id);
                break;
            default:
                m.resume(item.id);
        }
    }

    private void onSecondary() {
        DownloadItem item = item();
        if (item == null) return;
        FalconDownloadManager m = FalconDownloadManager.getInstance();
        if (item.isTerminal()) {
            m.remove(item.id);
            dismissAllowingStateLoss();
        } else {
            m.cancel(item.id);
        }
    }

    private void copyUrl() {
        DownloadItem item = item();
        if (item == null) return;
        ClipboardManager cm =
                (ClipboardManager) requireContext().getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("url", item.url));
        Toast.makeText(requireContext(), R.string.falcon_copied, Toast.LENGTH_SHORT).show();
    }
}
