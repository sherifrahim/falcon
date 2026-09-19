/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.ntp;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.text.format.DateFormat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.BraveActivity;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerImpl;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.ui.interpolators.Interpolators;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Binds the Falcon parts of the new tab page (mock v6 screen 1): the space header with the
 * ambient clock, and "Today" — the space's other open tabs, most recent first.
 */
public final class FalconNtp {
    private static final int MAX_TODAY_ROWS = 6;
    private static final int[] TILE_COLORS = {
        0xFF3B5BDB, 0xFFEA4335, 0xFF1F2328, 0xFF1DB954, 0xFFF97316, 0xFF5865F2, 0xFF1D9BF0
    };

    private static final long ENTER_MS = 200;

    private FalconNtp() {}

    /** Call once the NTP layout is inflated; safe to call again on every show. */
    public static void bind(ViewGroup root, Activity activity, Tab currentTab) {
        View header = root.findViewById(R.id.falcon_ntp_header);
        if (header == null) return;

        // The bottom capsule/omnibox is the search box; the NTP's own fake box is noise here.
        View searchBox = root.findViewById(R.id.search_box);
        if (searchBox != null) searchBox.setVisibility(View.GONE);

        TextView date = header.findViewById(R.id.falcon_date);
        date.setText(DateFormat.format("EEE d MMM", new Date()));

        TabModelSelector selector = null;
        try {
            selector = BraveActivity.getBraveActivity().getTabModelSelectorSupplier().get();
        } catch (BraveActivity.BraveActivityNotFoundException e) {
            // No browser activity (e.g. a custom tab): header only.
        }
        TextView meta = header.findViewById(R.id.falcon_space_meta);
        View today = root.findViewById(R.id.falcon_ntp_today);
        if (selector == null || today == null) {
            meta.setText("");
            return;
        }
        TabModel model = selector.getModel(false);
        int count = model.getCount();
        meta.setText(String.format(Locale.US, "%d %s", count, count == 1 ? "tab" : "tabs"));

        List<Tab> tabs = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            Tab t = model.getTabAt(i);
            if (t == null || t == currentTab) continue;
            GURL url = t.getUrl();
            if (url == null || url.isEmpty()) continue;
            tabs.add(t);
        }
        Collections.sort(tabs, (a, b) -> Long.compare(b.getTimestampMillis(), a.getTimestampMillis()));
        ViewGroup rows = today.findViewById(R.id.falcon_today_rows);
        rows.removeAllViews();
        if (tabs.isEmpty()) {
            today.setVisibility(View.GONE);
            return;
        }
        today.setVisibility(View.VISIBLE);
        LayoutInflater inflater = LayoutInflater.from(activity);
        long now = System.currentTimeMillis();
        int shown = 0;
        for (Tab t : tabs) {
            if (shown++ >= MAX_TODAY_ROWS) break;
            View row = inflater.inflate(R.layout.falcon_ntp_tab_row, rows, false);
            TextView icon = row.findViewById(R.id.falcon_tab_row_icon);
            TextView title = row.findViewById(R.id.falcon_tab_row_title);
            TextView age = row.findViewById(R.id.falcon_tab_row_age);
            String host = t.getUrl().getHost();
            String letter = host == null || host.isEmpty() ? "?" : initial(host);
            icon.setText(letter);
            icon.setBackgroundTintList(ColorStateList.valueOf(colorFor(host)));
            String name = t.getTitle();
            title.setText(name == null || name.isEmpty() ? host : name);
            age.setText(ago(now - t.getTimestampMillis()));
            final int tabId = t.getId();
            final TabModelSelector sel = selector;
            row.setOnClickListener(
                    v -> TabModelUtils.selectTabById(sel, tabId, TabSelectionType.FROM_USER));
            rows.addView(row);
            enter(row, shown);
        }
        today.findViewById(R.id.falcon_today_action)
                .setOnClickListener(v -> openTabSwitcher());
    }

    /** Rows settle in one after another: a short rise and fade, staggered per row. */
    private static void enter(View row, int index) {
        float rise = 8 * row.getResources().getDisplayMetrics().density;
        row.setAlpha(0f);
        row.setTranslationY(rise);
        row.animate()
                .alpha(1f)
                .translationY(0f)
                .setStartDelay(40L * index)
                .setDuration(ENTER_MS)
                .setInterpolator(Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR)
                .start();
    }

    private static void openTabSwitcher() {
        try {
            BraveActivity a = BraveActivity.getBraveActivity();
            LayoutManagerImpl layoutManager = a.getLayoutManagerSupplier().get();
            if (layoutManager != null) {
                layoutManager.showLayout(LayoutType.HUB, /* animate= */ true);
            }
        } catch (BraveActivity.BraveActivityNotFoundException e) {
            // Nothing to open.
        }
    }

    static String initial(String host) {
        String h = host.startsWith("www.") ? host.substring(4) : host;
        return h.isEmpty() ? "?" : h.substring(0, 1).toUpperCase(Locale.US);
    }

    static int colorFor(String host) {
        if (host == null) return TILE_COLORS[0];
        return TILE_COLORS[Math.floorMod(host.hashCode(), TILE_COLORS.length)];
    }

    static String ago(long ms) {
        if (ms < 60_000) return "now";
        long m = ms / 60_000;
        if (m < 60) return m + "m";
        long h = m / 60;
        if (h < 24) return h + "h";
        return (h / 24) + "d";
    }
}
