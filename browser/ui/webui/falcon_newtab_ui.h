// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_NEWTAB_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_NEWTAB_UI_H_

#include "content/public/browser/web_ui_controller.h"

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace falcon::prefs {
// Legacy profile pref: the configuration as a dict, from before it became a
// syncable JSON string (kNtpConfig, //brave/components/constants). Read once
// to migrate, never written.
inline constexpr char kNtpState[] = "falcon.ntp.state";
void RegisterNtpProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
}  // namespace falcon::prefs

// falcon://newtab — Falcon's own new tab page (Bonjourr-class: clock,
// greeting, search, quick links, wallpapers). Replaces Brave's NTP for
// regular profiles; private windows keep Brave's private NTP.
class FalconNewTabUI : public content::WebUIController {
 public:
  explicit FalconNewTabUI(content::WebUI* web_ui);
  FalconNewTabUI(const FalconNewTabUI&) = delete;
  FalconNewTabUI& operator=(const FalconNewTabUI&) = delete;
  ~FalconNewTabUI() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_NEWTAB_UI_H_
