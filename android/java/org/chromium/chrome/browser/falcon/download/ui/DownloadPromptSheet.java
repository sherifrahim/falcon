/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.net.Uri;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import com.google.android.material.bottomsheet.BottomSheetBehavior;
import com.google.android.material.bottomsheet.BottomSheetDialog;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.FalconDownloadManager;
import org.chromium.chrome.browser.falcon.ui.FalconTheme;

/**
 * The download prompt (IDM+'s "new download" dialog): before a download starts, the user can
 * rename the file, see its size and where it goes, choose the number of connections and the
 * Wi-Fi rule, and start now or queue it for later. Dismissing it cancels the download.
 */
public final class DownloadPromptSheet {
    private final BottomSheetDialog mDialog;
    private final float mDp;

    /** Shows the prompt for a download Falcon took over. */
    public static void show(
            Activity activity,
            String url,
            String referer,
            String userAgent,
            String cookies,
            String fileName,
            String mimeType,
            long contentLength) {
        new DownloadPromptSheet(
                        activity, url, referer, userAgent, cookies, fileName, mimeType,
                        contentLength)
                .mDialog
                .show();
    }

    private DownloadPromptSheet(
            Activity activity,
            String url,
            String referer,
            String userAgent,
            String cookies,
            String fileName,
            String mimeType,
            long contentLength) {
        mDp = activity.getResources().getDisplayMetrics().density;
        mDialog = new BottomSheetDialog(activity, R.style.Theme_Falcon_BottomSheet);
        FalconTheme.applyPitchBlack(mDialog.getContext().getTheme(), activity);
        Context ctx = mDialog.getContext();

        LinearLayout root = new LinearLayout(ctx);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundResource(R.drawable.falcon_bg_sheet);
        root.setPadding(dp(18), dp(18), dp(18), dp(20));

        TextView title = text(ctx, "Download file", 20, R.color.falcon_ink);
        title.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        title.setLetterSpacing(-0.03f);
        root.addView(title);

        String host = Uri.parse(url).getHost();
        String size =
                contentLength > 0 ? DownloadItem.formatBytes(contentLength) : "Size unknown";
        String type = TextUtils.isEmpty(mimeType) ? "" : " · " + mimeType;
        root.addView(
                text(ctx, size + type + (TextUtils.isEmpty(host) ? "" : " · " + host), 12.5f,
                        R.color.falcon_ink_2),
                marginTop(4));

        // File name.
        root.addView(label(ctx, "File name"), marginTop(16));
        EditText name = new EditText(ctx);
        name.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);
        name.setSingleLine(true);
        name.setText(fileName);
        name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        name.setTextColor(ctx.getColor(R.color.falcon_ink));
        name.setBackgroundResource(R.drawable.falcon_bg_field);
        name.setPadding(dp(14), 0, dp(14), 0);
        name.setMinHeight(dp(48));
        // Focus selects the name but not the extension, as file managers do.
        name.setOnFocusChangeListener(
                (v, focused) -> {
                    if (!focused) return;
                    String s = name.getText().toString();
                    int dot = s.lastIndexOf('.');
                    name.setSelection(0, dot > 0 ? dot : s.length());
                });
        root.addView(name, marginTop(6));

        // Where it goes (changed in Settings › Falcon › Save to).
        LinearLayout where = row(ctx);
        where.addView(label(ctx, "Save to"), weight());
        where.addView(text(ctx, FalconPrefs.saveToLabel(), 13.5f, R.color.falcon_ink));
        root.addView(where, marginTop(10));

        // Connections.
        final int defaultConnections = FalconPrefs.getDownloaderConnections();
        final int[] connections = {defaultConnections};
        LinearLayout connRow = row(ctx);
        connRow.addView(label(ctx, "Connections"), weight());
        TextView connValue = text(ctx, String.valueOf(defaultConnections), 13.5f, R.color.falcon_ink);
        connValue.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        connRow.addView(connValue);
        root.addView(connRow, marginTop(4));
        SeekBar seek = new SeekBar(ctx);
        seek.setMax(15); // 1..16
        seek.setProgress(defaultConnections - 1);
        seek.setOnSeekBarChangeListener(
                new SeekBar.OnSeekBarChangeListener() {
                    @Override
                    public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                        connections[0] = progress + 1;
                        connValue.setText(String.valueOf(connections[0]));
                    }

                    @Override
                    public void onStartTrackingTouch(SeekBar s) {}

                    @Override
                    public void onStopTrackingTouch(SeekBar s) {}
                });
        root.addView(seek, matchWidth());
        root.addView(
                text(ctx, "More connections can be faster; some servers allow only one.", 11.5f,
                        R.color.falcon_ink_3));

        // Wi-Fi rule and the prompt itself.
        Switch wifi = toggle(ctx, "Wait for Wi-Fi if over 100 MB", FalconPrefs.isDownloaderWifiOnly());
        root.addView(wifi, marginTop(10));
        Switch ask = toggle(ctx, "Ask every time", true);
        root.addView(ask, marginTop(2));

        // Actions at the thumb: Cancel · Later · Download.
        LinearLayout actions = row(ctx);
        actions.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        TextView cancel = button(ctx, "Cancel", false);
        TextView later = button(ctx, "Later", false);
        TextView go = button(ctx, "Download", true);
        actions.addView(cancel);
        actions.addView(later, marginStart(8));
        actions.addView(go, marginStart(8));
        root.addView(actions, marginTop(18));

        View.OnClickListener start =
                v -> {
                    if (!ask.isChecked()) FalconPrefs.setDownloaderAskFirst(false);
                    FalconDownloadManager.Options options = new FalconDownloadManager.Options();
                    options.connections =
                            connections[0] == defaultConnections ? 0 : connections[0];
                    options.wifiOnly = wifi.isChecked();
                    options.startPaused = v == later;
                    String chosen = name.getText().toString().trim();
                    FalconDownloadManager.getInstance()
                            .enqueue(
                                    url, referer, userAgent, cookies,
                                    TextUtils.isEmpty(chosen) ? fileName : chosen, mimeType,
                                    contentLength, false, options);
                    mDialog.dismiss();
                };
        go.setOnClickListener(start);
        later.setOnClickListener(start);
        cancel.setOnClickListener(v -> mDialog.dismiss());

        mDialog.setContentView(root);
        mDialog.getBehavior().setState(BottomSheetBehavior.STATE_EXPANDED);
        mDialog.getBehavior().setSkipCollapsed(true);
    }

    // ── view helpers ──

    private TextView text(Context ctx, String s, float sp, int colorRes) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(ctx.getColor(colorRes));
        return t;
    }

    private TextView label(Context ctx, String s) {
        TextView t = text(ctx, s.toUpperCase(java.util.Locale.ROOT), 11, R.color.falcon_ink_2);
        t.setLetterSpacing(0.08f);
        t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        return t;
    }

    private Switch toggle(Context ctx, String s, boolean on) {
        Switch sw = new Switch(ctx);
        sw.setText(s);
        sw.setChecked(on);
        sw.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f);
        sw.setTextColor(ctx.getColor(R.color.falcon_ink));
        sw.setMinHeight(dp(44));
        return sw;
    }

    private TextView button(Context ctx, String s, boolean primary) {
        TextView b = text(ctx, s, 14, primary ? R.color.falcon_accent_ink : R.color.falcon_ink);
        b.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        b.setGravity(Gravity.CENTER);
        b.setMinHeight(dp(44));
        b.setPadding(dp(18), 0, dp(18), 0);
        b.setBackgroundResource(primary ? R.drawable.falcon_bg_accent : R.drawable.falcon_bg_field);
        b.setClickable(true);
        b.setFocusable(true);
        return b;
    }

    private LinearLayout row(Context ctx) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setMinimumHeight(dp(36));
        return r;
    }

    private LinearLayout.LayoutParams marginTop(int dp) {
        LinearLayout.LayoutParams p = matchWidth();
        p.topMargin = dp(dp);
        return p;
    }

    private LinearLayout.LayoutParams marginStart(int dp) {
        LinearLayout.LayoutParams p =
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        p.setMarginStart(dp(dp));
        return p;
    }

    private static LinearLayout.LayoutParams matchWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private static LinearLayout.LayoutParams weight() {
        return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
    }

    private int dp(int v) {
        return Math.round(v * mDp);
    }
}
