// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_

#include <string_view>

#include "brave/components/constants/falcon_url_constants.h"
#include "brave/components/constants/webui_url_constants.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class FalconDownloaderUI;

class FalconDownloaderUIConfig
    : public content::DefaultWebUIConfig<FalconDownloaderUI> {
 public:
  FalconDownloaderUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kFalconDownloaderHost) {}
};

// falcon://downloader — the download manager. The page drives aria2 directly
// over its local WebSocket JSON-RPC; this controller only hands it the
// endpoint and serves the few things a page cannot do (open folders etc).
class FalconDownloaderUI : public content::WebUIController {
 public:
  explicit FalconDownloaderUI(content::WebUI* web_ui);
  FalconDownloaderUI(const FalconDownloaderUI&) = delete;
  FalconDownloaderUI& operator=(const FalconDownloaderUI&) = delete;
  ~FalconDownloaderUI() override;
};

// Same page in the sidebar side panel (chrome://downloader.top-chrome);
// the bundle reads loadTimeData "panel" and switches to the compact layout.
class FalconDownloaderPanelUI : public TopChromeWebUIController {
 public:
  explicit FalconDownloaderPanelUI(content::WebUI* web_ui);
  FalconDownloaderPanelUI(const FalconDownloaderPanelUI&) = delete;
  FalconDownloaderPanelUI& operator=(const FalconDownloaderPanelUI&) = delete;
  ~FalconDownloaderPanelUI() override;

  static constexpr std::string_view GetWebUIName() {
    return "FalconDownloaderPanel";
  }

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

class FalconDownloaderPanelUIConfig
    : public DefaultTopChromeWebUIConfig<FalconDownloaderPanelUI> {
 public:
  FalconDownloaderPanelUIConfig()
      : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                    falcon::kFalconDownloaderPanelHost) {}

  // TopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_
