// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_UX_BOOST_TAB_HELPER_H_
#define BRAVE_BROWSER_FALCON_UX_BOOST_TAB_HELPER_H_

#include <string>

#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon {

namespace prefs {
// List of dicts: {id, host, name, css, js, enabled}. `host` is a bare host
// ("reddit.com" also matches its subdomains) or "*" for every site.
inline constexpr char kBoosts[] = "falcon.boosts";
void RegisterBoostPrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace prefs

// True when boost `host` applies to `url`.
bool BoostMatches(const std::string& host, const GURL& url);

// Arc-style "Boosts": per-site CSS and JS the user wrote, injected into the
// main frame (isolated world) once the DOM is ready.
class BoostTabHelper : public content::WebContentsObserver,
                       public content::WebContentsUserData<BoostTabHelper> {
 public:
  ~BoostTabHelper() override;

  // content::WebContentsObserver:
  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;

 private:
  friend class content::WebContentsUserData<BoostTabHelper>;
  explicit BoostTabHelper(content::WebContents* contents);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_UX_BOOST_TAB_HELPER_H_
