// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_COLLECTIONS_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_COLLECTIONS_UI_H_

#include <string_view>

#include "brave/components/constants/falcon_url_constants.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class FalconCollectionsUI;

class FalconCollectionsUIConfig
    : public content::DefaultWebUIConfig<FalconCollectionsUI> {
 public:
  FalconCollectionsUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           falcon::kFalconCollectionsHost) {}
};

// falcon://collections — Collections as a full tab page.
class FalconCollectionsUI : public content::WebUIController {
 public:
  explicit FalconCollectionsUI(content::WebUI* web_ui);
  FalconCollectionsUI(const FalconCollectionsUI&) = delete;
  FalconCollectionsUI& operator=(const FalconCollectionsUI&) = delete;
  ~FalconCollectionsUI() override;
};

// Same bundle in the side panel (chrome://collections.top-chrome); the page
// reads loadTimeData "panel" for the narrow layout.
class FalconCollectionsPanelUI : public TopChromeWebUIController {
 public:
  explicit FalconCollectionsPanelUI(content::WebUI* web_ui);
  FalconCollectionsPanelUI(const FalconCollectionsPanelUI&) = delete;
  FalconCollectionsPanelUI& operator=(const FalconCollectionsPanelUI&) = delete;
  ~FalconCollectionsPanelUI() override;

  static constexpr std::string_view GetWebUIName() {
    return "FalconCollectionsPanel";
  }

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

class FalconCollectionsPanelUIConfig
    : public DefaultTopChromeWebUIConfig<FalconCollectionsPanelUI> {
 public:
  FalconCollectionsPanelUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                    falcon::kFalconCollectionsPanelHost) {}

  // TopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_COLLECTIONS_UI_H_
