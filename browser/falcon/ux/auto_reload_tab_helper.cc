// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/auto_reload_tab_helper.h"

#include "base/functional/bind.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace falcon {

AutoReloadTabHelper::AutoReloadTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<AutoReloadTabHelper>(*contents) {}

AutoReloadTabHelper::~AutoReloadTabHelper() = default;

void AutoReloadTabHelper::SetInterval(base::TimeDelta interval) {
  interval_ = interval;
  timer_.Stop();
  if (interval_.is_positive()) {
    timer_.Start(FROM_HERE, interval_,
                 base::BindRepeating(&AutoReloadTabHelper::Tick,
                                     base::Unretained(this)));
  }
}

void AutoReloadTabHelper::Tick() {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    timer_.Stop();
    return;
  }
  // Skip while the user is mid-load; the next tick will catch it.
  if (web_contents()->IsLoading()) {
    return;
  }
  web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/false);
}

void AutoReloadTabHelper::WebContentsDestroyed() {
  timer_.Stop();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AutoReloadTabHelper);

}  // namespace falcon
