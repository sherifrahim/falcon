// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_MOUSE_GESTURE_TAB_HELPER_H_
#define BRAVE_BROWSER_FALCON_UX_MOUSE_GESTURE_TAB_HELPER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/geometry/point_f.h"

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon {

namespace prefs {
inline constexpr char kMouseGesturesEnabled[] = "falcon.gestures.enabled";
void RegisterGesturePrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// Opera/Vivaldi-style right-button mouse gestures on the page:
//   L back · R forward · U reload · D new tab · DR close tab · DL reopen
//   closed tab · UL previous tab · UR next tab · DU scroll to top (no-op here)
// A right-drag that formed a gesture consumes the mouse-up, so the context
// menu does not open; a plain right-click still does.
class MouseGestureTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MouseGestureTabHelper> {
 public:
  ~MouseGestureTabHelper() override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void WebContentsDestroyed() override;

  // Direction string ("L", "DR", ...) -> IDC command, 0 when unknown.
  static int CommandForGesture(const std::string& gesture);

 private:
  friend class content::WebContentsUserData<MouseGestureTabHelper>;
  explicit MouseGestureTabHelper(content::WebContents* contents);

  bool HandleMouseEvent(const blink::WebMouseEvent& event);
  void Attach(content::RenderWidgetHost* host);
  void Detach();
  void Execute(const std::string& gesture);

  content::RenderWidgetHost::MouseEventCallback callback_;
  raw_ptr<content::RenderWidgetHost> attached_host_ = nullptr;

  bool tracking_ = false;
  bool suppress_context_menu_ = false;
  gfx::PointF origin_;
  std::string gesture_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_MOUSE_GESTURE_TAB_HELPER_H_
