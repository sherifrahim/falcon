// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_COLLECTIONS_SIDE_PANEL_WEB_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_COLLECTIONS_SIDE_PANEL_WEB_VIEW_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "url/gurl.h"

class FalconCollectionsPanelUI;
class Profile;
class SidePanelEntryScope;

// Hosts chrome://downloader.top-chrome (the downloader in "panel" layout).
class FalconCollectionsContentsWrapper : public WebUIContentsWrapper {
 public:
  explicit FalconCollectionsContentsWrapper(Profile* profile);
  ~FalconCollectionsContentsWrapper() override;

  // WebUIContentsWrapper:
  void ReloadWebContents() override;
  base::WeakPtr<WebUIContentsWrapper> GetWeakPtr() override;

 private:
  FalconCollectionsPanelUI* GetWebUIController();

  const GURL webui_url_;
  base::WeakPtrFactory<FalconCollectionsContentsWrapper> weak_ptr_factory_{this};
};

class FalconCollectionsSidePanelWebView : public SidePanelWebUIView {
  METADATA_HEADER(FalconCollectionsSidePanelWebView, SidePanelWebUIView)

 public:
  static std::unique_ptr<views::View> CreateView(Profile* profile,
                                                 SidePanelEntryScope& scope);

  FalconCollectionsSidePanelWebView(
      SidePanelEntryScope& scope,
      std::unique_ptr<FalconCollectionsContentsWrapper> contents_wrapper);
  FalconCollectionsSidePanelWebView(const FalconCollectionsSidePanelWebView&) =
      delete;
  FalconCollectionsSidePanelWebView& operator=(
      const FalconCollectionsSidePanelWebView&) = delete;
  ~FalconCollectionsSidePanelWebView() override;

 private:
  std::unique_ptr<FalconCollectionsContentsWrapper> contents_wrapper_;
};

#endif  // BRAVE_BROWSER_UI_VIEWS_SIDE_PANEL_FALCON_FALCON_COLLECTIONS_SIDE_PANEL_WEB_VIEW_H_
