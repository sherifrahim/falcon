// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_COMMAND_DECK_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_COMMAND_DECK_UI_H_

#include <string_view>

#include "brave/components/constants/falcon_url_constants.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/common/url_constants.h"

// Command deck (mock v2, screen 5): a WebUI bubble over the window driven by
// Brave's CommanderService — the same commands as the omnibox ":>" mode, in a
// large grouped overlay. Shown by BraveBrowserView::ToggleCommandDeck().
class FalconCommandDeckUI : public TopChromeWebUIController {
 public:
  explicit FalconCommandDeckUI(content::WebUI* web_ui);
  FalconCommandDeckUI(const FalconCommandDeckUI&) = delete;
  FalconCommandDeckUI& operator=(const FalconCommandDeckUI&) = delete;
  ~FalconCommandDeckUI() override;

  static constexpr std::string_view GetWebUIName() {
    return "FalconCommandDeck";
  }

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

class FalconCommandDeckUIConfig
    : public DefaultTopChromeWebUIConfig<FalconCommandDeckUI> {
 public:
  FalconCommandDeckUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                    falcon::kFalconCommandDeckHost) {}

  // TopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_COMMAND_DECK_UI_H_
