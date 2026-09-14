// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/falcon/download/download_tracker.h"
#include "brave/browser/falcon/media/media_sniffer_tab_helper.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/widget/widget.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

// Toolbar entry point for Falcon's download engine. Shows a progress ring
// around the icon while downloads are active, a count badge when the active
// tab has sniffed media (click = media panel), and otherwise opens
// falcon://downloader.
class FalconDownloadsButton : public ToolbarButton,
                              public falcon::DownloadTracker::Observer,
                              public falcon::MediaSnifferTabHelper::Observer {
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

  // falcon::MediaSnifferTabHelper::Observer:
  void OnMediaCandidatesChanged(content::WebContents* contents) override;

  // ToolbarButton:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void ButtonPressed();
  void UpdateTooltip();
  void OnActiveTabChanged(BrowserWindowInterface* browser);
  void ObserveActiveTab();
  int MediaCount() const;
  void OnBubbleClosing(views::Widget::ClosedReason reason);

  raw_ptr<BrowserWindowInterface> browser_;
  falcon::DownloadTracker::Snapshot snapshot_;
  bool has_new_completed_ = false;  // dot until the user opens the page
  int media_count_ = 0;
  base::ScopedObservation<falcon::DownloadTracker,
                          falcon::DownloadTracker::Observer>
      observation_{this};
  base::ScopedObservation<falcon::MediaSnifferTabHelper,
                          falcon::MediaSnifferTabHelper::Observer>
      media_observation_{this};
  base::CallbackListSubscription active_tab_subscription_;
  std::unique_ptr<views::Widget> bubble_widget_;
  base::WeakPtrFactory<FalconDownloadsButton> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FALCON_DOWNLOADS_BUTTON_H_
