// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/falcon/telemetry_edge_view.h"

#include <algorithm>

#include "brave/browser/falcon/download/aria2_service.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace falcon {

namespace {

constexpr SkColor kSky = SkColorSetRGB(0x38, 0xBD, 0xF8);
constexpr int kTopMargin = 48;
constexpr int kBottomMargin = 48;

}  // namespace

TelemetryEdgeView::TelemetryEdgeView(BrowserWindowInterface* browser)
    : browser_(browser) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  if (auto* service = Aria2Service::Get(); service && service->tracker()) {
    tracker_observation_.Observe(service->tracker());
  }
}

TelemetryEdgeView::~TelemetryEdgeView() = default;

void TelemetryEdgeView::AddedToWidget() {
  if (parent()) {
    parent_observation_.Observe(parent());
    UpdateBounds();
  }
}

void TelemetryEdgeView::OnViewBoundsChanged(views::View* observed_view) {
  UpdateBounds();
}

void TelemetryEdgeView::UpdateBounds() {
  if (!parent()) {
    return;
  }
  const int h = std::max(0, parent()->height() - kTopMargin - kBottomMargin);
  SetBoundsRect(gfx::Rect(parent()->width() - kWidth, kTopMargin, kWidth, h));
}

void TelemetryEdgeView::OnDownloadsChanged(
    const DownloadTracker::Snapshot& snapshot) {
  snapshot_ = snapshot;
  UpdateVisibility();
  SchedulePaint();
}

void TelemetryEdgeView::UpdateVisibility() {
  const bool show = snapshot_.active > 0;
  if (show != GetVisible()) {
    SetVisible(show);
    if (show && parent()) {
      parent()->ReorderChildView(this, -1);
    }
  }
}

void TelemetryEdgeView::OnPaint(gfx::Canvas* canvas) {
  const gfx::Rect local = GetLocalBounds();
  if (local.IsEmpty()) {
    return;
  }
  double fraction = 0.0;
  if (snapshot_.total_bytes > 0) {
    fraction = std::clamp(
        static_cast<double>(snapshot_.completed_bytes) / snapshot_.total_bytes,
        0.0, 1.0);
  }
  // Faint track.
  cc::PaintFlags track;
  track.setColor(SkColorSetA(kSky, 0x22));
  track.setAntiAlias(true);
  canvas->DrawRoundRect(gfx::RectF(local.x() + 2, local.y(), 2, local.height()),
                        1.0f, track);
  // Beam grows from the bottom.
  const int beam_h = std::max(24, static_cast<int>(local.height() * fraction));
  const gfx::RectF beam(local.x() + 1, local.bottom() - beam_h, 4, beam_h);
  cc::PaintFlags glow;
  glow.setColor(SkColorSetA(kSky, 0x55));
  glow.setAntiAlias(true);
  gfx::RectF halo = beam;
  halo.Inset(-3.0f);
  canvas->DrawRoundRect(halo, 4.0f, glow);
  cc::PaintFlags core;
  core.setColor(kSky);
  core.setAntiAlias(true);
  canvas->DrawRoundRect(beam, 2.0f, core);
}

void TelemetryEdgeView::OnMouseEntered(const ui::MouseEvent& event) {
  if (!browser_) {
    return;
  }
  if (auto* side_panel = SidePanelUI::From(browser_)) {
    if (!side_panel->IsSidePanelShowing()) {
      side_panel->Show(SidePanelEntryKey(SidePanelEntryId::kFalconDownloads));
    }
  }
}

BEGIN_METADATA(TelemetryEdgeView)
END_METADATA

}  // namespace falcon
