// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_downloader_ui.h"

#include <memory>
#include <string>

#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/values.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/falcon_downloader_ui/resources/grit/falcon_downloader_generated_map.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/grit/brave_components_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace {

class FalconDownloaderMessageHandler : public content::WebUIMessageHandler {
 public:
  FalconDownloaderMessageHandler() = default;
  ~FalconDownloaderMessageHandler() override = default;

 private:
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.openFolder",
        base::BindRepeating(&FalconDownloaderMessageHandler::OpenFolder,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.openFile",
        base::BindRepeating(&FalconDownloaderMessageHandler::OpenFile,
                            base::Unretained(this)));
  }

  Profile* profile() { return Profile::FromWebUI(web_ui()); }

  // args: [path]
  void OpenFolder(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const base::FilePath path = base::FilePath::FromUTF8Unsafe(args[0].GetString());
    if (!path.empty()) {
      platform_util::ShowItemInFolder(profile(), path);
    }
  }

  // args: [path]
  void OpenFile(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const base::FilePath path = base::FilePath::FromUTF8Unsafe(args[0].GetString());
    if (!path.empty()) {
      platform_util::OpenItem(profile(), path, platform_util::OPEN_FILE,
                              platform_util::OpenOperationCallback());
    }
  }
};

}  // namespace

FalconDownloaderUI::FalconDownloaderUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  auto* aria2 = falcon::Aria2Service::Get();
  aria2->EnsureRunning();

  content::WebUIDataSource* source = CreateAndAddWebUIDataSource(
      web_ui, kFalconDownloaderHost, kFalconDownloaderGenerated,
      IDR_FALCON_DOWNLOADER_HTML);

  source->AddString("rpcUrl", aria2->rpc_ws_url());
  source->AddString("rpcSecret", aria2->rpc_secret());
  Profile* profile = Profile::FromWebUI(web_ui);
  std::string download_dir;
  if (auto* prefs = DownloadPrefs::FromBrowserContext(profile)) {
    download_dir = prefs->DownloadPath().AsUTF8Unsafe();
  }
  source->AddString("downloadDir", download_dir);

  // The page talks to aria2 on the loopback WebSocket.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' ws://127.0.0.1:* http://127.0.0.1:*;");

  web_ui->AddMessageHandler(
      std::make_unique<FalconDownloaderMessageHandler>());
}

FalconDownloaderUI::~FalconDownloaderUI() = default;
