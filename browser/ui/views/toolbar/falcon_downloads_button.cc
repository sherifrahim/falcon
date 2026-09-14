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
#include "brave/browser/falcon/media/media_sniffer_tab_helper.h"
#include "brave/browser/ui/views/toolbar/falcon_media_bubble.h"
#include "brave/components/constants/webui_url_constants.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/font_list.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
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
constexpr SkColor kMediaBadge = SkColorSetRGB(0xF4, 0x3F, 0x5E);

}  // namespace

FalconDownloadsButton::FalconDownloadsButton(BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&FalconDownloadsButton::ButtonPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(kDownloadToolbarButtonChromeRefreshOldIcon);
  UpdateTooltip();
  observation_.Observe(falcon::Aria2Service::Get()->tracker());
  active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
      base::BindRepeating(&FalconDownloadsButton::OnActiveTabChanged,
                          base::Unretained(this)));
  ObserveActiveTab();
}

FalconDownloadsButton::~FalconDownloadsButton() {
  if (bubble_widget_) {
    bubble_widget_->RemoveObserver(this);
    bubble_widget_ = nullptr;
  }
  SetCallback(PressedCallback());
}

void FalconDownloadsButton::OnActiveTabChanged(BrowserWindowInterface*) {
  ObserveActiveTab();
}

void FalconDownloadsButton::ObserveActiveTab() {
  media_observation_.Reset();
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  auto* helper =
      contents ? falcon::MediaSnifferTabHelper::FromWebContents(contents)
               : nullptr;
  if (helper) {
    media_observation_.Observe(helper);
  }
  media_count_ = helper ? static_cast<int>(helper->candidates().size()) : 0;
  UpdateTooltip();
  SchedulePaint();
}

int FalconDownloadsButton::MediaCount() const {
  return media_count_;
}

void FalconDownloadsButton::OnMediaCandidatesChanged(
    content::WebContents* contents) {
  auto* helper = falcon::MediaSnifferTabHelper::FromWebContents(contents);
  media_count_ = helper ? static_cast<int>(helper->candidates().size()) : 0;
  UpdateTooltip();
  SchedulePaint();
}

void FalconDownloadsButton::OnWidgetDestroying(views::Widget* widget) {
  if (widget == bubble_widget_) {
    bubble_widget_->RemoveObserver(this);
    bubble_widget_ = nullptr;
  }
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
  if (media_count_ > 0) {
    tip = base::StrCat({base::FormatNumber(media_count_),
                        media_count_ == 1 ? u" media item on this page"
                                          : u" media items on this page"});
  }
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
  if (bubble_widget_) {
    bubble_widget_->Close();
    return;
  }
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (contents && media_count_ > 0) {
    auto host = falcon::CreateFalconMediaBubble(browser_, contents, this);
    bubble_widget_ =
        views::BubbleDialogDelegate::CreateBubble(std::move(host));
    bubble_widget_->AddObserver(this);
    bubble_widget_->Show();
    return;
  }
  ShowSingletonTab(browser_,
                   GURL(std::string("chrome://") + kFalconDownloaderHost));
}

void FalconDownloadsButton::PaintButtonContents(gfx::Canvas* canvas) {
  ToolbarButton::PaintButtonContents(canvas);

  constexpr int kRingRadius = 9;
  const int cx = width() / 2;
  const int cy = height() / 2;

  if (media_count_ > 0) {
    // Count badge, bottom-right, like a notification pip.
    const std::u16string text = base::FormatNumber(std::min(media_count_, 9));
    const gfx::FontList font = gfx::FontList().DeriveWithSizeDelta(-3);
    const int d = 13;
    const gfx::Rect badge(width() - d - 2, height() - d - 2, d, d);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kMediaBadge);
    canvas->DrawCircle(gfx::PointF(badge.CenterPoint()), d / 2.f, flags);
    canvas->DrawStringRectWithFlags(text, font, SK_ColorWHITE, badge,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  if (snapshot_.active <= 0 && !has_new_completed_) {
    return;
  }

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
