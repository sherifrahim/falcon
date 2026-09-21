// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/ux/command_chain_runner.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "brave/app/brave_command_ids.h"
#include "brave/browser/falcon/falcon_command_ids.h"
#include "brave/browser/ui/commander/commander_service.h"
#include "brave/browser/ui/commander/commander_service_factory.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace falcon {

namespace prefs {

void RegisterChainPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kCommandChains);
}

}  // namespace prefs

namespace {

constexpr int kMaxWaitMs = 60000;
constexpr size_t kMaxSteps = 40;

constexpr CommandChainRunner::KnownCommand kKnown[] = {
    {"new-tab", u"New tab", IDC_NEW_TAB},
    {"close-tab", u"Close tab", IDC_CLOSE_TAB},
    {"restore-tab", u"Reopen closed tab", IDC_RESTORE_TAB},
    {"duplicate-tab", u"Duplicate tab", IDC_DUPLICATE_TAB},
    {"pin-tab", u"Pin / unpin tab", IDC_PIN_TARGET_TAB},
    {"mute-tab", u"Mute / unmute site", IDC_MUTE_TARGET_SITE},
    {"next-tab", u"Next tab", IDC_SELECT_NEXT_TAB},
    {"prev-tab", u"Previous tab", IDC_SELECT_PREVIOUS_TAB},
    {"tab-to-window", u"Move tab to new window", IDC_MOVE_TAB_TO_NEW_WINDOW},
    {"split-view", u"Split view", IDC_NEW_SPLIT_TAB},
    {"new-window", u"New window", IDC_NEW_WINDOW},
    {"private-window", u"New private window", IDC_NEW_INCOGNITO_WINDOW},
    {"close-window", u"Close window", IDC_CLOSE_WINDOW},
    {"reload", u"Reload", IDC_RELOAD},
    {"hard-reload", u"Reload (bypass cache)", IDC_RELOAD_BYPASSING_CACHE},
    {"back", u"Back", IDC_BACK},
    {"forward", u"Forward", IDC_FORWARD},
    {"home", u"Home", IDC_HOME},
    {"focus-address", u"Focus address bar", IDC_FOCUS_LOCATION},
    {"bookmark", u"Bookmark this tab", IDC_BOOKMARK_THIS_TAB},
    {"copy-url", u"Copy page URL", IDC_COPY_URL},
    {"zoom-in", u"Zoom in", IDC_ZOOM_PLUS},
    {"zoom-out", u"Zoom out", IDC_ZOOM_MINUS},
    {"zoom-reset", u"Reset zoom", IDC_ZOOM_NORMAL},
    {"fullscreen", u"Toggle fullscreen", IDC_FULLSCREEN},
    {"find", u"Find in page", IDC_FIND},
    {"print", u"Print", IDC_PRINT},
    {"save-page", u"Save page", IDC_SAVE_PAGE},
    {"reader", u"Reader mode (Speedreader)", IDC_DISTILL_PAGE},
    {"view-source", u"View source", IDC_VIEW_SOURCE},
    {"devtools", u"Developer tools", IDC_DEV_TOOLS},
    {"tab-search", u"Tab search", IDC_TAB_SEARCH},
    {"history", u"History", IDC_SHOW_HISTORY},
    {"bookmarks-bar", u"Toggle bookmarks bar", IDC_SHOW_BOOKMARK_BAR},
    {"bookmarks-panel", u"Bookmarks side panel", IDC_SHOW_BOOKMARK_SIDE_PANEL},
    {"reading-mode", u"Reading mode side panel",
     IDC_SHOW_READING_MODE_SIDE_PANEL},
    {"settings", u"Settings", IDC_OPTIONS},
    {"clear-data", u"Clear browsing data", IDC_CLEAR_BROWSING_DATA},
    {"task-manager", u"Task manager", IDC_TASK_MANAGER},
    {"sidebar", u"Toggle sidebar", IDC_TOGGLE_SIDEBAR},
    {"cockpit", u"Cockpit mode (hide toolbar)", IDC_TOGGLE_FOCUS_MODE},
    {"downloads", u"Falcon Downloads", IDC_FALCON_SHOW_DOWNLOADS},
    {"collections", u"Collections panel", IDC_FALCON_SHOW_COLLECTIONS},
    {"control", u"Falcon control panel", IDC_FALCON_SHOW_CONTROL},
    {"quick-commands", u"Quick commands", IDC_COMMANDER},
    {"vertical-tabs", u"Toggle vertical tabs", 0},
};

const CommandChainRunner::KnownCommand* FindKnown(const std::string& key) {
  for (const auto& k : kKnown) {
    if (key == k.key) {
      return &k;
    }
  }
  return nullptr;
}

CommandChainStep StepFromDict(const base::DictValue& d) {
  CommandChainStep step;
  if (const std::string* t = d.FindString("type")) {
    step.type = *t;
  }
  if (const std::string* v = d.FindString("value")) {
    step.value = *v;
  } else if (std::optional<int> n = d.FindInt("value")) {
    step.value = base::NumberToString(*n);
  }
  return step;
}

bool ValidStep(const CommandChainStep& step) {
  if (step.type == "url" || step.type == "goto") {
    const GURL url = url_formatter::FixupURL(step.value, std::string());
    return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
  }
  if (step.type == "command") {
    return FindKnown(step.value) != nullptr;
  }
  if (step.type == "deck") {
    return !step.value.empty();
  }
  if (step.type == "wait") {
    int ms = 0;
    return base::StringToInt(step.value, &ms) && ms >= 0 && ms <= kMaxWaitMs;
  }
  return false;
}

// Runs one step; returns the delay before the next one.
base::TimeDelta Execute(BrowserWindowInterface* browser,
                        const CommandChainStep& step) {
  if (step.type == "url" || step.type == "goto") {
    const GURL url = url_formatter::FixupURL(step.value, std::string());
    if (step.type == "goto") {
      if (content::WebContents* contents =
              browser->GetTabStripModel()->GetActiveWebContents()) {
        contents->GetController().LoadURL(url, content::Referrer(),
                                          ui::PAGE_TRANSITION_TYPED,
                                          std::string());
      }
    } else {
      NavigateParams params(browser->GetProfile(), url,
                            ui::PAGE_TRANSITION_TYPED);
      params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
      params.browser = browser;
      Navigate(&params);
    }
    return base::Milliseconds(150);
  }
  if (step.type == "command") {
    const auto* known = FindKnown(step.value);
    if (!known) {
      return base::TimeDelta();
    }
    if (known->command_id == 0) {
      if (step.value == "vertical-tabs") {
        PrefService* prefs = browser->GetProfile()->GetPrefs();
        prefs->SetBoolean(brave_tabs::kVerticalTabsEnabled,
                          !prefs->GetBoolean(brave_tabs::kVerticalTabsEnabled));
      }
    } else if (chrome::IsCommandEnabled(browser, known->command_id)) {
      chrome::ExecuteCommand(browser, known->command_id);
    }
    return base::Milliseconds(120);
  }
  if (step.type == "deck") {
    auto* commander = commander::CommanderServiceFactory::GetForBrowserContext(
        browser->GetProfile());
    if (commander) {
      commander->ForceUpdateText(base::UTF8ToUTF16(step.value));
      if (!commander->GetItems().empty()) {
        commander->SelectCommand(0, commander->GetResultSetId());
      }
    }
    return base::Milliseconds(120);
  }
  if (step.type == "wait") {
    int ms = 0;
    base::StringToInt(step.value, &ms);
    return base::Milliseconds(std::clamp(ms, 0, kMaxWaitMs));
  }
  return base::TimeDelta();
}

void RunFrom(base::WeakPtr<BrowserWindowInterface> browser,
             std::vector<CommandChainStep> steps,
             size_t index) {
  if (!browser || index >= steps.size()) {
    return;
  }
  const base::TimeDelta delay = Execute(browser.get(), steps[index]);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&RunFrom, browser, std::move(steps), index + 1), delay);
}

}  // namespace

CommandChain::CommandChain() = default;
CommandChain::CommandChain(const CommandChain&) = default;
CommandChain::~CommandChain() = default;

// static
base::span<const CommandChainRunner::KnownCommand>
CommandChainRunner::KnownCommands() {
  return kKnown;
}

// static
std::vector<CommandChain> CommandChainRunner::Chains(Profile* profile) {
  std::vector<CommandChain> chains;
  for (const base::Value& v : profile->GetPrefs()->GetList(prefs::kCommandChains)) {
    const base::DictValue* d = v.GetIfDict();
    const std::string* id = d ? d->FindString("id") : nullptr;
    const std::string* name = d ? d->FindString("name") : nullptr;
    if (!id || !name) {
      continue;
    }
    CommandChain chain;
    chain.id = *id;
    chain.name = *name;
    if (const base::ListValue* steps = d->FindList("steps")) {
      for (const base::Value& sv : *steps) {
        if (const base::DictValue* sd = sv.GetIfDict()) {
          chain.steps.push_back(StepFromDict(*sd));
        }
      }
    }
    chains.push_back(std::move(chain));
  }
  return chains;
}

// static
void CommandChainRunner::Run(BrowserWindowInterface* browser,
                             const std::string& chain_id) {
  for (const CommandChain& chain : Chains(browser->GetProfile())) {
    if (chain.id == chain_id) {
      RunSteps(browser, chain.steps);
      return;
    }
  }
}

// static
void CommandChainRunner::RunSteps(BrowserWindowInterface* browser,
                                  std::vector<CommandChainStep> steps) {
  std::erase_if(steps, [](const CommandChainStep& s) { return !ValidStep(s); });
  if (steps.size() > kMaxSteps) {
    steps.resize(kMaxSteps);
  }
  RunFrom(browser->GetWeakPtr(), std::move(steps), 0);
}

// static
void CommandChainRunner::SetChains(Profile* profile,
                                   const base::ListValue& chains) {
  base::ListValue clean;
  for (const base::Value& v : chains) {
    const base::DictValue* d = v.GetIfDict();
    const std::string* name = d ? d->FindString("name") : nullptr;
    if (!name || name->empty()) {
      continue;
    }
    base::DictValue out;
    const std::string* id = d->FindString("id");
    out.Set("id", id && !id->empty()
                      ? *id
                      : base::Uuid::GenerateRandomV4().AsLowercaseString());
    out.Set("name", name->substr(0, 80));
    base::ListValue steps;
    if (const base::ListValue* in = d->FindList("steps")) {
      for (const base::Value& sv : *in) {
        const base::DictValue* sd = sv.GetIfDict();
        if (!sd) {
          continue;
        }
        CommandChainStep step = StepFromDict(*sd);
        if (!ValidStep(step) || steps.size() >= kMaxSteps) {
          continue;
        }
        base::DictValue so;
        so.Set("type", step.type);
        so.Set("value", step.value);
        steps.Append(std::move(so));
      }
    }
    out.Set("steps", std::move(steps));
    clean.Append(std::move(out));
  }
  profile->GetPrefs()->SetList(prefs::kCommandChains, std::move(clean));
}

}  // namespace falcon
