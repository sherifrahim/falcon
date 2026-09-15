// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/commander/falcon_command_source.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/ux/mouse_gesture_tab_helper.h"
#include "brave/browser/ui/commander/fuzzy_finder.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/views/falcon/peek_window.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace commander {

namespace {

std::unique_ptr<CommandItem> ItemForTitle(const std::u16string& title,
                                          FuzzyFinder& finder,
                                          std::vector<gfx::Range>& ranges) {
  double score = finder.Find(title, ranges);
  if (score > 0) {
    return std::make_unique<CommandItem>(title, score, ranges);
  }
  return nullptr;
}

void OpenPage(base::WeakPtr<BrowserWindowInterface> browser, const GURL& url) {
  if (!browser) {
    return;
  }
  NavigateParams params(browser->GetProfile(), url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.browser = browser.get();
  Navigate(&params);
}

void TogglePref(base::WeakPtr<BrowserWindowInterface> browser,
                const char* pref) {
  if (!browser) {
    return;
  }
  PrefService* prefs = browser->GetProfile()->GetPrefs();
  prefs->SetBoolean(pref, !prefs->GetBoolean(pref));
}

WorkspaceService* Sessions(BrowserWindowInterface* browser) {
  return browser ? WorkspaceServiceFactory::GetForProfile(browser->GetProfile())
                 : nullptr;
}

// Composite step: "Save session as…" → one item that saves under the typed
// name (or offers overwriting an existing one).
CommandSource::CommandResults SaveSessionItems(
    base::WeakPtr<BrowserWindowInterface> browser,
    const std::u16string& input) {
  CommandSource::CommandResults results;
  auto* service = Sessions(browser.get());
  if (!service) {
    return results;
  }
  const std::u16string name = input.empty() ? u"Session" : input;
  auto item = std::make_unique<CommandItem>(
      base::StrCat({u"Save session \"", name, u"\""}), 1.0,
      std::vector<gfx::Range>());
  item->command = base::BindOnce(
      [](base::WeakPtr<WorkspaceService> service, std::string name) {
        if (service) {
          service->SaveWorkspace(name);
        }
      },
      service->GetWeakPtr(), base::UTF16ToUTF8(name));
  results.push_back(std::move(item));
  return results;
}

// Composite step listing saved sessions; `restore` picks restore vs delete.
CommandSource::CommandResults SessionItems(
    base::WeakPtr<BrowserWindowInterface> browser,
    bool restore,
    const std::u16string& input) {
  CommandSource::CommandResults results;
  auto* service = Sessions(browser.get());
  if (!service) {
    return results;
  }
  FuzzyFinder finder(input);
  std::vector<gfx::Range> ranges;
  for (const WorkspaceMetadata& meta : service->ListWorkspaces()) {
    const std::u16string title = base::UTF8ToUTF16(meta.name);
    double score = input.empty() ? 1.0 : finder.Find(title, ranges);
    if (score <= 0) {
      continue;
    }
    auto item = std::make_unique<CommandItem>(title, score, ranges);
    item->annotation = base::UTF8ToUTF16(base::StrCat(
        {base::NumberToString(meta.number_of_windows), " win · ",
         base::NumberToString(meta.number_of_tabs), " tabs"}));
    item->command = base::BindOnce(
        [](base::WeakPtr<WorkspaceService> service, bool restore,
           std::string name) {
          if (!service) {
            return;
          }
          if (restore) {
            service->RestoreWorkspace(name);
          } else {
            service->DeleteWorkspace(name);
          }
        },
        service->GetWeakPtr(), restore, meta.name);
    results.push_back(std::move(item));
  }
  return results;
}

}  // namespace

FalconCommandSource::FalconCommandSource() = default;
FalconCommandSource::~FalconCommandSource() = default;

CommandSource::CommandResults FalconCommandSource::GetCommands(
    const std::u16string& input,
    BrowserWindowInterface* browser) const {
  CommandSource::CommandResults results;
  if (!browser) {
    return results;
  }
  FuzzyFinder finder(input);
  std::vector<gfx::Range> ranges;
  base::WeakPtr<BrowserWindowInterface> weak = browser->GetWeakPtr();

  struct Page {
    const char16_t* title;
    const char* url;
  };
  static constexpr Page kPages[] = {
      {u"Falcon Downloads", "chrome://downloader"},
      {u"Falcon download settings", "chrome://downloader#settings"},
      {u"Falcon control panel", "chrome://falcon"},
      {u"Falcon Boosts (per-site CSS/JS)", "chrome://falcon#boosts"},
      {u"New tab page settings", "chrome://newtab#settings"},
  };
  for (const Page& page : kPages) {
    if (auto item = ItemForTitle(page.title, finder, ranges)) {
      item->command = base::BindOnce(&OpenPage, weak, GURL(page.url));
      results.push_back(std::move(item));
    }
  }

  struct Toggle {
    const char16_t* title;
    const char* pref;
  };
  const Toggle kToggles[] = {
      {u"Toggle vertical tabs", brave_tabs::kVerticalTabsEnabled},
      {u"Toggle mouse gestures", falcon::prefs::kMouseGesturesEnabled},
      {u"Toggle Peek (Shift+click preview)", falcon::prefs::kPeekEnabled},
  };
  for (const Toggle& toggle : kToggles) {
    if (auto item = ItemForTitle(toggle.title, finder, ranges)) {
      item->command = base::BindOnce(&TogglePref, weak, toggle.pref);
      results.push_back(std::move(item));
    }
  }

  if (Sessions(browser)) {
    const std::u16string save = u"Save session as…";
    if (auto item = ItemForTitle(save, finder, ranges)) {
      item->command = std::make_pair(
          std::u16string(u"Session name"),
          base::BindRepeating(&SaveSessionItems, weak));
      results.push_back(std::move(item));
    }
    const std::u16string restore = u"Restore session…";
    if (auto item = ItemForTitle(restore, finder, ranges)) {
      item->command = std::make_pair(
          std::u16string(u"Restore which session?"),
          base::BindRepeating(&SessionItems, weak, true));
      results.push_back(std::move(item));
    }
    const std::u16string del = u"Delete saved session…";
    if (auto item = ItemForTitle(del, finder, ranges)) {
      item->command = std::make_pair(
          std::u16string(u"Delete which session?"),
          base::BindRepeating(&SessionItems, weak, false));
      results.push_back(std::move(item));
    }
  }

  return results;
}

}  // namespace commander
