// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/commander/falcon_command_source.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/collections/collections_service.h"
#include "brave/browser/falcon/collections/collections_service_factory.h"
#include "brave/browser/falcon/ux/command_chain_runner.h"
#include "brave/browser/falcon/ux/mouse_gesture_tab_helper.h"
#include "brave/browser/ui/brave_scheme_utils.h"
#include "brave/browser/ui/commander/fuzzy_finder.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/views/falcon/peek_window.h"
#include "brave/browser/workspaces/workspace_metadata.h"
#include "brave/browser/workspaces/workspace_service.h"
#include "brave/browser/workspaces/workspace_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
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

// Deck "Tabs" group: switch to an open tab of this window (upstream's
// commander only reaches tabs through composite commands).
void SwitchToTab(base::WeakPtr<BrowserWindowInterface> browser,
                 int index,
                 int session_id) {
  if (!browser) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (index < 0 || index >= model->count()) {
    return;
  }
  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents ||
      sessions::SessionTabHelper::IdForTab(contents).id() != session_id) {
    return;
  }
  model->ActivateTabAt(index);
  browser->GetWindow()->Activate();
}

std::u16string HostForDisplay(content::WebContents* contents) {
  const GURL& url = contents->GetVisibleURL();
  if (!url.is_valid()) {
    return std::u16string();
  }
  std::u16string host = url_formatter::FormatUrl(
      url,
      (url_formatter::kFormatUrlOmitDefaults &
       ~url_formatter::kFormatUrlOmitHTTP) |
          url_formatter::kFormatUrlOmitTrivialSubdomains |
          url_formatter::kFormatUrlOmitHTTPS |
          url_formatter::kFormatUrlTrimAfterHost,
      base::UnescapeRule::SPACES, nullptr, nullptr, nullptr);
  brave_utils::ReplaceChromeToBraveScheme(host);
  return host;
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

// Composite step: pick the collection the active page goes to.
CommandSource::CommandResults CollectionItems(
    base::WeakPtr<BrowserWindowInterface> browser,
    const std::u16string& input) {
  CommandSource::CommandResults results;
  if (!browser) {
    return results;
  }
  auto* service = falcon::CollectionsServiceFactory::GetForProfile(
      browser->GetProfile());
  if (!service) {
    return results;
  }
  FuzzyFinder finder(input);
  std::vector<gfx::Range> ranges;
  for (const base::Value& v : service->Collections()) {
    const base::DictValue* d = v.GetIfDict();
    const std::string* id = d ? d->FindString("id") : nullptr;
    const std::string* name = d ? d->FindString("name") : nullptr;
    if (!id || !name) {
      continue;
    }
    const std::u16string title = base::UTF8ToUTF16(*name);
    double score = input.empty() ? 1.0 : finder.Find(title, ranges);
    if (score <= 0) {
      continue;
    }
    auto item = std::make_unique<CommandItem>(title, score, ranges);
    const base::ListValue* items = d->FindList("items");
    item->annotation = base::UTF8ToUTF16(base::StrCat(
        {base::NumberToString(items ? items->size() : 0), " items"}));
    item->command = base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser, std::string id) {
          if (!browser) {
            return;
          }
          auto* service = falcon::CollectionsServiceFactory::GetForProfile(
              browser->GetProfile());
          content::WebContents* contents =
              browser->GetTabStripModel()->GetActiveWebContents();
          if (!service || !contents) {
            return;
          }
          const GURL& url = contents->GetLastCommittedURL();
          if (!url.SchemeIsHTTPOrHTTPS()) {
            return;
          }
          service->AddItem(id, falcon::CollectionsService::MakeItem(
                                   "page", base::UTF16ToUTF8(contents->GetTitle()),
                                   url.spec(), std::string()));
          browser->GetFeatures().side_panel_ui()->Show(
              SidePanelEntryKey(SidePanelEntryId::kFalconCollections));
        },
        browser, *id);
    results.push_back(std::move(item));
  }
  if (!input.empty()) {
    auto item = std::make_unique<CommandItem>(
        base::StrCat({u"New collection \"", input, u"\""}), 0.5,
        std::vector<gfx::Range>());
    item->command = base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser, std::string name) {
          if (!browser) {
            return;
          }
          auto* service = falcon::CollectionsServiceFactory::GetForProfile(
              browser->GetProfile());
          content::WebContents* contents =
              browser->GetTabStripModel()->GetActiveWebContents();
          if (!service) {
            return;
          }
          const std::string id = service->Create(name);
          if (contents && contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
            service->AddItem(
                id, falcon::CollectionsService::MakeItem(
                        "page", base::UTF16ToUTF8(contents->GetTitle()),
                        contents->GetLastCommittedURL().spec(), std::string()));
          }
          browser->GetFeatures().side_panel_ui()->Show(
              SidePanelEntryKey(SidePanelEntryId::kFalconCollections));
        },
        browser, base::UTF16ToUTF8(input));
    results.push_back(std::move(item));
  }
  return results;
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

  // Open tabs (all but the active one), so the deck's "Tabs" group is live.
  // Empty query: most recently active first.
  {
    TabStripModel* model = browser->GetTabStripModel();
    std::vector<std::pair<base::TimeTicks, int>> order;
    for (int i = 0; i < model->count(); ++i) {
      if (i == model->active_index()) {
        continue;
      }
      if (content::WebContents* c = model->GetWebContentsAt(i)) {
        order.emplace_back(c->GetLastActiveTimeTicks(), i);
      }
    }
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    double recency = 0.98;
    for (const auto& [time, i] : order) {
      content::WebContents* c = model->GetWebContentsAt(i);
      const std::u16string title = c->GetTitle();
      const std::u16string host = HostForDisplay(c);
      std::unique_ptr<CommandItem> item;
      if (input.empty()) {
        item = std::make_unique<CommandItem>(title, recency,
                                             std::vector<gfx::Range>());
        recency *= 0.97;
      } else {
        double score = finder.Find(title, ranges);
        if (score <= 0 && !host.empty()) {
          std::vector<gfx::Range> host_ranges;
          if (finder.Find(host, host_ranges) > 0) {
            score = 0.5;
            ranges.clear();
          }
        }
        if (score <= 0) {
          continue;
        }
        item = std::make_unique<CommandItem>(title, score, ranges);
      }
      item->entity_type = CommandItem::Entity::kTab;
      item->annotation = host;
      item->command = base::BindOnce(
          &SwitchToTab, weak, i, sessions::SessionTabHelper::IdForTab(c).id());
      results.push_back(std::move(item));
    }
  }

  struct Page {
    const char16_t* title;
    const char* url;
  };
  static constexpr Page kPages[] = {
      {u"Falcon Downloads", "chrome://downloader"},
      {u"Falcon download settings", "chrome://downloader#settings"},
      {u"Falcon control panel", "chrome://falcon"},
      {u"Collections", "chrome://collections"},
      {u"Command chains (macros)", "chrome://falcon#chains"},
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

  if (!browser->GetProfile()->IsOffTheRecord()) {
    const std::u16string collect = u"Add page to collection…";
    if (auto item = ItemForTitle(collect, finder, ranges)) {
      item->command = std::make_pair(
          std::u16string(u"Which collection?"),
          base::BindRepeating(&CollectionItems, weak));
      results.push_back(std::move(item));
    }
    const std::u16string toggle = u"Toggle Collections panel";
    if (auto item = ItemForTitle(toggle, finder, ranges)) {
      item->command = base::BindOnce(
          [](base::WeakPtr<BrowserWindowInterface> browser) {
            if (browser) {
              browser->GetFeatures().side_panel_ui()->Toggle(
                  SidePanelEntryKey(SidePanelEntryId::kFalconCollections),
                  SidePanelOpenTrigger::kAppMenu);
            }
          },
          weak);
      results.push_back(std::move(item));
    }
  }

  // Command chains: each saved chain is a deck command.
  for (const falcon::CommandChain& chain :
       falcon::CommandChainRunner::Chains(browser->GetProfile())) {
    const std::u16string title =
        base::StrCat({u"Run chain: ", base::UTF8ToUTF16(chain.name)});
    if (auto item = ItemForTitle(title, finder, ranges)) {
      item->annotation = base::UTF8ToUTF16(base::StrCat(
          {base::NumberToString(chain.steps.size()), " steps"}));
      item->command = base::BindOnce(
          [](base::WeakPtr<BrowserWindowInterface> browser, std::string id) {
            if (browser) {
              falcon::CommandChainRunner::Run(browser.get(), id);
            }
          },
          weak, chain.id);
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
