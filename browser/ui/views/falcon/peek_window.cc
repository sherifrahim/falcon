// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/views/falcon/peek_window.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace falcon {

namespace prefs {

void RegisterPeekPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kPeekEnabled, true);
}

}  // namespace prefs

namespace {

constexpr SkColor kSky400 = SkColorSetRGB(0x38, 0xBD, 0xF8);
constexpr SkColor kSlate800 = SkColorSetRGB(0x1E, 0x29, 0x3B);
constexpr SkColor kSlate100 = SkColorSetRGB(0xF1, 0xF5, 0xF9);
constexpr SkColor kSlate950 = SkColorSetRGB(0x0B, 0x12, 0x20);

std::unique_ptr<views::LabelButton> MakeHeaderButton(
    views::Button::PressedCallback callback,
    std::u16string_view text,
    bool primary) {
  auto button =
      std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetEnabledTextColors(primary ? kSlate950 : kSlate100);
  button->SetBackground(views::CreateRoundedRectBackground(
      primary ? kSky400 : kSlate800, 8.0f));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return button;
}

// Root view of the peek: vertical box (header + WebView) that owns the Esc
// accelerator and forwards it to the window.
class PeekRootView : public views::View {
 public:
  explicit PeekRootView(base::RepeatingClosure on_escape)
      : on_escape_(std::move(on_escape)) {
    SetBackground(views::CreateSolidBackground(ui::kColorDialogBackground));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  }
  ~PeekRootView() override = default;

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (accelerator.key_code() == ui::VKEY_ESCAPE) {
      on_escape_.Run();
      return true;
    }
    return false;
  }

 private:
  base::RepeatingClosure on_escape_;
};

}  // namespace

bool MaybePeek(BrowserWindowInterface* browser,
               content::WebContents* source,
               const content::OpenURLParams& params) {
  if (!browser || !source ||
      params.disposition != WindowOpenDisposition::NEW_WINDOW ||
      !params.user_gesture || !params.url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return false;
  }
  Profile* profile = browser->GetProfile();
  if (!profile || !profile->GetPrefs()->GetBoolean(prefs::kPeekEnabled)) {
    return false;
  }
  // Only for links clicked in a real tab (not popups / app windows).
  if (!tabs::TabInterface::MaybeGetFromContents(source)) {
    return false;
  }
  PeekWindow::Show(browser, params.url);
  return true;
}

// static
void PeekWindow::Show(BrowserWindowInterface* browser, const GURL& url) {
  ui::BaseWindow* window = browser->GetWindow();
  if (!window) {
    return;
  }
  const gfx::Rect host = window->GetBounds();
  const int w = std::max(480, host.width() * 82 / 100);
  const int h = std::max(360, host.height() * 84 / 100);
  gfx::Rect bounds(host.x() + (host.width() - w) / 2,
                   host.y() + (host.height() - h) / 2, w, h);

  // Owns itself; freed with the widget in WidgetIsZombie().
  auto* peek = new PeekWindow(browser, url);

  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.delegate = peek;
  params.parent = window->GetNativeWindow();
  params.bounds = bounds;
  params.opacity = views::Widget::InitParams::WindowOpacity::kOpaque;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.name = "FalconPeek";
  auto* widget = new views::Widget();
  widget->Init(std::move(params));
  widget->Show();
}

PeekWindow::PeekWindow(BrowserWindowInterface* browser, const GURL& url)
    : browser_(browser) {
  SetCanResize(true);
  SetModalType(ui::mojom::ModalType::kNone);

  root_ = std::make_unique<PeekRootView>(
      base::BindRepeating(&PeekWindow::Close, base::Unretained(this)));

  auto* header = root_->AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  header->SetInsideBorderInsets(gfx::Insets::VH(6, 10));
  header->SetBetweenChildSpacing(8);
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* peek_label = header->AddChildView(std::make_unique<views::Label>(
      u"Peek", views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
  peek_label->SetEnabledColor(kSky400);
  url_label_ = header->AddChildView(std::make_unique<views::Label>(
      base::UTF8ToUTF16(url.spec()), views::style::CONTEXT_LABEL,
      views::style::STYLE_SECONDARY));
  url_label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  url_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header->SetFlexForView(url_label_, 1);
  // Plain LabelButtons with explicit colours: Brave's MdTextButton resolves
  // Nala colour ids that come out transparent in this standalone widget.
  auto* open_button = header->AddChildView(MakeHeaderButton(
      base::BindRepeating(&PeekWindow::OpenInTab, base::Unretained(this)),
      u"Open in tab", /*primary=*/true));
  open_button->SetTooltipText(u"Move this page to a real tab");
  auto* close = header->AddChildView(MakeHeaderButton(
      base::BindRepeating(&PeekWindow::Close, base::Unretained(this)),
      u"Esc", /*primary=*/false));
  close->SetTooltipText(u"Close (Esc)");

  Profile* profile = browser->GetProfile();
  contents_ = content::WebContents::Create(
      content::WebContents::CreateParams(profile));
  contents_->SetDelegate(this);
  web_view_ = root_->AddChildView(std::make_unique<views::WebView>(profile));
  web_view_->SetWebContents(contents_.get());
  static_cast<views::BoxLayout*>(root_->GetLayoutManager())
      ->SetFlexForView(web_view_, 1);

  content::NavigationController::LoadURLParams load(url);
  load.transition_type = ui::PAGE_TRANSITION_LINK;
  contents_->GetController().LoadURLWithParams(load);
}

PeekWindow::~PeekWindow() {
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
    web_view_ = nullptr;
  }
  url_label_ = nullptr;
  root_.reset();
  if (contents_) {
    contents_->SetDelegate(nullptr);
  }
}

views::View* PeekWindow::GetContentsView() {
  return root_.get();
}

void PeekWindow::WidgetIsZombie(views::Widget* widget) {
  // Delete ourselves first so root_ (and the WebView) leave the RootView
  // before the widget tears down; mirrors BraveOriginStartupView.
  delete this;
  delete widget;
}

void PeekWindow::Close() {
  if (views::Widget* widget = GetWidget()) {
    widget->Close();
  }
}

void PeekWindow::OpenInTab() {
  if (!contents_) {
    return;
  }
  const GURL url = contents_->GetLastCommittedURL().is_valid()
                       ? contents_->GetLastCommittedURL()
                       : contents_->GetVisibleURL();
  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_LINK, false);
  if (tabs::TabInterface* tab = browser_->GetActiveTabInterface()) {
    tab->GetContents()->OpenURL(params, /*navigation_handle_callback=*/{});
  }
  Close();
}

content::WebContents* PeekWindow::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  // Same-window navigations stay in the peek; anything asking for a new
  // tab/window goes to the real browser.
  if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    content::NavigationController::LoadURLParams load(params);
    source->GetController().LoadURLWithParams(load);
    return source;
  }
  if (tabs::TabInterface* tab = browser_->GetActiveTabInterface()) {
    content::OpenURLParams p = params;
    p.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    return tab->GetContents()->OpenURL(p, std::move(navigation_handle_callback));
  }
  return nullptr;
}

bool PeekWindow::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // Esc anywhere in the page closes the peek, even when the event has no
  // native os_event (synthesized input) and skips the focus manager.
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE) {
    Close();
    return true;
  }
  views::Widget* widget = GetWidget();
  if (!widget) {
    return false;
  }
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, widget->GetFocusManager());
}

void PeekWindow::NavigationStateChanged(content::WebContents* source,
                                        content::InvalidateTypes changed_flags) {
  if (url_label_ && source) {
    url_label_->SetText(base::UTF8ToUTF16(source->GetVisibleURL().spec()));
  }
}

void PeekWindow::CloseContents(content::WebContents* source) {
  Close();
}

bool PeekWindow::HandleContextMenu(content::RenderFrameHost& render_frame_host,
                                   const content::ContextMenuParams& params) {
  // No context menu inside the peek (keeps it simple and avoids the
  // RenderViewContextMenu needing a Browser).
  return true;
}

}  // namespace falcon
