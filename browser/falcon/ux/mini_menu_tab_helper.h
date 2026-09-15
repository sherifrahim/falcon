// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_MINI_MENU_TAB_HELPER_H_
#define BRAVE_BROWSER_FALCON_UX_MINI_MENU_TAB_HELPER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/geometry/point.h"

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon {

namespace prefs {
inline constexpr char kMiniMenuEnabled[] = "falcon.minimenu.enabled";
void RegisterMiniMenuPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// Edge-style "mini menu": after the user selects text with the mouse, a tiny
// two-button pill (Copy · Search) appears next to the cursor. Any click,
// navigation or tab switch dismisses it.
class MiniMenuTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MiniMenuTabHelper> {
 public:
  ~MiniMenuTabHelper() override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<MiniMenuTabHelper>;
  explicit MiniMenuTabHelper(content::WebContents* contents);

  bool HandleMouseEvent(const blink::WebMouseEvent& event);
  void Attach(content::RenderWidgetHost* host);
  void Detach();
  void CheckSelection(gfx::Point screen_point);
  void CloseMenu();

  content::RenderWidgetHost::MouseEventCallback callback_;
  raw_ptr<content::RenderWidgetHost> attached_host_ = nullptr;
  bool dragging_ = false;
  gfx::Point press_position_;
  base::WeakPtrFactory<MiniMenuTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_MINI_MENU_TAB_HELPER_H_
