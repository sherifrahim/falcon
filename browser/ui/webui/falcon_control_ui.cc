// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_control_ui.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/check_op.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/version_info/version_info.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/browser/falcon/media/media_service.h"
#include "brave/browser/falcon/sidecars/sidecar_installer.h"
#include "brave/browser/falcon/ux/boost_tab_helper.h"
#include "brave/browser/falcon/ux/command_chain_runner.h"
#include "brave/browser/falcon/ux/mini_menu_tab_helper.h"
#include "brave/browser/falcon/ux/tab_archiver.h"
#include "brave/browser/falcon/vault/bitwarden_service.h"
#include "brave/browser/falcon/ux/mouse_gesture_tab_helper.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/color/falcon_color_mixer.h"
#include "brave/browser/ui/views/falcon/peek_window.h"
#include "chrome/browser/browser_process.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "brave/common/pref_names.h"
#include "brave/components/constants/webui_url_constants.h"
#include "brave/components/brave_sync/brave_sync_prefs.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "brave/components/falcon_control_ui/resources/grit/falcon_control_generated_map.h"
#include "brave/components/sidebar/browser/pref_names.h"
#include "brave/components/constants/falcon_version.h"
#include "brave/components/version_info/version_info.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/sync/base/data_type.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_user_settings.h"
#include "components/sync_device_info/device_info_sync_service.h"
#include "components/sync_device_info/device_info_tracker.h"
#include "chrome/browser/sync/device_info_sync_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "components/grit/brave_components_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace {

// Blocking: runs `yt-dlp.exe <arg>` and returns its output (or a message).
struct ToolResult {
  bool ok = false;
  std::string output;
};

ToolResult RunYtDlp(const std::string& arg, base::TimeDelta timeout) {
  ToolResult r;
  base::CommandLine cmd(falcon::YtDlpPath());
  cmd.AppendArg(arg);
  base::LaunchOptions options;
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
#endif
  int exit_code = -1;
  const bool ran = base::GetAppOutputWithExitCodeAndTimeout(
      cmd.GetCommandLineString(), /*include_stderr=*/true, &r.output,
      &exit_code, timeout, options);
  r.ok = ran && exit_code == 0;
  if (!ran) {
    r.output = "yt-dlp did not finish (missing binary or timeout)";
  }
  r.output = std::string(base::TrimWhitespaceASCII(r.output, base::TRIM_ALL));
  return r;
}

class FalconControlMessageHandler
    : public content::WebUIMessageHandler,
      public password_manager::PasswordStoreConsumer,
      public falcon::SidecarInstaller::Observer {
 public:
  FalconControlMessageHandler() {
    falcon::SidecarInstaller::Get()->AddObserver(this);
  }
  ~FalconControlMessageHandler() override {
    falcon::SidecarInstaller::Get()->RemoveObserver(this);
  }

  // falcon::SidecarInstaller::Observer:
  void OnSidecarsChanged() override {
    if (IsJavascriptAllowed()) {
      FireWebUIListener("falcon-sidecars-changed");
    }
  }

 private:
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "falcon_control.getState",
        base::BindRepeating(&FalconControlMessageHandler::GetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.setState",
        base::BindRepeating(&FalconControlMessageHandler::SetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.getSidecars",
        base::BindRepeating(&FalconControlMessageHandler::GetSidecars,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.installSidecar",
        base::BindRepeating(&FalconControlMessageHandler::InstallSidecar,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.updateYtDlp",
        base::BindRepeating(&FalconControlMessageHandler::UpdateYtDlp,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.setBoosts",
        base::BindRepeating(&FalconControlMessageHandler::SetBoosts,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.getChains",
        base::BindRepeating(&FalconControlMessageHandler::GetChains,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.setChains",
        base::BindRepeating(&FalconControlMessageHandler::SetChains,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.runChain",
        base::BindRepeating(&FalconControlMessageHandler::RunChain,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.getSessions",
        base::BindRepeating(&FalconControlMessageHandler::GetSessions,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.saveSession",
        base::BindRepeating(&FalconControlMessageHandler::SaveSession,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.restoreSession",
        base::BindRepeating(&FalconControlMessageHandler::RestoreSession,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.deleteSession",
        base::BindRepeating(&FalconControlMessageHandler::DeleteSession,
                            base::Unretained(this)));
    // Falcon Sync: status, manual sync, password count.
    web_ui()->RegisterMessageCallback(
        "falcon_control.getSync",
        base::BindRepeating(&FalconControlMessageHandler::GetSync,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.setSyncServer",
        base::BindRepeating(&FalconControlMessageHandler::SetSyncServer,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.syncNow",
        base::BindRepeating(&FalconControlMessageHandler::SyncNow,
                            base::Unretained(this)));
    // Falcon Passwords: Bitwarden vault.
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwStatus",
        base::BindRepeating(&FalconControlMessageHandler::BwStatus,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwConnect",
        base::BindRepeating(&FalconControlMessageHandler::BwConnect,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwUnlock",
        base::BindRepeating(&FalconControlMessageHandler::BwUnlock,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwLock",
        base::BindRepeating(&FalconControlMessageHandler::BwLock,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwSync",
        base::BindRepeating(&FalconControlMessageHandler::BwSync,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.bwDisconnect",
        base::BindRepeating(&FalconControlMessageHandler::BwDisconnect,
                            base::Unretained(this)));
  }

  // args: [list of {id, host, name, css, js, enabled}] — replaces the list.
  base::DictValue ChainsState() {
    base::DictValue out;
    out.Set("chains", Profile::FromWebUI(web_ui())
                          ->GetPrefs()
                          ->GetList(falcon::prefs::kCommandChains)
                          .Clone());
    base::ListValue known;
    for (const auto& k : falcon::CommandChainRunner::KnownCommands()) {
      base::DictValue d;
      d.Set("key", k.key);
      d.Set("title", k.title);
      known.Append(std::move(d));
    }
    out.Set("known", std::move(known));
    return out;
  }

  void GetChains(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], ChainsState());
  }

  // (callbackId, chains) → the validated list as stored.
  void SetChains(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    if (args[1].is_list()) {
      falcon::CommandChainRunner::SetChains(Profile::FromWebUI(web_ui()),
                                            args[1].GetList());
    }
    ResolveJavascriptCallback(args[0], ChainsState().Find("chains")->Clone());
  }

  // Runs an (unsaved) chain from the editor on the last active window.
  void RunChain(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const base::DictValue* chain = args[0].GetIfDict();
    const base::ListValue* steps = chain ? chain->FindList("steps") : nullptr;
    BrowserWindowInterface* browser =
        ProfileBrowserCollection::GetForProfile(Profile::FromWebUI(web_ui()))
            ->GetLastActiveBrowser();
    if (!steps || !browser) {
      return;
    }
    std::vector<falcon::CommandChainStep> list;
    for (const base::Value& v : *steps) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        continue;
      }
      falcon::CommandChainStep step;
      if (const std::string* t = d->FindString("type")) {
        step.type = *t;
      }
      if (const std::string* s = d->FindString("value")) {
        step.value = *s;
      }
      list.push_back(std::move(step));
    }
    falcon::CommandChainRunner::RunSteps(browser, std::move(list));
  }

  void SetBoosts(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    if (!args[0].is_list()) {
      return;
    }
    base::ListValue clean;
    for (const base::Value& v : args[0].GetList()) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        continue;
      }
      const std::string* host = d->FindString("host");
      if (!host || host->empty()) {
        continue;
      }
      base::DictValue out;
      out.Set("id", d->FindString("id") ? *d->FindString("id") : *host);
      out.Set("host", base::ToLowerASCII(
                          base::TrimWhitespaceASCII(*host, base::TRIM_ALL)));
      out.Set("name", d->FindString("name") ? *d->FindString("name") : "");
      out.Set("css", d->FindString("css") ? *d->FindString("css") : "");
      out.Set("js", d->FindString("js") ? *d->FindString("js") : "");
      out.Set("enabled", d->FindBool("enabled").value_or(true));
      clean.Append(std::move(out));
    }
    profile()->GetPrefs()->SetList(falcon::prefs::kBoosts, std::move(clean));
  }

  base::ListValue SessionList() {
    base::ListValue list;
    auto* service = WorkspaceServiceFactory::GetForProfile(profile());
    if (!service) {
      return list;
    }
    for (const WorkspaceMetadata& m : service->ListWorkspaces()) {
      base::DictValue d;
      d.Set("name", m.name);
      d.Set("modified", m.modified_at.InMillisecondsFSinceUnixEpoch());
      d.Set("windows", m.number_of_windows);
      d.Set("tabs", m.number_of_tabs);
      list.Append(std::move(d));
    }
    return list;
  }

  // args: [callbackId] -> [{name, modified, windows, tabs}]
  // Sync state for the control panel: chain, devices, last sync, item counts.
  base::DictValue SyncState() {
    base::DictValue d;
    Profile* profile = Profile::FromWebUI(web_ui());
    syncer::SyncService* sync = SyncServiceFactory::GetForProfile(profile);
    const bool in_chain =
        sync && sync->GetUserSettings()->IsInitialSyncFeatureSetupComplete();
    d.Set("inChain", in_chain);
    d.Set("serverUrl", profile->GetPrefs()->GetString(
                           brave_sync::kCustomSyncServiceUrl));
    if (sync) {
      d.Set("active", sync->IsSyncFeatureActive());
      const base::Time last = sync->GetLastSyncedTimeForDebugging();
      d.Set("lastSynced", last.is_null()
                              ? 0.0
                              : last.InMillisecondsFSinceUnixEpoch());
      base::ListValue types;
      for (syncer::DataType type : sync->GetActiveDataTypes()) {
        types.Append(syncer::DataTypeToDebugString(type));
      }
      d.Set("activeTypes", std::move(types));
      if (auto* device_sync =
              DeviceInfoSyncServiceFactory::GetForProfile(profile)) {
        d.Set("devices",
              static_cast<int>(device_sync->GetDeviceInfoTracker()
                                   ->GetAllDeviceInfo()
                                   .size()));
      }
    }
    d.Set("passwords", password_count_);
    return d;
  }

  // PasswordStoreConsumer: the saved-password count for the Sync card.
  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface* store,
      password_manager::LoginsResultOrError results_or_error) override {
    if (auto* logins =
            std::get_if<password_manager::LoginsResult>(&results_or_error)) {
      password_count_ = static_cast<int>(logins->size());
    }
    if (!password_callback_id_.is_none() && IsJavascriptAllowed()) {
      ResolveJavascriptCallback(password_callback_id_, SyncState());
    }
    password_callback_id_ = base::Value();
  }

  void GetSync(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    scoped_refptr<password_manager::PasswordStoreInterface> store =
        ProfilePasswordStoreFactory::GetForProfile(
            Profile::FromWebUI(web_ui()), ServiceAccessType::EXPLICIT_ACCESS);
    if (!store) {
      ResolveJavascriptCallback(args[0], SyncState());
      return;
    }
    // The count arrives asynchronously; the state resolves with it.
    password_callback_id_ = args[0].Clone();
    store->GetAllLogins(weak_factory_.GetWeakPtr());
  }

  // The sync server is per install: Falcon has no Brave services key, so it
  // talks to a self-hosted brave/go-sync the owner points it at. Takes effect
  // when the sync service is next built, i.e. after a restart.
  void SetSyncServer(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    std::string url = args[1].is_string() ? args[1].GetString() : std::string();
    base::TrimWhitespaceASCII(url, base::TRIM_ALL, &url);
    GURL parsed(url);
    if (!url.empty() && (!parsed.is_valid() || !parsed.SchemeIs(url::kHttpsScheme))) {
      ResolveJavascriptCallback(args[0], base::Value(false));
      return;
    }
    Profile::FromWebUI(web_ui())->GetPrefs()->SetString(
        brave_sync::kCustomSyncServiceUrl, url);
    ResolveJavascriptCallback(args[0], base::Value(true));
  }

  // "Sync now": push local changes and pull remote ones for every active type.
  void SyncNow(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    syncer::SyncService* sync =
        SyncServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()));
    if (sync) {
      sync->TriggerRefresh(
          syncer::SyncService::TriggerRefreshSource::kSyncInternals,
          sync->GetActiveDataTypes());
    }
    ResolveJavascriptCallback(args[0], SyncState());
  }

  void GetSessions(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], SessionList());
  }

  // ---- Bitwarden vault -----------------------------------------------------
  static base::DictValue StatusDict(falcon::BitwardenService::Status st) {
    base::DictValue d;
    d.Set("configured", st.configured);
    d.Set("serving", st.serving);
    d.Set("state", st.state);
    d.Set("email", st.email);
    d.Set("server", st.server);
    d.Set("lastSync", st.last_sync);
    d.Set("error", st.error);
    return d;
  }

  void ResolveStatus(base::Value callback_id,
                     falcon::BitwardenService::Status st) {
    ResolveJavascriptCallback(callback_id, StatusDict(std::move(st)));
  }

  void ResolveResult(base::Value callback_id, bool ok, std::string error) {
    base::DictValue d;
    d.Set("ok", ok);
    d.Set("error", error);
    ResolveJavascriptCallback(callback_id, d);
  }

  // args: [callbackId]
  void BwStatus(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    falcon::BitwardenService::Get()->GetStatus(
        base::BindOnce(&FalconControlMessageHandler::ResolveStatus,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  // args: [callbackId, email, masterPassword, totp, serverUrl]
  void BwConnect(const base::ListValue& args) {
    CHECK_EQ(5U, args.size());
    AllowJavascript();
    auto str = [&](size_t i) {
      return args[i].is_string() ? args[i].GetString() : std::string();
    };
    falcon::BitwardenService::Get()->Connect(
        str(1), str(2), str(3), str(4),
        base::BindOnce(&FalconControlMessageHandler::ResolveResult,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  // args: [callbackId, masterPassword]
  void BwUnlock(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    falcon::BitwardenService::Get()->Unlock(
        args[1].is_string() ? args[1].GetString() : std::string(),
        base::BindOnce(&FalconControlMessageHandler::ResolveResult,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void BwLock(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    falcon::BitwardenService::Get()->Lock(
        base::BindOnce(&FalconControlMessageHandler::ResolveResult,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void BwSync(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    falcon::BitwardenService::Get()->Sync(
        base::BindOnce(&FalconControlMessageHandler::ResolveResult,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void BwDisconnect(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    falcon::BitwardenService::Get()->Disconnect(
        base::BindOnce(&FalconControlMessageHandler::ResolveResult,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  // args: [callbackId, name]
  void SaveSession(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    if (auto* service = WorkspaceServiceFactory::GetForProfile(profile());
        service && args[1].is_string() && !args[1].GetString().empty()) {
      service->SaveWorkspace(args[1].GetString());
    }
    ResolveJavascriptCallback(args[0], SessionList());
  }

  // args: [callbackId, name]
  void RestoreSession(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    if (auto* service = WorkspaceServiceFactory::GetForProfile(profile());
        service && args[1].is_string()) {
      service->RestoreWorkspace(args[1].GetString());
    }
    ResolveJavascriptCallback(args[0], true);
  }

  // args: [callbackId, name]
  void DeleteSession(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    if (auto* service = WorkspaceServiceFactory::GetForProfile(profile());
        service && args[1].is_string()) {
      service->DeleteWorkspace(args[1].GetString());
    }
    ResolveJavascriptCallback(args[0], SessionList());
  }

  Profile* profile() { return Profile::FromWebUI(web_ui()); }

  base::DictValue StateDict() {
    PrefService* p = profile()->GetPrefs();
    base::DictValue d;
    auto* theme = ThemeServiceFactory::GetForProfile(profile());
    d.Set("colorScheme",
          theme ? static_cast<int>(theme->GetBrowserColorScheme()) : 0);
    d.Set("verticalTabs", p->GetBoolean(brave_tabs::kVerticalTabsEnabled));
    d.Set("sidebarShow", p->GetInteger(sidebar::kSidebarShowOption));
    d.Set("roundedCorners", p->GetBoolean(kWebViewRoundedCorners));
    d.Set("mouseGestures", p->GetBoolean(falcon::prefs::kMouseGesturesEnabled));
    d.Set("peek", p->GetBoolean(falcon::prefs::kPeekEnabled));
    d.Set("miniMenu", p->GetBoolean(falcon::prefs::kMiniMenuEnabled));
    d.Set("shellMode", p->GetInteger(falcon::prefs::kShellMode));
    d.Set("blackTheme", g_browser_process->local_state()->GetBoolean(
                            falcon::prefs::kThemeBlack));
    d.Set("verticalTabsCollapsed",
          p->GetBoolean(brave_tabs::kVerticalTabsCollapsed));
    d.Set("dockCards", p->GetBoolean(falcon::prefs::kDockCards));
    d.Set("archiveHours", p->GetInteger(falcon::prefs::kTabArchiveHours));
    d.Set("bwEmail", g_browser_process->local_state()->GetString(
                         falcon::prefs::kBitwardenEmail));
    d.Set("bwSaveNew", g_browser_process->local_state()->GetBoolean(
                           falcon::prefs::kBitwardenSaveNew));
    d.Set("bwRemember", g_browser_process->local_state()->GetBoolean(
                            falcon::prefs::kBitwardenRememberSession));
    d.Set("videoPill", p->GetBoolean(falcon::prefs::kDownloadVideoPill));
    d.Set("clipboardMonitor",
          p->GetBoolean(falcon::prefs::kDownloadClipboardMonitor));
    d.Set("engineEnabled", p->GetBoolean(falcon::prefs::kDownloadEngineEnabled));
    d.Set("boosts", p->GetList(falcon::prefs::kBoosts).Clone());
    d.Set("sessionsAvailable",
          !!WorkspaceServiceFactory::GetForProfile(profile()));
    d.Set("chromiumVersion", std::string(version_info::GetVersionNumber()));
    d.Set("braveVersion", version_info::GetBraveVersionNumberForDisplay());
    d.Set("falconVersion", falcon::kFalconVersion);
    d.Set("falconRepo", falcon::kFalconRepo);
    d.Set("ytDlpVersion", yt_dlp_version_);
    d.Set("aria2Version", "1.37.0");
    return d;
  }

  // args: [callbackId]
  static base::ListValue SidecarList() {
    base::ListValue list;
    for (const auto& st : falcon::SidecarInstaller::Get()->GetStatuses()) {
      base::DictValue d;
      d.Set("name", st.name);
      d.Set("version", st.version);
      d.Set("size", static_cast<double>(st.size));
      d.Set("bundled", st.bundled);
      d.Set("installed", st.installed);
      d.Set("installing", st.installing);
      d.Set("progress", st.progress);
      d.Set("error", st.error);
      list.Append(std::move(d));
    }
    return list;
  }

  void GetSidecars(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], SidecarList());
  }

  void InstallSidecar(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    falcon::SidecarInstaller::Get()->Install(
        args[1].GetString(),
        base::BindOnce(
            [](base::WeakPtr<FalconControlMessageHandler> self,
               base::Value callback_id, bool ok, const std::string& error) {
              if (!self || !self->IsJavascriptAllowed()) {
                return;
              }
              base::DictValue d;
              d.Set("ok", ok);
              d.Set("error", error);
              self->ResolveJavascriptCallback(callback_id, d);
            },
            weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void GetState(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    if (yt_dlp_version_.empty()) {
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE,
          {base::MayBlock(), base::WithBaseSyncPrimitives(),
           base::TaskPriority::USER_VISIBLE},
          base::BindOnce(&RunYtDlp, "--version", base::Seconds(20)),
          base::BindOnce(&FalconControlMessageHandler::OnVersion,
                         weak_factory_.GetWeakPtr(), args[0].Clone()));
      return;
    }
    ResolveJavascriptCallback(args[0], StateDict());
  }

  void OnVersion(base::Value callback_id, ToolResult r) {
    yt_dlp_version_ = r.ok ? r.output : "unavailable";
    if (IsJavascriptAllowed()) {
      ResolveJavascriptCallback(callback_id, StateDict());
    }
  }

  // args: [{key: value}] — any subset of StateDict keys.
  void SetState(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    if (!args[0].is_dict()) {
      return;
    }
    const base::DictValue& in = args[0].GetDict();
    PrefService* p = profile()->GetPrefs();
    if (std::optional<int> v = in.FindInt("colorScheme")) {
      if (auto* theme = ThemeServiceFactory::GetForProfile(profile())) {
        theme->SetBrowserColorScheme(
            static_cast<ThemeService::BrowserColorScheme>(
                std::clamp(*v, 0, 2)));
      }
    }
    if (std::optional<bool> v = in.FindBool("verticalTabs")) {
      p->SetBoolean(brave_tabs::kVerticalTabsEnabled, *v);
    }
    if (std::optional<int> v = in.FindInt("sidebarShow")) {
      if (*v == 0 || *v == 1 || *v == 3) {
        p->SetInteger(sidebar::kSidebarShowOption, *v);
      }
    }
    if (std::optional<bool> v = in.FindBool("roundedCorners")) {
      p->SetBoolean(kWebViewRoundedCorners, *v);
    }
    if (std::optional<bool> v = in.FindBool("mouseGestures")) {
      p->SetBoolean(falcon::prefs::kMouseGesturesEnabled, *v);
    }
    if (std::optional<bool> v = in.FindBool("peek")) {
      p->SetBoolean(falcon::prefs::kPeekEnabled, *v);
    }
    if (std::optional<bool> v = in.FindBool("miniMenu")) {
      p->SetBoolean(falcon::prefs::kMiniMenuEnabled, *v);
    }
    if (std::optional<int> v = in.FindInt("shellMode")) {
      p->SetInteger(falcon::prefs::kShellMode, std::clamp(*v, 0, 2));
    }
    if (std::optional<bool> v = in.FindBool("blackTheme")) {
      g_browser_process->local_state()->SetBoolean(falcon::prefs::kThemeBlack,
                                                   *v);
      falcon::SetBlackTheme(*v);
    }
    if (std::optional<bool> v = in.FindBool("verticalTabsCollapsed")) {
      p->SetBoolean(brave_tabs::kVerticalTabsCollapsed, *v);
    }
    if (std::optional<bool> v = in.FindBool("dockCards")) {
      p->SetBoolean(falcon::prefs::kDockCards, *v);
    }
    if (std::optional<int> v = in.FindInt("archiveHours")) {
      p->SetInteger(falcon::prefs::kTabArchiveHours, std::clamp(*v, 0, 24 * 30));
    }
    if (std::optional<bool> v = in.FindBool("bwSaveNew")) {
      g_browser_process->local_state()->SetBoolean(
          falcon::prefs::kBitwardenSaveNew, *v);
    }
    if (std::optional<bool> v = in.FindBool("bwRemember")) {
      g_browser_process->local_state()->SetBoolean(
          falcon::prefs::kBitwardenRememberSession, *v);
    }
    if (std::optional<bool> v = in.FindBool("videoPill")) {
      p->SetBoolean(falcon::prefs::kDownloadVideoPill, *v);
    }
    if (std::optional<bool> v = in.FindBool("clipboardMonitor")) {
      p->SetBoolean(falcon::prefs::kDownloadClipboardMonitor, *v);
    }
    if (std::optional<bool> v = in.FindBool("engineEnabled")) {
      p->SetBoolean(falcon::prefs::kDownloadEngineEnabled, *v);
    }
  }

  // args: [callbackId] -> {ok, output, version}. yt-dlp replaces its own
  // exe in the output directory (-U).
  void UpdateYtDlp(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(),
         base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&RunYtDlp, "-U", base::Minutes(3)),
        base::BindOnce(&FalconControlMessageHandler::OnUpdated,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void OnUpdated(base::Value callback_id, ToolResult r) {
    yt_dlp_version_.clear();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(),
         base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&RunYtDlp, "--version", base::Seconds(20)),
        base::BindOnce(&FalconControlMessageHandler::OnUpdatedVersion,
                       weak_factory_.GetWeakPtr(), std::move(callback_id),
                       std::move(r)));
  }

  void OnUpdatedVersion(base::Value callback_id,
                        ToolResult update,
                        ToolResult version) {
    yt_dlp_version_ = version.ok ? version.output : "unavailable";
    base::DictValue d;
    d.Set("ok", update.ok);
    d.Set("output", update.output);
    d.Set("version", yt_dlp_version_);
    if (IsJavascriptAllowed()) {
      ResolveJavascriptCallback(callback_id, d);
    }
  }

  std::string yt_dlp_version_;
  int password_count_ = 0;
  base::Value password_callback_id_;
  base::WeakPtrFactory<FalconControlMessageHandler> weak_factory_{this};
};

}  // namespace

FalconControlUI::FalconControlUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = CreateAndAddWebUIDataSource(
      web_ui, falcon::kFalconControlHost, kFalconControlGenerated,
      IDR_FALCON_CONTROL_HTML);
  source->AddBoolean("blackTheme", g_browser_process->local_state()->GetBoolean(
                                       falcon::prefs::kThemeBlack));
  // About > "Check for updates" asks GitHub Releases directly from the page.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://api.github.com;");
  web_ui->AddMessageHandler(std::make_unique<FalconControlMessageHandler>());
}

FalconControlUI::~FalconControlUI() = default;
