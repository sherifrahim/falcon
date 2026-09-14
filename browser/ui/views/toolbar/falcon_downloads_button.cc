// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/toolbar/falcon_downloads_button.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "base/functional/bind.h"
#include "base/i18n/number_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "base/byte_size.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/controls/progress_ring_utils.h"
#include "url/gurl.h"

namespace {

constexpr SkColor kRingTrack = SkColorSetA(SK_ColorGRAY, 0x50);
constexpr SkColor kRingProgress = SkColorSetRGB(0x1D, 0x9B, 0xFF);
constexpr SkColor kDoneDot = SkColorSetRGB(0x22, 0xC5, 0x5E);

}  // namespace

FalconDownloadsButton::FalconDownloadsButton(BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&FalconDownloadsButton::ButtonPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(kDownloadToolbarButtonChromeRefreshOldIcon);
  UpdateTooltip();
  observation_.Observe(falcon::Aria2Service::Get()->tracker());
}

FalconDownloadsButton::~FalconDownloadsButton() {
  SetCallback(PressedCallback());
}

void FalconDownloadsButton::OnDownloadsChanged(
    const falcon::DownloadTracker::Snapshot& snapshot) {
  snapshot_ = snapshot;
  UpdateTooltip();
  SchedulePaint();
}

void FalconDownloadsButton::OnDownloadFinished(
    const falcon::DownloadTracker::Finished& finished) {
  if (finished.success) {
    has_new_completed_ = true;
    SchedulePaint();
  }
}

void FalconDownloadsButton::UpdateTooltip() {
  std::u16string tip = u"Falcon Downloads";
  if (snapshot_.active > 0) {
    tip = base::StrCat(
        {base::FormatNumber(snapshot_.active), u" downloading · ",
         ui::FormatSpeed(base::ByteSize(
             static_cast<uint64_t>(std::max<int64_t>(0, snapshot_.download_speed))))});
    if (snapshot_.total_bytes > 0) {
      tip += base::StrCat(
          {u" · ",
           base::FormatNumber(snapshot_.completed_bytes * 100 /
                              snapshot_.total_bytes),
           u"%"});
    }
  }
  SetTooltipText(tip);
}

void FalconDownloadsButton::ButtonPressed() {
  has_new_completed_ = false;
  SchedulePaint();
  ShowSingletonTab(browser_,
                   GURL(std::string("chrome://") + kFalconDownloaderHost));
}

void FalconDownloadsButton::PaintButtonContents(gfx::Canvas* canvas) {
  ToolbarButton::PaintButtonContents(canvas);

  if (snapshot_.active <= 0 && !has_new_completed_) {
    return;
  }
  constexpr int kRingRadius = 9;
  const int cx = width() / 2;
  const int cy = height() / 2;

  if (snapshot_.active > 0) {
    const gfx::RectF ring(cx - kRingRadius, cy - kRingRadius, 2 * kRingRadius,
                          2 * kRingRadius);
    float sweep = 360.f;
    if (snapshot_.total_bytes > 0) {
      sweep = 360.f * static_cast<float>(snapshot_.completed_bytes) /
              static_cast<float>(snapshot_.total_bytes);
    }
    views::DrawProgressRing(canvas, gfx::RectFToSkRect(ring), kRingTrack,
                            kRingProgress, 2.f, /*start_angle=*/-90.f, sweep);
  } else {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kDoneDot);
    canvas->DrawCircle(gfx::PointF(cx + kRingRadius - 1.f, cy - kRingRadius + 1.f),
                       3.f, flags);
  }
}

BEGIN_METADATA(FalconDownloadsButton)
END_METADATA
