// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FALCON_TELEMETRY_EDGE_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_FALCON_TELEMETRY_EDGE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/falcon/download/download_tracker.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserWindowInterface;

namespace falcon {

// Cockpit "telemetry edge": a thin glowing beam on the right edge of the
// window that shows overall download progress while transfers are active
// and is invisible otherwise. Hovering it opens the Downloads side panel.
class TelemetryEdgeView : public views::View,
                          public views::ViewObserver,
                          public DownloadTracker::Observer {
  METADATA_HEADER(TelemetryEdgeView, views::View)

 public:
  static constexpr int kWidth = 6;

  explicit TelemetryEdgeView(BrowserWindowInterface* browser);
  TelemetryEdgeView(const TelemetryEdgeView&) = delete;
  TelemetryEdgeView& operator=(const TelemetryEdgeView&) = delete;
  ~TelemetryEdgeView() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void AddedToWidget() override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;

  // DownloadTracker::Observer:
  void OnDownloadsChanged(const DownloadTracker::Snapshot& snapshot) override;

 private:
  void UpdateBounds();
  void UpdateVisibility();

  raw_ptr<BrowserWindowInterface> browser_;
  DownloadTracker::Snapshot snapshot_;
  base::ScopedObservation<views::View, views::ViewObserver> parent_observation_{
      this};
  base::ScopedObservation<DownloadTracker, DownloadTracker::Observer>
      tracker_observation_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_UI_VIEWS_FALCON_TELEMETRY_EDGE_VIEW_H_
