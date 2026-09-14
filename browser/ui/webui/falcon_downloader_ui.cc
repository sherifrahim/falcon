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
#include <utility>

#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/browser/falcon/download/download_history.h"
#include "brave/browser/falcon/download/download_scheduler.h"
#include "brave/browser/falcon/download/download_security.h"
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
  base::WeakPtrFactory<FalconDownloaderMessageHandler> weak_factory_{this};

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
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.getScanResults",
        base::BindRepeating(&FalconDownloaderMessageHandler::GetScanResults,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.rescan",
        base::BindRepeating(&FalconDownloaderMessageHandler::Rescan,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.openInSandbox",
        base::BindRepeating(&FalconDownloaderMessageHandler::OpenInSandbox,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.getHistory",
        base::BindRepeating(&FalconDownloaderMessageHandler::GetHistory,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.removeHistory",
        base::BindRepeating(&FalconDownloaderMessageHandler::RemoveHistory,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.clearHistory",
        base::BindRepeating(&FalconDownloaderMessageHandler::ClearHistory,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_downloader.renameFile",
        base::BindRepeating(&FalconDownloaderMessageHandler::RenameFile,
                            base::Unretained(this)));
  }

  // args: [callbackId] -> {entries: [...], stats: {...}}
  void GetHistory(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    auto* history = falcon::Aria2Service::Get()->history();
    base::DictValue result;
    result.Set("entries", history->Entries());
    result.Set("stats", history->Stats());
    ResolveJavascriptCallback(args[0], result);
  }

  // args: [gid]
  void RemoveHistory(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    falcon::Aria2Service::Get()->history()->Remove(args[0].GetString());
  }

  void ClearHistory(const base::ListValue& args) {
    falcon::Aria2Service::Get()->history()->Clear();
  }

  // args: [gid, path, newName] - renames a finished file in place. The aria2
  // entry is dropped (its path would be stale); the history entry is updated.
  void RenameFile(const base::ListValue& args) {
    CHECK_EQ(3U, args.size());
    const std::string gid = args[0].GetString();
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(args[1].GetString());
    const base::FilePath new_name =
        base::FilePath::FromUTF8Unsafe(args[2].GetString());
    if (path.empty() || path.ReferencesParent() || new_name.empty() ||
        new_name != new_name.BaseName() || new_name.ReferencesParent()) {
      return;
    }
    const base::FilePath target = path.DirName().Append(new_name);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](const base::FilePath& from, const base::FilePath& to) {
              return !base::PathExists(to) && base::Move(from, to);
            },
            path, target),
        base::BindOnce(&FalconDownloaderMessageHandler::OnRenamed,
                       weak_factory_.GetWeakPtr(), gid, target));
  }

  void OnRenamed(const std::string& gid, base::FilePath target, bool ok) {
    if (!ok) {
      return;
    }
    auto* service = falcon::Aria2Service::Get();
    base::ListValue rm;
    rm.Append(gid);
    service->Call("aria2.removeDownloadResult", std::move(rm),
                  base::DoNothing());
    service->history()->UpdatePath(gid, target.BaseName().AsUTF8Unsafe(),
                                   target.AsUTF8Unsafe());
    service->tracker()->Poke();
  }

  // args: [callbackId]
  void GetScanResults(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(
        args[0], falcon::Aria2Service::Get()->security()->ResultsAsDict());
  }

  // args: [gid]
  void Rescan(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    falcon::Aria2Service::Get()->security()->Rescan(args[0].GetString());
  }

  // args: [path]
  void OpenInSandbox(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(args[0].GetString());
    if (path.empty() || path.ReferencesParent()) {
      return;
    }
    falcon::DownloadSecurity::OpenInSandbox(
        path, LocalState()->GetBoolean(falcon::prefs::kSecuritySandboxNetworking));
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
    d.Set("vtApiKey", ls->GetString(falcon::prefs::kSecurityVtApiKey));
    d.Set("vtEnabled", ls->GetBoolean(falcon::prefs::kSecurityVtEnabled));
    d.Set("quarantineFlagged",
          ls->GetBoolean(falcon::prefs::kSecurityQuarantineFlagged));
    d.Set("sandboxNetworking",
          ls->GetBoolean(falcon::prefs::kSecuritySandboxNetworking));
    d.Set("clipboardMonitor",
          p->GetBoolean(falcon::prefs::kDownloadClipboardMonitor));
    d.Set("categoryRules", p->GetString(falcon::prefs::kDownloadCategoryRules));
    d.Set("proxy", ls->GetString(falcon::prefs::kEngineProxy));
    d.Set("duplicateAction",
          ls->GetString(falcon::prefs::kEngineDuplicateAction));
    d.Set("scheduleEnabled", ls->GetBoolean(falcon::prefs::kScheduleEnabled));
    d.Set("scheduleStart", ls->GetString(falcon::prefs::kScheduleStart));
    d.Set("scheduleStop", ls->GetString(falcon::prefs::kScheduleStop));
    d.Set("historyKeepDays", ls->GetInteger(falcon::prefs::kHistoryKeepDays));
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
    if (const std::string* v = in.FindString("vtApiKey")) {
      ls->SetString(falcon::prefs::kSecurityVtApiKey,
                    std::string(base::TrimWhitespaceASCII(*v, base::TRIM_ALL)));
    }
    set_bool("vtEnabled", falcon::prefs::kSecurityVtEnabled, ls);
    set_bool("quarantineFlagged", falcon::prefs::kSecurityQuarantineFlagged, ls);
    set_bool("sandboxNetworking", falcon::prefs::kSecuritySandboxNetworking, ls);
    if (std::optional<double> v = in.FindDouble("seedRatio")) {
      ls->SetDouble(falcon::prefs::kEngineSeedRatio,
                    std::clamp(*v, 0.0, 100.0));
    }
    set_bool("clipboardMonitor", falcon::prefs::kDownloadClipboardMonitor, p);
    if (const std::string* v = in.FindString("categoryRules")) {
      p->SetString(falcon::prefs::kDownloadCategoryRules, v->substr(0, 8192));
    }
    if (const std::string* v = in.FindString("proxy")) {
      ls->SetString(falcon::prefs::kEngineProxy,
                    std::string(base::TrimWhitespaceASCII(*v, base::TRIM_ALL)));
    }
    if (const std::string* v = in.FindString("duplicateAction")) {
      if (*v == "rename" || *v == "overwrite") {
        ls->SetString(falcon::prefs::kEngineDuplicateAction, *v);
      }
    }
    set_bool("scheduleEnabled", falcon::prefs::kScheduleEnabled, ls);
    for (const auto& [key, pref] :
         {std::pair{"scheduleStart", falcon::prefs::kScheduleStart},
          std::pair{"scheduleStop", falcon::prefs::kScheduleStop}}) {
      if (const std::string* v = in.FindString(key)) {
        if (falcon::DownloadScheduler::ParseTime(*v).has_value()) {
          ls->SetString(pref, *v);
        }
      }
    }
    set_int("historyKeepDays", falcon::prefs::kHistoryKeepDays, ls, 0, 3650);
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
  source->AddBoolean("sandboxAvailable",
                     falcon::DownloadSecurity::IsSandboxAvailable());
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
