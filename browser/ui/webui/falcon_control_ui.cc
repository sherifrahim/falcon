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
#include "brave/browser/falcon/ux/boost_tab_helper.h"
#include "brave/browser/falcon/ux/mini_menu_tab_helper.h"
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
#include "brave/components/falcon_control_ui/resources/grit/falcon_control_generated_map.h"
#include "brave/components/sidebar/browser/pref_names.h"
#include "brave/components/version_info/version_info.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "components/grit/brave_components_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
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

class FalconControlMessageHandler : public content::WebUIMessageHandler {
 public:
  FalconControlMessageHandler() = default;
  ~FalconControlMessageHandler() override = default;

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
        "falcon_control.updateYtDlp",
        base::BindRepeating(&FalconControlMessageHandler::UpdateYtDlp,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_control.setBoosts",
        base::BindRepeating(&FalconControlMessageHandler::SetBoosts,
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
  }

  // args: [list of {id, host, name, css, js, enabled}] — replaces the list.
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
  void GetSessions(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], SessionList());
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
    d.Set("videoPill", p->GetBoolean(falcon::prefs::kDownloadVideoPill));
    d.Set("clipboardMonitor",
          p->GetBoolean(falcon::prefs::kDownloadClipboardMonitor));
    d.Set("engineEnabled", p->GetBoolean(falcon::prefs::kDownloadEngineEnabled));
    d.Set("boosts", p->GetList(falcon::prefs::kBoosts).Clone());
    d.Set("sessionsAvailable",
          !!WorkspaceServiceFactory::GetForProfile(profile()));
    d.Set("chromiumVersion", std::string(version_info::GetVersionNumber()));
    d.Set("braveVersion", version_info::GetBraveVersionNumberForDisplay());
    d.Set("ytDlpVersion", yt_dlp_version_);
    d.Set("aria2Version", "1.37.0");
    return d;
  }

  // args: [callbackId]
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
  web_ui->AddMessageHandler(std::make_unique<FalconControlMessageHandler>());
}

FalconControlUI::~FalconControlUI() = default;
