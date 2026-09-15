// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/mini_menu_tab_helper.h"

#include <cstdlib>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "brave/browser/ui/views/falcon/mini_menu_bubble.h"
#include "chrome/browser/profiles/profile.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"

namespace falcon {

namespace prefs {

void RegisterMiniMenuPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kMiniMenuEnabled, true);
}

}  // namespace prefs

namespace {

constexpr int kDragThreshold = 4;
constexpr size_t kMaxSelectionLength = 4000;

}  // namespace

MiniMenuTabHelper::MiniMenuTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<MiniMenuTabHelper>(*contents) {
  callback_ = base::BindRepeating(&MiniMenuTabHelper::HandleMouseEvent,
                                  base::Unretained(this));
  if (content::RenderFrameHost* rfh = contents->GetPrimaryMainFrame()) {
    Attach(rfh->GetRenderWidgetHost());
  }
}

MiniMenuTabHelper::~MiniMenuTabHelper() {
  Detach();
}

void MiniMenuTabHelper::Attach(content::RenderWidgetHost* host) {
  Detach();
  if (host) {
    host->AddMouseEventCallback(callback_);
    attached_host_ = host;
  }
}

void MiniMenuTabHelper::Detach() {
  if (attached_host_) {
    attached_host_->RemoveMouseEventCallback(callback_);
    attached_host_ = nullptr;
  }
}

void MiniMenuTabHelper::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  if (new_host != web_contents()->GetPrimaryMainFrame()) {
    return;
  }
  Attach(new_host->GetRenderWidgetHost());
}

void MiniMenuTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    CloseMenu();
  }
}

void MiniMenuTabHelper::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE) {
    CloseMenu();
  }
}

void MiniMenuTabHelper::WebContentsDestroyed() {
  CloseMenu();
  Detach();
}

bool MiniMenuTabHelper::HandleMouseEvent(const blink::WebMouseEvent& event) {
  using Type = blink::WebInputEvent::Type;
  const Type type = event.GetType();
  const bool left = event.button == blink::WebPointerProperties::Button::kLeft;

  if (type == Type::kMouseDown) {
    CloseMenu();
    dragging_ = false;
    if (left) {
      press_position_ = gfx::ToRoundedPoint(event.PositionInWidget());
    }
    return false;
  }
  if (type == Type::kMouseMove && left &&
      (event.GetModifiers() & blink::WebInputEvent::kLeftButtonDown)) {
    const gfx::Point p = gfx::ToRoundedPoint(event.PositionInWidget());
    if (std::abs(p.x() - press_position_.x()) > kDragThreshold ||
        std::abs(p.y() - press_position_.y()) > kDragThreshold) {
      dragging_ = true;
    }
    return false;
  }
  if (type != Type::kMouseUp || !left) {
    return false;
  }
  // A double-click (word select) has clickCount 2 and no drag; both count.
  if (!dragging_ && event.click_count < 2) {
    return false;
  }
  dragging_ = false;
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || !profile->GetPrefs()->GetBoolean(prefs::kMiniMenuEnabled)) {
    return false;
  }
  content::RenderWidgetHostView* view = web_contents()->GetRenderWidgetHostView();
  if (!view) {
    return false;
  }
  gfx::Point screen_point = gfx::ToRoundedPoint(event.PositionInWidget());
  screen_point += view->GetViewBounds().OffsetFromOrigin();
  // The renderer updates the selection slightly after mouse-up.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&MiniMenuTabHelper::CheckSelection,
                     weak_factory_.GetWeakPtr(), screen_point),
      base::Milliseconds(140));
  return false;
}

void MiniMenuTabHelper::CheckSelection(gfx::Point screen_point) {
  content::RenderWidgetHostView* view = web_contents()->GetRenderWidgetHostView();
  if (!view) {
    return;
  }
  std::u16string text = view->GetSelectedText();
  base::TrimWhitespace(text, base::TRIM_ALL, &text);
  if (text.empty() || text.size() > kMaxSelectionLength) {
    return;
  }
  ShowMiniMenu(web_contents(), screen_point, text);
}

void MiniMenuTabHelper::CloseMenu() {
  CloseMiniMenu(web_contents());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MiniMenuTabHelper);

}  // namespace falcon
