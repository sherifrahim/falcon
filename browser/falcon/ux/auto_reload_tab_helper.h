// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_AUTO_RELOAD_TAB_HELPER_H_
#define BRAVE_BROWSER_FALCON_UX_AUTO_RELOAD_TAB_HELPER_H_

#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace falcon {

// Vivaldi-style periodic reload of a tab (context menu > "Reload every…").
// Session-only; the timer dies with the tab.
class AutoReloadTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<AutoReloadTabHelper> {
 public:
  ~AutoReloadTabHelper() override;

  // Zero stops.
  void SetInterval(base::TimeDelta interval);
  base::TimeDelta interval() const { return interval_; }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<AutoReloadTabHelper>;
  explicit AutoReloadTabHelper(content::WebContents* contents);

  void Tick();

  base::TimeDelta interval_;
  base::RepeatingTimer timer_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_AUTO_RELOAD_TAB_HELPER_H_
