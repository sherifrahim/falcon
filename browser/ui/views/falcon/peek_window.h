// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_FALCON_PEEK_WINDOW_H_
#define BRAVE_BROWSER_UI_VIEWS_FALCON_PEEK_WINDOW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

class BrowserWindowInterface;

namespace content {
struct OpenURLParams;
}  // namespace content

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace views {
class Label;
class View;
class WebView;
}  // namespace views

namespace falcon {

namespace prefs {
inline constexpr char kPeekEnabled[] = "falcon.peek.enabled";
void RegisterPeekPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// Arc-style "Peek": Shift+click on a link opens it in a floating preview over
// the current tab instead of a new window. Esc closes; "Open in tab" promotes
// it. Hooked from BrowserWebContentsDelegate::OpenURLFromTab.
bool MaybePeek(BrowserWindowInterface* browser,
               content::WebContents* source,
               const content::OpenURLParams& params);

// Plain WidgetDelegate (WidgetDelegateView's ctor is friend-gated upstream).
// CLIENT_OWNS_WIDGET; `this` and the widget are freed in WidgetIsZombie().
class PeekWindow : public views::WidgetDelegate,
                   public content::WebContentsDelegate {
 public:
  static void Show(BrowserWindowInterface* browser, const GURL& url);

  PeekWindow(const PeekWindow&) = delete;
  PeekWindow& operator=(const PeekWindow&) = delete;

  void Close();

 private:
  PeekWindow(BrowserWindowInterface* browser, const GURL& url);
  ~PeekWindow() override;

  void OpenInTab();

  // views::WidgetDelegate:
  views::View* GetContentsView() override;
  void WidgetIsZombie(views::Widget* widget) override;

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override;
  void CloseContents(content::WebContents* source) override;
  bool HandleContextMenu(content::RenderFrameHost& render_frame_host,
                         const content::ContextMenuParams& params) override;

  raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<views::View> root_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::Label> url_label_ = nullptr;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_UI_VIEWS_FALCON_PEEK_WINDOW_H_
