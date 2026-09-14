// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_

#include "brave/components/constants/webui_url_constants.h"
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

#endif  // BRAVE_BROWSER_UI_WEBUI_FALCON_DOWNLOADER_UI_H_
