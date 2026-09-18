/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.content.Context;
import android.content.Intent;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;

import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.DownloadItem;
import org.chromium.chrome.browser.falcon.download.DownloadItem.State;
import org.chromium.chrome.browser.falcon.download.FalconDownloadManager;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;
import org.chromium.chrome.browser.settings.FalconPreferences;
import org.chromium.chrome.browser.settings.SettingsNavigationFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** The Downloads page (mock v6 screen 4). */
public class FalconDownloadsActivity extends AsyncInitializationActivity
        implements FalconDownloadManager.Observer, DownloadRowAdapter.Listener {
    private static final int TAB_ALL = 0;
    private static final int TAB_ACTIVE = 1;
    private static final int TAB_QUEUE = 2;
    private static final int TAB_DONE = 3;

    private TelemetryView mTelemetry;
    private TextView mEnginePill;
    private TextView mPauseAll;
    private LinearLayout mTabs;
    private RecyclerView mList;
    private TextView mEmpty;
    private EditText mSearch;
    private DownloadRowAdapter mAdapter;
    private int mTab = TAB_ALL;
    private String mQuery = "";

    public static void launch(Context context) {
        Intent intent = new Intent(context, FalconDownloadsActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        context.startActivity(intent);
    }

    @Override
    public boolean shouldStartGpuProcess() {
        return true;
    }

    @Override
    protected void triggerLayoutInflation() {
        setContentView(R.layout.falcon_downloads_activity);
        mTelemetry = findViewById(R.id.falcon_telemetry);
        mEnginePill = findViewById(R.id.falcon_engine_pill);
        mPauseAll = findViewById(R.id.falcon_pause_all);
        mTabs = findViewById(R.id.falcon_tabs);
        mList = findViewById(R.id.falcon_download_list);
        mEmpty = findViewById(R.id.falcon_empty);
        mSearch = findViewById(R.id.falcon_search);

        mAdapter = new DownloadRowAdapter(this);
        mList.setLayoutManager(new LinearLayoutManager(this));
        mList.setAdapter(mAdapter);
        mList.setItemAnimator(null);

        buildTabs();
        mPauseAll.setOnClickListener(v -> togglePauseAll());
        findViewById(R.id.falcon_back).setOnClickListener(v -> finish());
        findViewById(R.id.falcon_add).setOnClickListener(
                v -> new AddDownloadSheet().show(getSupportFragmentManager(), "falcon-add"));
        findViewById(R.id.falcon_more).setOnClickListener(this::showMenu);
        mSearch.addTextChangedListener(
                new TextWatcher() {
                    @Override
                    public void beforeTextChanged(CharSequence s, int a, int b, int c) {}

                    @Override
                    public void onTextChanged(CharSequence s, int a, int b, int c) {}

                    @Override
                    public void afterTextChanged(Editable s) {
                        mQuery = s.toString().trim().toLowerCase(Locale.US);
                        refresh();
                    }
                });
        onInitialLayoutInflationComplete();
    }

    @Override
    public void onResume() {
        super.onResume();
        FalconDownloadManager.getInstance().addObserver(this);
        refresh();
    }

    @Override
    public void onPause() {
        FalconDownloadManager.getInstance().removeObserver(this);
        super.onPause();
    }

    @Override
    public void onDownloadsChanged() {
        refresh();
    }

    // ── tabs ──

    private void buildTabs() {
        mTabs.removeAllViews();
        String[] labels = {
            getString(R.string.falcon_tab_all),
            getString(R.string.falcon_tab_active),
            getString(R.string.falcon_tab_queue),
            getString(R.string.falcon_tab_done)
        };
        for (int i = 0; i < labels.length; i++) {
            final int index = i;
            TextView t = new TextView(this);
            t.setText(labels[i]);
            t.setTextSize(15);
            t.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
            int padH = (int) (10 * getResources().getDisplayMetrics().density);
            int padV = (int) (8 * getResources().getDisplayMetrics().density);
            t.setPadding(i == 0 ? 0 : padH, padV, padH, padV);
            t.setOnClickListener(v -> {
                mTab = index;
                refresh();
            });
            mTabs.addView(t);
        }
    }

    private void styleTabs(int[] counts) {
        for (int i = 0; i < mTabs.getChildCount(); i++) {
            TextView t = (TextView) mTabs.getChildAt(i);
            boolean on = i == mTab;
            t.setTextColor(getColor(on ? R.color.falcon_ink : R.color.falcon_ink_3));
            CharSequence base = t.getText().toString().split(" ")[0];
            t.setText(counts[i] > 0 && i != TAB_DONE ? base + " " + counts[i] : base);
        }
    }

    // ── data ──

    private void refresh() {
        FalconDownloadManager m = FalconDownloadManager.getInstance();
        List<DownloadItem> all = m.items();
        int[] counts = new int[4];
        List<DownloadItem> shown = new ArrayList<>();
        for (DownloadItem item : all) {
            counts[TAB_ALL]++;
            if (item.isActive()) counts[TAB_ACTIVE]++;
            if (item.state == State.QUEUED || item.state == State.WAITING_WIFI) counts[TAB_QUEUE]++;
            if (item.state == State.DONE) counts[TAB_DONE]++;
            if (!matchesTab(item) || !matchesQuery(item)) continue;
            shown.add(item);
        }
        styleTabs(counts);
        mAdapter.submit(shown);
        mEmpty.setVisibility(shown.isEmpty() ? View.VISIBLE : View.GONE);
        mList.setVisibility(shown.isEmpty() ? View.GONE : View.VISIBLE);

        int active = m.activeCount();
        mTelemetry.setData(
                m.speedHistory(), m.totalSpeedBps(), active, m.queuedCount(), counts[TAB_DONE],
                FalconPrefs.getDownloaderConnections());
        mEnginePill.setText(
                active > 0
                        ? "● " + DownloadRowAdapter.formatSpeed(m.totalSpeedBps())
                        : "● idle");
        mEnginePill.setTextColor(getColor(active > 0 ? R.color.falcon_ok : R.color.falcon_ink_3));
        mPauseAll.setText(active > 0 ? R.string.falcon_pause_all : R.string.falcon_resume_all);
        mPauseAll.setVisibility(active > 0 || hasPaused(all) ? View.VISIBLE : View.INVISIBLE);
    }

    private static boolean hasPaused(List<DownloadItem> items) {
        for (DownloadItem i : items) if (i.state == State.PAUSED) return true;
        return false;
    }

    private boolean matchesTab(DownloadItem item) {
        switch (mTab) {
            case TAB_ACTIVE:
                return item.isActive();
            case TAB_QUEUE:
                return item.state == State.QUEUED || item.state == State.WAITING_WIFI
                        || item.state == State.PAUSED;
            case TAB_DONE:
                return item.state == State.DONE;
            default:
                return true;
        }
    }

    private boolean matchesQuery(DownloadItem item) {
        if (mQuery.isEmpty()) return true;
        return item.fileName.toLowerCase(Locale.US).contains(mQuery)
                || item.url.toLowerCase(Locale.US).contains(mQuery);
    }

    private void togglePauseAll() {
        FalconDownloadManager m = FalconDownloadManager.getInstance();
        if (m.activeCount() > 0) {
            m.pauseAll();
        } else {
            m.resumeAll();
        }
    }

    private void showMenu(View anchor) {
        PopupMenu menu = new PopupMenu(this, anchor);
        Menu items = menu.getMenu();
        items.add(0, 1, 0, R.string.falcon_clear_finished);
        items.add(0, 2, 1, R.string.falcon_settings);
        menu.setOnMenuItemClickListener(
                (MenuItem mi) -> {
                    if (mi.getItemId() == 1) {
                        FalconDownloadManager.getInstance().clearFinished();
                    } else if (mi.getItemId() == 2) {
                        SettingsNavigationFactory.createSettingsNavigation()
                                .startSettings(this, FalconPreferences.class);
                    }
                    return true;
                });
        menu.show();
    }

    // ── rows ──

    @Override
    public void onRowClicked(DownloadItem item) {
        DownloadDetailSheet.newInstance(item.id).show(getSupportFragmentManager(), "falcon-detail");
    }

    @Override
    public void onActionClicked(DownloadItem item) {
        FalconDownloadManager m = FalconDownloadManager.getInstance();
        switch (item.state) {
            case State.ACTIVE:
            case State.QUEUED:
            case State.WAITING_WIFI:
                m.pause(item.id);
                break;
            case State.PAUSED:
            case State.FAILED:
                m.resume(item.id);
                break;
            case State.CANCELLED:
                m.retry(item.id);
                break;
            default: {
                Intent open = m.openIntent(item);
                if (open == null) return;
                try {
                    startActivity(open);
                } catch (android.content.ActivityNotFoundException e) {
                    Toast.makeText(this, R.string.falcon_no_app_to_open, Toast.LENGTH_SHORT).show();
                }
            }
        }
    }
}
