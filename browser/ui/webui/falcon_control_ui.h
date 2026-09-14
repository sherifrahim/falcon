// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_CONTROL_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_CONTROL_UI_H_

#include "brave/components/constants/falcon_url_constants.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class FalconControlUI;

class FalconControlUIConfig
    : public content::DefaultWebUIConfig<FalconControlUI> {
 public:
  FalconControlUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           falcon::kFalconControlHost) {}
};

// falcon://falcon — Falcon's own settings: look, behaviour, engines, about.
class FalconControlUI : public content::WebUIController {
 public:
  explicit FalconControlUI(content::WebUI* web_ui);
  FalconControlUI(const FalconControlUI&) = delete;
  FalconControlUI& operator=(const FalconControlUI&) = delete;
  ~FalconControlUI() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_CONTROL_UI_H_
