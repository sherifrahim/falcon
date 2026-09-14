// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/mouse_gesture_tab_helper.h"

#include <cmath>
#include <cstdlib>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"

namespace falcon {

namespace prefs {

void RegisterGesturePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kMouseGesturesEnabled, true);
}

}  // namespace prefs

namespace {

// Minimum travel before a stroke counts, in DIPs.
constexpr float kStrokeThreshold = 24.f;
constexpr size_t kMaxStrokes = 3;

}  // namespace

// static
int MouseGestureTabHelper::CommandForGesture(const std::string& g) {
  if (g == "L") return IDC_BACK;
  if (g == "R") return IDC_FORWARD;
  if (g == "U") return IDC_RELOAD;
  if (g == "D") return IDC_NEW_TAB;
  if (g == "DR") return IDC_CLOSE_TAB;
  if (g == "DL") return IDC_RESTORE_TAB;
  if (g == "UL") return IDC_SELECT_PREVIOUS_TAB;
  if (g == "UR") return IDC_SELECT_NEXT_TAB;
  if (g == "RL") return IDC_RELOAD_BYPASSING_CACHE;
  return 0;
}

MouseGestureTabHelper::MouseGestureTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<MouseGestureTabHelper>(*contents) {
  callback_ = base::BindRepeating(&MouseGestureTabHelper::HandleMouseEvent,
                                  base::Unretained(this));
  if (content::RenderFrameHost* rfh = contents->GetPrimaryMainFrame()) {
    Attach(rfh->GetRenderWidgetHost());
  }
}

MouseGestureTabHelper::~MouseGestureTabHelper() {
  Detach();
}

void MouseGestureTabHelper::Attach(content::RenderWidgetHost* host) {
  Detach();
  if (host) {
    host->AddMouseEventCallback(callback_);
    attached_host_ = host;
  }
}

void MouseGestureTabHelper::Detach() {
  if (attached_host_) {
    attached_host_->RemoveMouseEventCallback(callback_);
    attached_host_ = nullptr;
  }
}

void MouseGestureTabHelper::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  if (new_host != web_contents()->GetPrimaryMainFrame()) {
    return;
  }
  Attach(new_host->GetRenderWidgetHost());
}

void MouseGestureTabHelper::WebContentsDestroyed() {
  Detach();
}

bool MouseGestureTabHelper::HandleMouseEvent(
    const blink::WebMouseEvent& event) {
  using Type = blink::WebInputEvent::Type;
  const Type type = event.GetType();

  if (type == Type::kContextMenu) {
    if (suppress_context_menu_) {
      suppress_context_menu_ = false;
      return true;
    }
    return false;
  }

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile ||
      !profile->GetPrefs()->GetBoolean(prefs::kMouseGesturesEnabled)) {
    tracking_ = false;
    return false;
  }

  const bool right = event.button == blink::WebPointerProperties::Button::kRight;
  const bool right_held =
      event.GetModifiers() & blink::WebInputEvent::kRightButtonDown;

  if (type == Type::kMouseDown && right) {
    tracking_ = true;
    origin_ = event.PositionInWidget();
    gesture_.clear();
    return false;  // let the page see the press (selection etc.)
  }

  if (type == Type::kMouseMove && tracking_ && right_held) {
    const gfx::PointF p = event.PositionInWidget();
    const float dx = p.x() - origin_.x();
    const float dy = p.y() - origin_.y();
    if (std::abs(dx) >= kStrokeThreshold || std::abs(dy) >= kStrokeThreshold) {
      const char dir = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 'R' : 'L')
                                                   : (dy > 0 ? 'D' : 'U');
      if (gesture_.empty() || gesture_.back() != dir) {
        if (gesture_.size() < kMaxStrokes) gesture_.push_back(dir);
      }
      origin_ = p;
    }
    // Once a stroke exists, keep the page from turning the drag into a
    // selection/drag-and-drop.
    return !gesture_.empty();
  }

  if (type == Type::kMouseUp && right && tracking_) {
    tracking_ = false;
    if (gesture_.empty()) {
      return false;  // plain right click -> context menu as usual
    }
    suppress_context_menu_ = true;
    Execute(gesture_);
    gesture_.clear();
    return true;
  }

  if (type == Type::kMouseDown && !right) {
    tracking_ = false;
  }
  return false;
}

void MouseGestureTabHelper::Execute(const std::string& gesture) {
  const int command = CommandForGesture(gesture);
  VLOG(1) << "Falcon: gesture " << gesture << " -> " << command;
  if (!command) {
    return;
  }
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(web_contents());
  BrowserWindowInterface* browser = tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (browser && chrome::IsCommandEnabled(browser, command)) {
    chrome::ExecuteCommand(browser, command);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MouseGestureTabHelper);

}  // namespace falcon
