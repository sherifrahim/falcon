// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_DOWNLOADS_SIDE_PANEL_WEB_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_DOWNLOADS_SIDE_PANEL_WEB_VIEW_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "url/gurl.h"

class FalconDownloaderPanelUI;
class Profile;
class SidePanelEntryScope;

// Hosts chrome://downloader.top-chrome (the downloader in "panel" layout).
class FalconDownloadsContentsWrapper : public WebUIContentsWrapper {
 public:
  explicit FalconDownloadsContentsWrapper(Profile* profile);
  ~FalconDownloadsContentsWrapper() override;

  // WebUIContentsWrapper:
  void ReloadWebContents() override;
  base::WeakPtr<WebUIContentsWrapper> GetWeakPtr() override;

 private:
  FalconDownloaderPanelUI* GetWebUIController();

  const GURL webui_url_;
  base::WeakPtrFactory<FalconDownloadsContentsWrapper> weak_ptr_factory_{this};
};

class FalconDownloadsSidePanelWebView : public SidePanelWebUIView {
  METADATA_HEADER(FalconDownloadsSidePanelWebView, SidePanelWebUIView)

 public:
  static std::unique_ptr<views::View> CreateView(Profile* profile,
                                                 SidePanelEntryScope& scope);

  FalconDownloadsSidePanelWebView(
      SidePanelEntryScope& scope,
      std::unique_ptr<FalconDownloadsContentsWrapper> contents_wrapper);
  FalconDownloadsSidePanelWebView(const FalconDownloadsSidePanelWebView&) =
      delete;
  FalconDownloadsSidePanelWebView& operator=(
      const FalconDownloadsSidePanelWebView&) = delete;
  ~FalconDownloadsSidePanelWebView() override;

 private:
  std::unique_ptr<FalconDownloadsContentsWrapper> contents_wrapper_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_DOWNLOADS_SIDE_PANEL_WEB_VIEW_H_
