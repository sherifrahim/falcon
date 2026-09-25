/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.media;

import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.net.Uri;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.bottomsheet.BottomSheetDialog;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.FalconDownloadManager;
import org.chromium.chrome.browser.falcon.download.ui.FalconDownloadsActivity;
import org.chromium.chrome.browser.falcon.media.CapturedMedia.Probe;
import org.chromium.chrome.browser.falcon.media.CapturedMedia.Variant;
import org.chromium.chrome.browser.falcon.ui.FalconTheme;
import org.chromium.content_public.browser.WebContents;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The grabber (1DM's "captured links"): everything the current page played, newest first. A
 * stream with several qualities opens in place to show them; one tap downloads. The per-site
 * switch at the top stops capturing on this site.
 */
final class GrabberSheet implements MediaCaptureStore.Observer {
    private final Activity mActivity;
    private final WebContents mWebContents;
    private final String mHost;
    private final BottomSheetDialog mDialog;
    private final LinearLayout mList;
    private final TextView mSubtitle;
    private final Set<CapturedMedia> mExpanded = new HashSet<>();
    private final float mDp;

    static void show(Activity activity, WebContents webContents, String pageUrl) {
        new GrabberSheet(activity, webContents, pageUrl).mDialog.show();
    }

    private GrabberSheet(Activity activity, WebContents webContents, String pageUrl) {
        mActivity = activity;
        mWebContents = webContents;
        String host = Uri.parse(pageUrl == null ? "" : pageUrl).getHost();
        mHost = host == null ? "" : host;
        mDp = activity.getResources().getDisplayMetrics().density;

        mDialog = new BottomSheetDialog(activity, R.style.Theme_Falcon_BottomSheet);
        FalconTheme.applyPitchBlack(mDialog.getContext().getTheme(), activity);
        Context ctx = mDialog.getContext();

        LinearLayout root = new LinearLayout(ctx);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundResource(R.drawable.falcon_bg_sheet);
        root.setPadding(dp(18), dp(18), dp(18), dp(20));

        TextView title = text(ctx, "Media on this page", 20, R.color.falcon_ink);
        title.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        title.setLetterSpacing(-0.03f);
        root.addView(title);
        mSubtitle = text(ctx, "", 12.5f, R.color.falcon_ink_2);
        root.addView(mSubtitle, marginTop(4));

        // Per-site switch (1DM: "capture audio/video" per site).
        LinearLayout siteRow = new LinearLayout(ctx);
        siteRow.setOrientation(LinearLayout.HORIZONTAL);
        siteRow.setGravity(Gravity.CENTER_VERTICAL);
        siteRow.setMinimumHeight(dp(44));
        TextView siteLabel =
                text(ctx, "Capture on " + (mHost.isEmpty() ? "this site" : mHost), 13.5f,
                        R.color.falcon_ink);
        siteLabel.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        siteRow.addView(siteLabel, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Switch siteSwitch = new Switch(ctx);
        siteSwitch.setChecked(!FalconPrefs.isMediaCaptureOffFor(mHost));
        siteSwitch.setOnCheckedChangeListener(
                (b, on) -> {
                    FalconPrefs.setMediaCaptureFor(mHost, on);
                    if (!on) MediaCaptureStore.getInstance().clear(mWebContents);
                    render();
                });
        siteRow.addView(siteSwitch);
        root.addView(siteRow, marginTop(8));
        root.addView(hairline(ctx));

        // The list scrolls inside at most 60% of the screen so the footer always shows.
        final int maxListHeight = (int) (activity.getResources().getDisplayMetrics().heightPixels * 0.6f);
        ScrollView scroll =
                new ScrollView(ctx) {
                    @Override
                    protected void onMeasure(int widthSpec, int heightSpec) {
                        super.onMeasure(
                                widthSpec,
                                MeasureSpec.makeMeasureSpec(maxListHeight, MeasureSpec.AT_MOST));
                    }
                };
        mList = new LinearLayout(ctx);
        mList.setOrientation(LinearLayout.VERTICAL);
        mList.setPadding(0, dp(10), 0, 0);
        scroll.addView(mList);
        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout footer = new LinearLayout(ctx);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        footer.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        TextView clear = footerButton(ctx, "Clear list");
        clear.setOnClickListener(v -> MediaCaptureStore.getInstance().clear(mWebContents));
        TextView downloads = footerButton(ctx, "Downloads");
        downloads.setOnClickListener(
                v -> {
                    mDialog.dismiss();
                    FalconDownloadsActivity.launch(mActivity);
                });
        footer.addView(clear);
        footer.addView(downloads);
        root.addView(footer, marginTop(10));

        mDialog.setContentView(root);
        MediaCaptureStore.getInstance().addObserver(this);
        mDialog.setOnDismissListener(d -> MediaCaptureStore.getInstance().removeObserver(this));
        render();
    }

    @Override
    public void onCapturesChanged(WebContents webContents) {
        if (webContents == mWebContents) render();
    }

    private void render() {
        Context ctx = mDialog.getContext();
        List<CapturedMedia> items = MediaCaptureStore.getInstance().visible(mWebContents);
        boolean off = FalconPrefs.isMediaCaptureOffFor(mHost);
        mSubtitle.setText(
                off
                        ? "Capture is off for this site."
                        : items.isEmpty()
                                ? "Nothing yet. Play the video, then open this again."
                                : items.size() + (items.size() == 1 ? " item" : " items")
                                        + " · tap to download");
        mList.removeAllViews();
        for (CapturedMedia m : items) mList.addView(row(ctx, m));
    }

    private View row(Context ctx, CapturedMedia m) {
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundResource(R.drawable.falcon_bg_surface);
        card.setPadding(dp(12), dp(12), dp(12), dp(10));
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cardLp.bottomMargin = dp(6);
        card.setLayoutParams(cardLp);

        LinearLayout top = new LinearLayout(ctx);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout texts = new LinearLayout(ctx);
        texts.setOrientation(LinearLayout.VERTICAL);

        LinearLayout nameLine = new LinearLayout(ctx);
        nameLine.setOrientation(LinearLayout.HORIZONTAL);
        nameLine.setGravity(Gravity.CENTER_VERTICAL);
        TextView tag = text(ctx, m.typeTag(), 11, R.color.falcon_ink_3);
        tag.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        tag.setLetterSpacing(0.08f);
        tag.setPadding(0, 0, dp(8), 0);
        nameLine.addView(tag);
        TextView name = text(ctx, displayName(m), 15, R.color.falcon_ink);
        name.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        nameLine.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        texts.addView(nameLine);

        TextView stat = text(ctx, statLine(m), 11, R.color.falcon_ink_2);
        stat.setTypeface(Typeface.MONOSPACE);
        stat.setSingleLine(true);
        stat.setEllipsize(TextUtils.TruncateAt.END);
        texts.addView(stat, marginTop(4));
        top.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        boolean several = m.variants.size() > 1;
        ImageButton action = new ImageButton(ctx);
        action.setBackgroundResource(R.drawable.falcon_bg_button);
        action.setImageResource(R.drawable.falcon_ic_download);
        action.setContentDescription(several ? "Choose quality" : "Download");
        action.setEnabled(m.isDownloadable());
        action.setAlpha(m.isDownloadable() ? 1f : 0.35f);
        LinearLayout.LayoutParams alp = new LinearLayout.LayoutParams(dp(40), dp(40));
        alp.setMarginStart(dp(12));
        top.addView(action, alp);
        card.addView(top);

        View.OnClickListener onTap =
                v -> {
                    if (m.probe == Probe.PENDING) {
                        toast("Still reading the stream…");
                    } else if (!m.isDownloadable()) {
                        toast(TextUtils.isEmpty(m.note) ? "Can't download this one" : "Can't download: " + m.note);
                    } else if (several) {
                        if (!mExpanded.remove(m)) mExpanded.add(m);
                        render();
                    } else {
                        start(m, m.variants.isEmpty() ? null : m.variants.get(0));
                    }
                };
        card.setOnClickListener(onTap);
        action.setOnClickListener(onTap);

        if (several && mExpanded.contains(m)) {
            for (Variant v : m.variants) card.addView(variantRow(ctx, m, v));
        }
        return card;
    }

    private View variantRow(Context ctx, CapturedMedia m, Variant v) {
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(44));
        row.setBackgroundResource(R.drawable.falcon_bg_pill);
        row.setPadding(dp(12), 0, dp(8), 0);
        TextView label = text(ctx, v.label, 13.5f, R.color.falcon_ink);
        label.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        row.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (v.estimatedBytes > 0) {
            TextView size = text(ctx, "~" + DownloadItem.formatBytes(v.estimatedBytes), 11.5f, R.color.falcon_ink_2);
            size.setTypeface(Typeface.MONOSPACE);
            size.setPadding(0, 0, dp(10), 0);
            row.addView(size);
        }
        android.widget.ImageView icon = new android.widget.ImageView(ctx);
        icon.setImageResource(R.drawable.falcon_ic_download);
        row.addView(icon, new LinearLayout.LayoutParams(dp(20), dp(20)));
        row.setOnClickListener(x -> start(m, v));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        row.setLayoutParams(lp);
        return row;
    }

    private void start(CapturedMedia m, Variant v) {
        FalconDownloadManager manager = FalconDownloadManager.getInstance();
        String fileName = m.suggestedName(v);
        if (v == null || TextUtils.isEmpty(v.spec)) {
            manager.enqueue(m.url, m.referer, m.userAgent, m.cookies, fileName, m.mimeType, m.size > 0 ? m.size : -1);
        } else {
            manager.enqueueStream(m.url, m.referer, m.userAgent, m.cookies, fileName, v.spec, v.estimatedBytes);
        }
        toast("Downloading " + fileName);
        mDialog.dismiss();
    }

    private static String displayName(CapturedMedia m) {
        if (m.kind == CapturedMedia.Kind.FILE) {
            String last = Uri.parse(m.url).getLastPathSegment();
            if (!TextUtils.isEmpty(last) && last.contains(".")) return last;
        }
        return TextUtils.isEmpty(m.pageTitle) ? m.host() : m.pageTitle;
    }

    private static String statLine(CapturedMedia m) {
        String sep = " · ";
        StringBuilder sb = new StringBuilder();
        switch (m.probe) {
            case Probe.PENDING:
                sb.append(m.isStream() ? "reading playlist…" : "checking size…");
                break;
            case Probe.FAILED:
            case Probe.UNSUPPORTED:
                sb.append(TextUtils.isEmpty(m.note) ? "not downloadable" : m.note);
                break;
            default:
                if (m.variants.size() > 1) {
                    sb.append(m.variants.size()).append(" qualities");
                    Variant best = m.variants.get(0);
                    if (best.height > 0) sb.append(sep).append("up to ").append(best.height).append('p');
                } else if (m.size > 0) {
                    sb.append(DownloadItem.formatBytes(m.size));
                } else {
                    sb.append(m.isStream() ? "stream" : "size unknown");
                }
        }
        if (m.durationSeconds > 0) sb.append(sep).append(CapturedMedia.formatDuration(m.durationSeconds));
        sb.append(sep).append(m.host());
        return sb.toString();
    }

    // ── view helpers ──

    private TextView text(Context ctx, String s, float sp, int colorRes) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(ctx.getColor(colorRes));
        return t;
    }

    private TextView footerButton(Context ctx, String s) {
        TextView b = text(ctx, s, 13.5f, R.color.falcon_ink);
        b.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        b.setBackgroundResource(R.drawable.falcon_bg_button);
        b.setGravity(Gravity.CENTER);
        b.setMinHeight(dp(40));
        b.setPadding(dp(16), 0, dp(16), 0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(40));
        lp.setMarginStart(dp(8));
        b.setLayoutParams(lp);
        return b;
    }

    private View hairline(Context ctx) {
        View v = new View(ctx);
        v.setBackgroundColor(ctx.getColor(R.color.falcon_hair));
        v.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
        return v;
    }

    private LinearLayout.LayoutParams marginTop(int dps) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(dps);
        return lp;
    }

    private int dp(float v) {
        return Math.round(v * mDp);
    }

    private void toast(String s) {
        Toast.makeText(mActivity, s, Toast.LENGTH_SHORT).show();
    }
}
