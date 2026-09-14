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
#include "brave/browser/falcon/ux/mouse_gesture_tab_helper.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
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
    d.Set("videoPill", p->GetBoolean(falcon::prefs::kDownloadVideoPill));
    d.Set("clipboardMonitor",
          p->GetBoolean(falcon::prefs::kDownloadClipboardMonitor));
    d.Set("engineEnabled", p->GetBoolean(falcon::prefs::kDownloadEngineEnabled));
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
  CreateAndAddWebUIDataSource(web_ui, falcon::kFalconControlHost,
                              kFalconControlGenerated, IDR_FALCON_CONTROL_HTML);
  web_ui->AddMessageHandler(std::make_unique<FalconControlMessageHandler>());
}

FalconControlUI::~FalconControlUI() = default;
