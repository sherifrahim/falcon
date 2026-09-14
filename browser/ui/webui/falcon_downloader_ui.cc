// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_downloader_ui.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/values.h"
#include "base/files/file_util.h"
#include "base/task/thread_pool.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/browser/falcon/download/download_tracker.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
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
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.deleteFile",
        base::BindRepeating(&FalconDownloaderMessageHandler::DeleteFile,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.poke",
        base::BindRepeating(&FalconDownloaderMessageHandler::Poke,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.getSettings",
        base::BindRepeating(&FalconDownloaderMessageHandler::GetSettings,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.setSettings",
        base::BindRepeating(&FalconDownloaderMessageHandler::SetSettings,
                            base::Unretained(this)));
  }

  static PrefService* LocalState() { return g_browser_process->local_state(); }

  base::DictValue SettingsDict() {
    PrefService* p = profile()->GetPrefs();
    PrefService* ls = LocalState();
    base::DictValue d;
    d.Set("engineEnabled", p->GetBoolean(falcon::prefs::kDownloadEngineEnabled));
    d.Set("minInterceptKb",
          static_cast<int>(p->GetInt64(falcon::prefs::kDownloadMinInterceptBytes) /
                           1024));
    d.Set("categoriesEnabled",
          p->GetBoolean(falcon::prefs::kDownloadCategoriesEnabled));
    d.Set("magnetEnabled", p->GetBoolean(falcon::prefs::kDownloadMagnetEnabled));
    d.Set("notificationsEnabled",
          p->GetBoolean(falcon::prefs::kDownloadNotificationsEnabled));
    d.Set("showToolbarButton",
          p->GetBoolean(falcon::prefs::kShowDownloadsToolbarButton));
    d.Set("maxConnections", ls->GetInteger(falcon::prefs::kEngineMaxConnections));
    d.Set("maxConcurrent", ls->GetInteger(falcon::prefs::kEngineMaxConcurrent));
    d.Set("speedLimitKbps", ls->GetInteger(falcon::prefs::kEngineSpeedLimitKbps));
    d.Set("seedRatio", ls->GetDouble(falcon::prefs::kEngineSeedRatio));
    d.Set("seedTimeMinutes",
          ls->GetInteger(falcon::prefs::kEngineSeedTimeMinutes));
    return d;
  }

  // args: [callbackId]
  void GetSettings(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], SettingsDict());
  }

  // args: [{key: value, ...}] - any subset of the keys from SettingsDict().
  void SetSettings(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    if (!args[0].is_dict()) {
      return;
    }
    const base::DictValue& in = args[0].GetDict();
    PrefService* p = profile()->GetPrefs();
    PrefService* ls = LocalState();
    auto set_bool = [&](std::string_view key, const char* pref, PrefService* s) {
      if (std::optional<bool> v = in.FindBool(key)) s->SetBoolean(pref, *v);
    };
    auto set_int = [&](std::string_view key, const char* pref, PrefService* s,
                       int lo, int hi) {
      if (std::optional<int> v = in.FindInt(key)) {
        s->SetInteger(pref, std::clamp(*v, lo, hi));
      }
    };
    set_bool("engineEnabled", falcon::prefs::kDownloadEngineEnabled, p);
    set_bool("categoriesEnabled", falcon::prefs::kDownloadCategoriesEnabled, p);
    set_bool("magnetEnabled", falcon::prefs::kDownloadMagnetEnabled, p);
    set_bool("notificationsEnabled",
             falcon::prefs::kDownloadNotificationsEnabled, p);
    set_bool("showToolbarButton", falcon::prefs::kShowDownloadsToolbarButton, p);
    if (std::optional<int> v = in.FindInt("minInterceptKb")) {
      p->SetInt64(falcon::prefs::kDownloadMinInterceptBytes,
                  static_cast<int64_t>(std::clamp(*v, 0, 1 << 20)) * 1024);
    }
    set_int("maxConnections", falcon::prefs::kEngineMaxConnections, ls, 1, 32);
    set_int("maxConcurrent", falcon::prefs::kEngineMaxConcurrent, ls, 1, 20);
    set_int("speedLimitKbps", falcon::prefs::kEngineSpeedLimitKbps, ls, 0,
            1 << 24);
    set_int("seedTimeMinutes", falcon::prefs::kEngineSeedTimeMinutes, ls, 0,
            1 << 20);
    if (std::optional<double> v = in.FindDouble("seedRatio")) {
      ls->SetDouble(falcon::prefs::kEngineSeedRatio,
                    std::clamp(*v, 0.0, 100.0));
    }
    // Engine prefs are pushed to aria2 by Aria2Service's pref observer.
  }

  // args: [path] - deletes the downloaded file (and aria2's .aria2 control
  // file next to it) after the entry was removed from the engine.
  void DeleteFile(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(args[0].GetString());
    if (path.empty() || path.ReferencesParent()) {
      return;
    }
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](const base::FilePath& p) {
              if (!base::DirectoryExists(p)) {
                base::DeleteFile(p);
              }
              base::DeleteFile(p.AddExtensionASCII("aria2"));
            },
            path));
  }

  // args: [] - the page changed something; refresh the tracker soon.
  void Poke(const base::ListValue& args) {
    falcon::Aria2Service::Get()->tracker()->Poke();
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
  source->AddBoolean(
      "categoriesEnabled",
      profile->GetPrefs()->GetBoolean(falcon::prefs::kDownloadCategoriesEnabled));

  // The page talks to aria2 on the loopback WebSocket.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' ws://127.0.0.1:* http://127.0.0.1:*;");

  web_ui->AddMessageHandler(
      std::make_unique<FalconDownloaderMessageHandler>());
}

FalconDownloaderUI::~FalconDownloaderUI() = default;
