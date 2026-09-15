// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/boost_tab_helper.h"

#include <string>
#include <string_view>

#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace falcon {

namespace prefs {

void RegisterBoostPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kBoosts);
}

}  // namespace prefs

bool BoostMatches(const std::string& host, const GURL& url) {
  if (host.empty() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (host == "*") {
    return true;
  }
  const std::string_view page = url.host();
  std::string want = base::ToLowerASCII(host);
  if (base::StartsWith(want, "www.")) {
    want = want.substr(4);
  }
  if (page == want) {
    return true;
  }
  return base::EndsWith(page, base::StrCat({".", want}));
}

BoostTabHelper::BoostTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<BoostTabHelper>(*contents) {}

BoostTabHelper::~BoostTabHelper() = default;

void BoostTabHelper::DOMContentLoaded(
    content::RenderFrameHost* render_frame_host) {
  if (render_frame_host != web_contents()->GetPrimaryMainFrame() ||
      !render_frame_host->IsRenderFrameLive()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile) {
    return;
  }
  const GURL& url = web_contents()->GetLastCommittedURL();
  const base::ListValue& boosts = profile->GetPrefs()->GetList(prefs::kBoosts);
  std::string css;
  std::string js;
  for (const base::Value& v : boosts) {
    const base::DictValue* d = v.GetIfDict();
    if (!d || !d->FindBool("enabled").value_or(true)) {
      continue;
    }
    const std::string* host = d->FindString("host");
    if (!host || !BoostMatches(*host, url)) {
      continue;
    }
    if (const std::string* c = d->FindString("css"); c && !c->empty()) {
      css += *c;
      css += "\n";
    }
    if (const std::string* j = d->FindString("js"); j && !j->empty()) {
      js += "try{\n";
      js += *j;
      js += "\n}catch(e){console.warn('Falcon boost error',e)}\n";
    }
  }
  if (css.empty() && js.empty()) {
    return;
  }
  std::string script = "(function(){\n";
  if (!css.empty()) {
    std::string css_json;
    base::JSONWriter::Write(base::Value(css), &css_json);
    script += base::StrCat(
        {"var s=document.getElementById('falcon-boost-css');"
         "if(!s){s=document.createElement('style');s.id='falcon-boost-css';"
         "(document.head||document.documentElement).appendChild(s);}"
         "s.textContent=",
         css_json, ";\n"});
  }
  script += js;
  script += "})();";
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), base::NullCallback(),
      ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(BoostTabHelper);

}  // namespace falcon
