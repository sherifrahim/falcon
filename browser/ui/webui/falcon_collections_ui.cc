// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_collections_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "brave/browser/falcon/collections/collections_service.h"
#include "brave/browser/falcon/collections/collections_service_factory.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/falcon_collections_ui/resources/grit/falcon_collections_generated_map.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "components/grit/brave_components_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace {

class FalconCollectionsMessageHandler
    : public content::WebUIMessageHandler,
      public falcon::CollectionsService::Observer {
 public:
  FalconCollectionsMessageHandler() = default;
  ~FalconCollectionsMessageHandler() override {
    if (service_) {
      service_->RemoveObserver(this);
    }
  }

  // falcon::CollectionsService::Observer:
  void OnCollectionsChanged() override {
    if (IsJavascriptAllowed()) {
      FireWebUIListener("collections-changed", State());
    }
  }

 private:
  falcon::CollectionsService* service() {
    if (!service_) {
      service_ = falcon::CollectionsServiceFactory::GetForProfile(
          Profile::FromWebUI(web_ui()));
      if (service_) {
        service_->AddObserver(this);
      }
    }
    return service_;
  }

  void RegisterMessages() override {
    auto reg = [this](const char* name,
                      void (FalconCollectionsMessageHandler::*fn)(
                          const base::ListValue&)) {
      web_ui()->RegisterMessageCallback(
          name, base::BindRepeating(fn, base::Unretained(this)));
    };
    reg("collections.get", &FalconCollectionsMessageHandler::Get);
    reg("collections.create", &FalconCollectionsMessageHandler::Create);
    reg("collections.rename", &FalconCollectionsMessageHandler::Rename);
    reg("collections.delete", &FalconCollectionsMessageHandler::Delete);
    reg("collections.reorder", &FalconCollectionsMessageHandler::Reorder);
    reg("collections.addCurrentPage",
        &FalconCollectionsMessageHandler::AddCurrentPage);
    reg("collections.addItem", &FalconCollectionsMessageHandler::AddItem);
    reg("collections.removeItem",
        &FalconCollectionsMessageHandler::RemoveItem);
    reg("collections.moveItem", &FalconCollectionsMessageHandler::MoveItem);
    reg("collections.setLast", &FalconCollectionsMessageHandler::SetLast);
    reg("collections.open", &FalconCollectionsMessageHandler::Open);
    reg("collections.openAll", &FalconCollectionsMessageHandler::OpenAll);
    reg("collections.copyMarkdown",
        &FalconCollectionsMessageHandler::CopyMarkdown);
  }

  base::DictValue State() {
    base::DictValue state;
    if (auto* s = service()) {
      state.Set("collections", s->Collections());
      state.Set("last", Profile::FromWebUI(web_ui())->GetPrefs()->GetString(
                            falcon::prefs::kCollectionsLast));
    } else {
      state.Set("collections", base::ListValue());
      state.Set("last", std::string());
    }
    return state;
  }

  static const std::string& Str(const base::ListValue& args, size_t i) {
    static const std::string kEmpty;
    return i < args.size() && args[i].is_string() ? args[i].GetString()
                                                  : kEmpty;
  }

  BrowserWindowInterface* Browser() {
    return ProfileBrowserCollection::GetForProfile(Profile::FromWebUI(web_ui()))
        ->GetLastActiveBrowser();
  }

  void Get(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0], State());
  }

  void Create(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    AllowJavascript();
    std::string id;
    if (auto* s = service()) {
      id = s->Create(Str(args, 1));
    }
    ResolveJavascriptCallback(args[0], base::Value(id));
  }

  void Rename(const base::ListValue& args) {
    if (auto* s = service()) {
      s->Rename(Str(args, 0), Str(args, 1));
    }
  }

  void Delete(const base::ListValue& args) {
    if (auto* s = service()) {
      s->Delete(Str(args, 0));
    }
  }

  void Reorder(const base::ListValue& args) {
    auto* s = service();
    if (s && !args.empty() && args[0].is_list()) {
      s->Reorder(args[0].GetList());
    }
  }

  void AddCurrentPage(const base::ListValue& args) {
    auto* s = service();
    BrowserWindowInterface* browser = Browser();
    if (!s || !browser) {
      return;
    }
    content::WebContents* contents =
        browser->GetTabStripModel()->GetActiveWebContents();
    if (!contents) {
      return;
    }
    const GURL& url = contents->GetLastCommittedURL();
    if (!url.SchemeIsHTTPOrHTTPS() && !url.SchemeIsFile()) {
      return;
    }
    std::string id = Str(args, 0);
    if (id.empty()) {
      id = s->LastCollectionId();
    }
    s->AddItem(id, falcon::CollectionsService::MakeItem(
                       "page", base::UTF16ToUTF8(contents->GetTitle()),
                       url.spec(), std::string()));
  }

  // (collectionId, kind, title, url, text)
  void AddItem(const base::ListValue& args) {
    auto* s = service();
    if (!s) {
      return;
    }
    std::string id = Str(args, 0);
    if (id.empty()) {
      id = s->LastCollectionId();
    }
    s->AddItem(id, falcon::CollectionsService::MakeItem(
                       Str(args, 1), Str(args, 2), Str(args, 3), Str(args, 4)));
  }

  void RemoveItem(const base::ListValue& args) {
    if (auto* s = service()) {
      s->RemoveItem(Str(args, 0), Str(args, 1));
    }
  }

  // (collectionId, itemId, toCollectionId, index)
  void MoveItem(const base::ListValue& args) {
    auto* s = service();
    if (!s || args.size() < 4) {
      return;
    }
    const int index = args[3].is_int() ? args[3].GetInt() : -1;
    const std::string to = Str(args, 2).empty() ? Str(args, 0) : Str(args, 2);
    s->MoveItem(Str(args, 0), Str(args, 1), to, index);
  }

  void SetLast(const base::ListValue& args) {
    if (auto* s = service()) {
      s->SetLastCollectionId(Str(args, 0));
    }
  }

  void Navigate(const GURL& url, bool foreground) {
    BrowserWindowInterface* browser = Browser();
    if (!browser || !url.is_valid() ||
        (!url.SchemeIsHTTPOrHTTPS() && !url.SchemeIsFile())) {
      return;
    }
    NavigateParams params(browser->GetProfile(), url,
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = foreground
                             ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                             : WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.browser = browser;
    ::Navigate(&params);
  }

  // (url, background?)
  void Open(const base::ListValue& args) {
    const bool background = args.size() > 1 && args[1].is_bool() &&
                            args[1].GetBool();
    Navigate(GURL(Str(args, 0)), !background);
  }

  void OpenAll(const base::ListValue& args) {
    auto* s = service();
    if (!s) {
      return;
    }
    std::optional<base::DictValue> c = s->Find(Str(args, 0));
    const base::ListValue* items = c ? c->FindList("items") : nullptr;
    if (!items) {
      return;
    }
    bool first = true;
    for (const base::Value& v : *items) {
      const base::DictValue* d = v.GetIfDict();
      const std::string* kind = d ? d->FindString("kind") : nullptr;
      const std::string* url = d ? d->FindString("url") : nullptr;
      if (!kind || !url || *kind == "text" || url->empty()) {
        continue;
      }
      Navigate(GURL(*url), first);
      first = false;
    }
  }

  void CopyMarkdown(const base::ListValue& args) {
    auto* s = service();
    if (!s) {
      return;
    }
    const std::string md = s->ToMarkdown(Str(args, 0));
    if (md.empty()) {
      return;
    }
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(md));
  }

  raw_ptr<falcon::CollectionsService> service_ = nullptr;
};

void SetUpCollectionsDataSource(content::WebUI* web_ui,
                                std::string_view host,
                                bool panel) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = CreateAndAddWebUIDataSource(
      web_ui, host, kFalconCollectionsGenerated, IDR_FALCON_COLLECTIONS_HTML);
  source->AddBoolean("panel", panel);
  source->AddBoolean("blackTheme", g_browser_process->local_state()->GetBoolean(
                                       falcon::prefs::kThemeBlack));
  // Item cards show the site favicon (chrome://favicon2) and, for image
  // items, the image itself.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' chrome://resources chrome://theme chrome://favicon2 "
      "data: https: http:;");
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
  web_ui->AddMessageHandler(
      std::make_unique<FalconCollectionsMessageHandler>());
}

}  // namespace

FalconCollectionsUI::FalconCollectionsUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  SetUpCollectionsDataSource(web_ui, falcon::kFalconCollectionsHost,
                             /*panel=*/false);
}

FalconCollectionsUI::~FalconCollectionsUI() = default;

FalconCollectionsPanelUI::FalconCollectionsPanelUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui, /*enable_chrome_send=*/true) {
  SetUpCollectionsDataSource(web_ui, falcon::kFalconCollectionsPanelHost,
                             /*panel=*/true);
}

FalconCollectionsPanelUI::~FalconCollectionsPanelUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(FalconCollectionsPanelUI)

bool FalconCollectionsPanelUIConfig::ShouldAutoResizeHost() {
  return false;
}
