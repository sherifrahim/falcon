// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/falcon/mini_menu_bubble.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr SkColor kSky400 = SkColorSetRGB(0x38, 0xBD, 0xF8);
constexpr SkColor kSlate800 = SkColorSetRGB(0x1E, 0x29, 0x3B);
constexpr SkColor kSlate100 = SkColorSetRGB(0xF1, 0xF5, 0xF9);
constexpr SkColor kSlate950 = SkColorSetRGB(0x0B, 0x12, 0x20);

// One live bubble per WebContents (closed by the tab helper, or replaced).
// CLIENT_OWNS_WIDGET: the delegate must outlive the widget, so both live here.
struct Entry {
  std::unique_ptr<views::BubbleDialogModelHost> host;
  std::unique_ptr<views::Widget> widget;
};

std::map<content::WebContents*, Entry>& Bubbles() {
  static base::NoDestructor<std::map<content::WebContents*, Entry>> bubbles;
  return *bubbles;
}

void OnBubbleClosed(content::WebContents* web_contents,
                    views::Widget::ClosedReason reason) {
  auto it = Bubbles().find(web_contents);
  if (it == Bubbles().end()) {
    return;
  }
  Entry entry = std::move(it->second);
  Bubbles().erase(it);
  // Free after the close finishes (we may be inside the widget's close path).
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, entry.widget.release());
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, entry.host.release());
}

std::unique_ptr<views::LabelButton> MakeButton(
    views::Button::PressedCallback callback,
    std::u16string_view text,
    bool primary) {
  auto button =
      std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetEnabledTextColors(primary ? kSlate950 : kSlate100);
  button->SetBackground(views::CreateRoundedRectBackground(
      primary ? kSky400 : kSlate800, 8.0f));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  return button;
}

void CopyText(content::WebContents* web_contents, std::u16string text) {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(text);
  CloseMiniMenu(web_contents);
}

void SearchText(content::WebContents* web_contents, std::u16string text) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  GURL url;
  if (auto* service = TemplateURLServiceFactory::GetForProfile(profile)) {
    url = service->GenerateSearchURLForDefaultSearchProvider(text);
  }
  if (!url.is_valid()) {
    url = GURL(base::StrCat({"https://www.google.com/search?q=",
                             base::EscapeQueryParamValue(
                                 base::UTF16ToUTF8(text), true)}));
  }
  NavigateParams params(profile, url, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  if (auto* tab = tabs::TabInterface::MaybeGetFromContents(web_contents)) {
    params.browser = tab->GetBrowserWindowInterface();
  }
  CloseMiniMenu(web_contents);
  Navigate(&params);
}

std::u16string SearchLabel(content::WebContents* web_contents) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (auto* service = TemplateURLServiceFactory::GetForProfile(profile)) {
    if (const TemplateURL* dse = service->GetDefaultSearchProvider()) {
      return base::StrCat({u"Search ", dse->short_name()});
    }
  }
  return u"Search";
}

}  // namespace

void ShowMiniMenu(content::WebContents* web_contents,
                  const gfx::Point& screen_point,
                  const std::u16string& text) {
  CloseMiniMenu(web_contents);

  auto row = std::make_unique<views::BoxLayoutView>();
  row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  row->SetBetweenChildSpacing(6);
  row->AddChildView(MakeButton(
      base::BindRepeating(&CopyText, base::Unretained(web_contents), text),
      u"Copy", /*primary=*/false));
  row->AddChildView(MakeButton(
      base::BindRepeating(&SearchText, base::Unretained(web_contents), text),
      SearchLabel(web_contents), /*primary=*/true));

  auto model =
      ui::DialogModel::Builder()
          .OverrideShowCloseButton(false)
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  std::move(row),
                  views::BubbleDialogModelHost::FieldType::kControl))
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(model), views::BubbleAnchor(), views::BubbleBorder::TOP_LEFT,
      /*autosize=*/true, /*owned_by_widget=*/false);
  // Sit just below-right of the cursor, never steal focus from the page.
  bubble->SetAnchorRect(gfx::Rect(screen_point + gfx::Vector2d(8, 12),
                                  gfx::Size(1, 1)));
  bubble->set_parent_window(web_contents->GetNativeView());
  bubble->SetCanActivate(false);
  bubble->set_close_on_deactivate(false);
  bubble->set_margins(gfx::Insets(6));
  bubble->set_frame_margins(views::DialogDelegate::FrameMarginsParams{
      .contents = gfx::Insets(0)});

  Entry entry;
  entry.widget = views::BubbleDialogDelegate::CreateBubble(
      bubble.get(),
      base::BindOnce(&OnBubbleClosed, base::Unretained(web_contents)));
  entry.host = std::move(bubble);
  entry.widget->ShowInactive();
  Bubbles()[web_contents] = std::move(entry);
}

void CloseMiniMenu(content::WebContents* web_contents) {
  auto it = Bubbles().find(web_contents);
  if (it == Bubbles().end()) {
    return;
  }
  // Close() runs OnBubbleClosed synchronously, which frees the entry.
  it->second.widget->Close();
}

}  // namespace falcon
