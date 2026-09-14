// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/falcon/download/download_tracker.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserWindowInterface;

// Toolbar entry point for Falcon's download engine. Shows a progress ring
// around the icon while downloads are active and opens falcon://downloader.
class FalconDownloadsButton : public ToolbarButton,
                              public falcon::DownloadTracker::Observer {
  METADATA_HEADER(FalconDownloadsButton, ToolbarButton)
 public:
  explicit FalconDownloadsButton(BrowserWindowInterface* browser);
  FalconDownloadsButton(const FalconDownloadsButton&) = delete;
  FalconDownloadsButton& operator=(const FalconDownloadsButton&) = delete;
  ~FalconDownloadsButton() override;

  // falcon::DownloadTracker::Observer:
  void OnDownloadsChanged(
      const falcon::DownloadTracker::Snapshot& snapshot) override;
  void OnDownloadFinished(
      const falcon::DownloadTracker::Finished& finished) override;

  // ToolbarButton:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void ButtonPressed();
  void UpdateTooltip();

  raw_ptr<BrowserWindowInterface> browser_;
  falcon::DownloadTracker::Snapshot snapshot_;
  bool has_new_completed_ = false;  // dot until the user opens the page
  base::ScopedObservation<falcon::DownloadTracker,
                          falcon::DownloadTracker::Observer>
      observation_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_
