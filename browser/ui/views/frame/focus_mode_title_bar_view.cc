/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/frame/focus_mode_title_bar_view.h"

#include <array>
#include <string>

#include "base/functional/bind.h"
#include "brave/browser/ui/brave_scheme_utils.h"
#include "brave/ui/color/nala/nala_color_id.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "components/url_formatter/url_formatter.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/background.h"
#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace {

// Falcon "Mac-style" title bar: a little taller, page title + domain.
constexpr int kTitleBarHeight = 28;
constexpr int kFaviconSize = 14;
constexpr int kFaviconLabelSpacing = 6;
constexpr int kLabelFontSize = 12;

gfx::FontList GetLabelFont() {
  const gfx::FontList base;
  return base.DeriveWithSizeDelta(kLabelFontSize - base.GetFontSize());
}

// macOS window controls, drawn: 12 px discs, 8 px apart, glyph on hover.
constexpr int kLightSize = 12;
constexpr int kLightGap = 8;
constexpr int kLightsLeftInset = 12;
constexpr SkColor kLightClose = SkColorSetRGB(0xFF, 0x5F, 0x57);
constexpr SkColor kLightMin = SkColorSetRGB(0xFE, 0xBC, 0x2E);
constexpr SkColor kLightZoom = SkColorSetRGB(0x28, 0xC8, 0x40);
constexpr SkColor kLightInactive = SkColorSetRGB(0x5A, 0x5F, 0x6A);

class TrafficLight : public views::Button {
  METADATA_HEADER(TrafficLight, views::Button)
 public:
  // |which|: 0 close, 1 minimise, 2 zoom.
  TrafficLight(int which, PressedCallback callback)
      : views::Button(std::move(callback)), which_(which) {
    SetPreferredSize(gfx::Size(kLightSize, kLightSize));
    SetFocusBehavior(FocusBehavior::NEVER);
    static constexpr std::array<const char16_t*, 3> kNames = {
        u"Close window", u"Minimise window", u"Zoom window"};
    SetTooltipText(kNames.at(which));
    GetViewAccessibility().SetName(kNames.at(which));
  }
  ~TrafficLight() override = default;

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF b(GetContentsBounds());
    const bool active = GetWidget() && GetWidget()->IsActive();
    static constexpr std::array<SkColor, 3> kColors = {kLightClose, kLightMin,
                                                       kLightZoom};
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(active ? kColors.at(which_) : kLightInactive);
    canvas->DrawCircle(b.CenterPoint(), kLightSize / 2.0f, fill);

    // Hovering any light shows all glyphs (parent repaints siblings).
    if (!(parent() && parent()->IsMouseHovered())) {
      return;
    }
    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(1.5f);
    line.setStrokeCap(cc::PaintFlags::kRound_Cap);
    line.setColor(SkColorSetA(SK_ColorBLACK, 0xA0));
    const gfx::PointF c = b.CenterPoint();
    const float r = 2.6f;
    if (which_ == 0) {
      canvas->DrawLine(gfx::PointF(c.x() - r, c.y() - r),
                       gfx::PointF(c.x() + r, c.y() + r), line);
      canvas->DrawLine(gfx::PointF(c.x() - r, c.y() + r),
                       gfx::PointF(c.x() + r, c.y() - r), line);
    } else if (which_ == 1) {
      canvas->DrawLine(gfx::PointF(c.x() - r - 0.5f, c.y()),
                       gfx::PointF(c.x() + r + 0.5f, c.y()), line);
    } else {
      // Two small triangles (macOS "zoom"): draw as a diagonal with arrow tips.
      canvas->DrawLine(gfx::PointF(c.x() - r, c.y() + r),
                       gfx::PointF(c.x() + r, c.y() - r), line);
      canvas->DrawLine(gfx::PointF(c.x() + r, c.y() - r),
                       gfx::PointF(c.x() + 0.2f, c.y() - r), line);
      canvas->DrawLine(gfx::PointF(c.x() + r, c.y() - r),
                       gfx::PointF(c.x() + r, c.y() + 0.2f), line);
      canvas->DrawLine(gfx::PointF(c.x() - r, c.y() + r),
                       gfx::PointF(c.x() - 0.2f, c.y() + r), line);
      canvas->DrawLine(gfx::PointF(c.x() - r, c.y() + r),
                       gfx::PointF(c.x() - r, c.y() - 0.2f), line);
    }
  }

 private:
  const int which_;
};

BEGIN_METADATA(TrafficLight)
END_METADATA

// Hosts the three lights; repaints them together on hover so the glyphs
// appear as a set, like macOS.
class TrafficLights : public views::View {
  METADATA_HEADER(TrafficLights, views::View)
 public:
  TrafficLights() {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kLightGap));
    SetNotifyEnterExitOnChild(true);
  }
  ~TrafficLights() override = default;
  void OnMouseEntered(const ui::MouseEvent&) override { SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent&) override { SchedulePaint(); }
};

BEGIN_METADATA(TrafficLights)
END_METADATA

}  // namespace

FocusModeTitleBarView::FocusModeTitleBarView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kFaviconLabelSpacing));
  SetBackground(views::CreateSolidBackground(kColorToolbar));

  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetPreferredSize(gfx::Size(0, kTitleBarHeight));

  lights_ = AddChildView(std::make_unique<TrafficLights>());
  lights_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  for (int i = 0; i < 3; ++i) {
    lights_->AddChildView(std::make_unique<TrafficLight>(
        i, base::BindRepeating(&FocusModeTitleBarView::OnTrafficLight,
                               base::Unretained(this), i)));
  }

  favicon_image_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_image_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon_image_->SetVisible(false);

  title_label_ = AddChildView(std::make_unique<views::Label>(
      u"", views::Label::CustomFont(GetLabelFont())));
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetEnabledColor(nala::kColorTextPrimary);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetMaximumWidthSingleLine(520);

  domain_label_ = AddChildView(std::make_unique<views::Label>(
      u"", views::Label::CustomFont(GetLabelFont())));
  domain_label_->SetDirectionalityMode(
      gfx::DirectionalityMode::DIRECTIONALITY_AS_URL);
  domain_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  domain_label_->SetEnabledColor(nala::kColorTextTertiary);
  domain_label_->SetAutoColorReadabilityEnabled(false);
}

FocusModeTitleBarView::~FocusModeTitleBarView() = default;

void FocusModeTitleBarView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  const gfx::Size size = lights_->GetPreferredSize();
  lights_->SetBounds(kLightsLeftInset, (height() - size.height()) / 2,
                     size.width(), size.height());
}

void FocusModeTitleBarView::AddedToWidget() {
  if (views::Widget* widget = GetWidget(); widget && !widget_observation_.IsObserving()) {
    widget_observation_.Observe(widget);
  }
}

void FocusModeTitleBarView::RemovedFromWidget() {
  widget_observation_.Reset();
}

void FocusModeTitleBarView::OnWidgetActivationChanged(views::Widget* widget,
                                                      bool active) {
  lights_->SchedulePaint();
}

void FocusModeTitleBarView::OnTrafficLight(int which) {
  views::Widget* widget = GetWidget();
  if (!widget) {
    return;
  }
  switch (which) {
    case 0:
      widget->Close();
      break;
    case 1:
      widget->Minimize();
      break;
    default:
      if (widget->IsMaximized()) {
        widget->Restore();
      } else {
        widget->Maximize();
      }
      break;
  }
}

void FocusModeTitleBarView::SetTab(tabs::TabInterface* tab) {
  tab_ui_updated_subscription_ = {};
  tab_will_detach_subscription_ = {};
  tab_ = tab;

  if (tab_) {
    tab_will_detach_subscription_ =
        tab_->RegisterWillDetach(base::BindRepeating(
            &FocusModeTitleBarView::OnTabWillDetach, base::Unretained(this)));
    if (auto* helper = TabUIHelper::From(tab_)) {
      tab_ui_updated_subscription_ =
          helper->AddTabUIChangeCallback(base::BindRepeating(
              &FocusModeTitleBarView::Update, base::Unretained(this)));
    }
  }

  Update();
}

bool FocusModeTitleBarView::IsFaviconVisibleForTesting() const {
  return favicon_image_->GetVisible();
}

void FocusModeTitleBarView::Update() {
  auto* tab_ui_helper = tab_ ? TabUIHelper::From(tab_) : nullptr;
  if (!tab_ui_helper) {
    favicon_image_->SetImage(ui::ImageModel());
    favicon_image_->SetVisible(false);
    title_label_->SetText(u"");
    domain_label_->SetText(u"");
    return;
  }
  title_label_->SetText(tab_ui_helper->GetTitle());

  GURL domain_url = tab_ui_helper->GetVisibleURL();
  std::u16string domain;
  if (tab_ui_helper->ShouldDisplayURL()) {
    domain = url_formatter::FormatUrl(
        domain_url,
        (url_formatter::kFormatUrlOmitDefaults &
         ~url_formatter::kFormatUrlOmitHTTP) |
            url_formatter::kFormatUrlOmitTrivialSubdomains |
            url_formatter::kFormatUrlOmitHTTPS |
            url_formatter::kFormatUrlTrimAfterHost,
        base::UnescapeRule::SPACES, nullptr, nullptr, nullptr);
    brave_utils::ReplaceChromeToBraveScheme(domain);
  }

  domain_label_->SetText(domain.empty() ? u"" : u"·  " + domain);

  if (ui::ImageModel favicon = tab_ui_helper->GetFavicon();
      !favicon.IsEmpty() && !domain.empty()) {
    bool themify_favicon = tab_ui_helper->ShouldThemifyFavicon();
    if (auto* provider = GetColorProvider(); provider && themify_favicon) {
      SkColor favicon_color = provider->GetColor(kColorBookmarkFavicon);
      if (favicon_color != SK_ColorTRANSPARENT) {
        favicon = ui::ImageModel::FromImageSkia(
            gfx::ImageSkiaOperations::CreateColorMask(
                favicon.Rasterize(provider), favicon_color));
      }
    }
    favicon_image_->SetImage(favicon);
    favicon_image_->SetVisible(true);
  } else {
    favicon_image_->SetImage(ui::ImageModel());
    favicon_image_->SetVisible(false);
  }
}

void FocusModeTitleBarView::OnTabWillDetach(
    tabs::TabInterface* tab,
    tabs::TabInterface::DetachReason reason) {
  SetTab(nullptr);
}

BEGIN_METADATA(FocusModeTitleBarView)
END_METADATA
