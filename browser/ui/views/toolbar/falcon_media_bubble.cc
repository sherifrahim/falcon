// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/toolbar/falcon_media_bubble.h"

#include <string>
#include <utility>

#include "base/byte_size.h"
#include "base/functional/bind.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/download/download_interceptor.h"
#include "brave/browser/falcon/media/media_service.h"
#include "brave/browser/falcon/media/media_sniffer_tab_helper.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/color/color_id.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr int kBubbleWidth = 420;
constexpr int kRowSpacing = 6;
constexpr int kOuterPadding = 16;
constexpr size_t kMaxRows = 8;

using Kind = MediaSnifferTabHelper::Kind;

std::u16string KindLabel(const MediaSnifferTabHelper::Candidate& c) {
  std::u16string label;
  switch (c.kind) {
    case Kind::kFile:
      label = c.size > 0 ? ui::FormatBytes(base::ByteSize(
                               static_cast<uint64_t>(c.size)))
                         : std::u16string(u"media file");
      if (!c.mime_type.empty()) {
        label = base::StrCat({base::UTF8ToUTF16(c.mime_type), u" · ", label});
      }
      break;
    case Kind::kPlaylist:
      label = u"stream (HLS/DASH) · via yt-dlp";
      break;
    case Kind::kPage:
      label = u"page player · via yt-dlp";
      break;
  }
  return label;
}

void StartCandidate(BrowserWindowInterface* browser,
                    MediaSnifferTabHelper::Candidate candidate,
                    GURL page_url) {
  Profile* profile = browser->GetProfile();
  if (!profile) {
    return;
  }
  if (candidate.kind == Kind::kFile) {
    StartEngineDownload(profile, candidate.url, page_url);
  } else {
    MediaService::Get()->Start(profile, candidate.url, page_url, "best");
  }
}

void OpenPicker(BrowserWindowInterface* browser, GURL url, GURL page_url) {
  ShowSingletonTab(
      browser,
      GURL(base::StrCat({"chrome://", kFalconDownloaderHost, "#media=",
                         base::EscapeQueryParamValue(url.spec(), false),
                         "&referer=",
                         base::EscapeQueryParamValue(page_url.spec(), false)})));
}

}  // namespace

std::unique_ptr<views::BubbleDialogModelHost> CreateFalconMediaBubble(
    BrowserWindowInterface* browser,
    content::WebContents* web_contents,
    views::View* anchor) {
  auto* helper = MediaSnifferTabHelper::FromWebContents(web_contents);
  const GURL page_url = web_contents->GetLastCommittedURL();

  auto container = std::make_unique<views::BoxLayoutView>();
  container->SetOrientation(views::BoxLayout::Orientation::kVertical);
  container->SetBetweenChildSpacing(kRowSpacing);

  size_t shown = 0;
  if (helper) {
    for (const auto& candidate : helper->candidates()) {
      if (shown++ >= kMaxRows) break;
      auto* row = container->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
      row->SetBetweenChildSpacing(8);
      row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);

      auto* text = row->AddChildView(std::make_unique<views::BoxLayoutView>());
      text->SetOrientation(views::BoxLayout::Orientation::kVertical);
      auto* name = text->AddChildView(std::make_unique<views::Label>(
          base::UTF8ToUTF16(candidate.name), views::style::CONTEXT_LABEL,
          views::style::STYLE_PRIMARY));
      name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      name->SetElideBehavior(gfx::ELIDE_MIDDLE);
      auto* sub = text->AddChildView(std::make_unique<views::Label>(
          KindLabel(candidate), views::style::CONTEXT_LABEL,
          views::style::STYLE_SECONDARY));
      sub->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      sub->SetElideBehavior(gfx::ELIDE_TAIL);
      row->SetFlexForView(text, 1);

      auto* download = row->AddChildView(std::make_unique<views::MdTextButton>(
          views::Button::PressedCallback(), u"Download"));
      download->SetStyle(ui::ButtonStyle::kProminent);
      // The callback needs the button itself; bind after creation.
      download->SetCallback(base::BindRepeating(
          [](BrowserWindowInterface* browser,
             MediaSnifferTabHelper::Candidate candidate, GURL page_url,
             views::Button* btn) {
            btn->SetCallback(views::Button::PressedCallback());
            btn->GetWidget()->CloseWithReason(
                views::Widget::ClosedReason::kUnspecified);
            StartCandidate(browser, std::move(candidate), std::move(page_url));
          },
          base::Unretained(browser), candidate, page_url,
          base::Unretained(download)));

      if (candidate.kind != Kind::kFile) {
        auto* quality = row->AddChildView(std::make_unique<views::MdTextButton>(
            views::Button::PressedCallback(), u"Quality…"));
        quality->SetCallback(base::BindRepeating(
            [](BrowserWindowInterface* browser, GURL url, GURL page_url,
               views::Button* btn) {
              btn->SetCallback(views::Button::PressedCallback());
              btn->GetWidget()->CloseWithReason(
                  views::Widget::ClosedReason::kUnspecified);
              OpenPicker(browser, std::move(url), std::move(page_url));
            },
            base::Unretained(browser), candidate.url, page_url,
            base::Unretained(quality)));
      }
    }
  }

  if (shown == 0) {
    auto* empty = container->AddChildView(std::make_unique<views::Label>(
        u"No video or audio spotted on this page yet. Start playing something "
        u"and come back.",
        views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    empty->SetMultiLine(true);
    empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  auto* open = container->AddChildView(std::make_unique<views::MdTextButton>(
      views::Button::PressedCallback(), u"Open Falcon Downloads"));
  open->SetCallback(base::BindRepeating(
      [](BrowserWindowInterface* browser, views::Button* btn) {
        btn->SetCallback(views::Button::PressedCallback());
        btn->GetWidget()->CloseWithReason(
            views::Widget::ClosedReason::kUnspecified);
        ShowSingletonTab(browser, GURL(base::StrCat({"chrome://",
                                                     kFalconDownloaderHost})));
      },
      base::Unretained(browser), base::Unretained(open)));

  auto model =
      ui::DialogModel::Builder()
          .SetTitle(u"Media on this page")
          .OverrideShowCloseButton(true)
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  std::move(container),
                  views::BubbleDialogModelHost::FieldType::kControl))
          .Build();

  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(model), anchor, views::BubbleBorder::TOP_RIGHT);
  bubble->set_fixed_width(kBubbleWidth);
  bubble->set_frame_margins(views::DialogDelegate::FrameMarginsParams{
      .contents = gfx::Insets(kOuterPadding)});
  bubble->set_margins(gfx::Insets::VH(kOuterPadding, 0));
  return bubble;
}

}  // namespace falcon
