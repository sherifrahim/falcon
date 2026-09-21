/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.falcon.download.ui;

import android.app.Dialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.bottomsheet.BottomSheetDialogFragment;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.falcon.FalconPrefs;
import org.chromium.chrome.browser.falcon.download.FalconDownloadManager;
import org.chromium.chrome.browser.falcon.ui.FalconTheme;

import java.util.Locale;

/** New download (mock v6 screen 6). The clipboard's link, if any, is offered up front. */
public class AddDownloadSheet extends BottomSheetDialogFragment {
    private EditText mUrl;
    private EditText mName;
    private EditText mSha;
    private Switch mWifiOnly;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setStyle(STYLE_NORMAL, R.style.Theme_Falcon_BottomSheet);
        // Pitch black applies to the dialog's own theme (created from the style above).
    }

    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        FalconTheme.applyPitchBlack(dialog.getContext().getTheme(), requireContext());
        return dialog;
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle saved) {
        View v = inflater.inflate(R.layout.falcon_download_add_sheet, container, false);
        mUrl = v.findViewById(R.id.falcon_add_url);
        mName = v.findViewById(R.id.falcon_add_name);
        mSha = v.findViewById(R.id.falcon_add_sha);
        mWifiOnly = v.findViewById(R.id.falcon_add_wifi_only);
        TextView connections = v.findViewById(R.id.falcon_add_connections);
        TextView hint = v.findViewById(R.id.falcon_add_hint);

        connections.setText(String.valueOf(FalconPrefs.getDownloaderConnections()));
        mWifiOnly.setChecked(FalconPrefs.isDownloaderWifiOnly());

        String clip = clipboardLink();
        if (clip != null) {
            mUrl.setText(clip);
            hint.setText(R.string.falcon_new_download_hint_clipboard);
        }
        mUrl.setOnEditorActionListener(
                (tv, action, event) -> {
                    if (action == EditorInfo.IME_ACTION_GO) {
                        submit();
                        return true;
                    }
                    return false;
                });
        v.findViewById(R.id.falcon_add_go).setOnClickListener(x -> submit());
        return v;
    }

    private void submit() {
        String url = mUrl.getText().toString().trim();
        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            if (url.contains(".") && !url.contains(" ") && !url.contains("://")) {
                url = "https://" + url;
            } else {
                Toast.makeText(requireContext(), R.string.falcon_url_invalid, Toast.LENGTH_SHORT).show();
                return;
            }
        }
        String name = mName.getText().toString().trim();
        if (TextUtils.isEmpty(name)) {
            String last = Uri.parse(url).getLastPathSegment();
            name = TextUtils.isEmpty(last) ? "download" : last;
        }
        String sha = mSha.getText().toString().trim().toLowerCase(Locale.US);
        if (!sha.isEmpty() && !sha.matches("[0-9a-f]{64}")) {
            Toast.makeText(requireContext(), R.string.falcon_sha256_invalid, Toast.LENGTH_SHORT).show();
            return;
        }
        FalconPrefs.setDownloaderWifiOnly(mWifiOnly.isChecked());
        FalconDownloadManager.getInstance().enqueueManual(url, name, sha);
        dismissAllowingStateLoss();
    }

    private String clipboardLink() {
        ClipboardManager cm =
                (ClipboardManager) requireContext().getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm == null || !cm.hasPrimaryClip()) return null;
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return null;
        CharSequence text = clip.getItemAt(0).coerceToText(requireContext());
        if (text == null) return null;
        String s = text.toString().trim();
        return s.startsWith("http://") || s.startsWith("https://") ? s : null;
    }
}
