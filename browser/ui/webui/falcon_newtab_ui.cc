// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/falcon_newtab_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/falcon_newtab_ui/resources/grit/falcon_newtab_generated_map.h"
#include "chrome/browser/history/top_sites_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "components/grit/brave_components_resources.h"
#include "components/history/core/browser/top_sites.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace falcon::prefs {

void RegisterNtpProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kNtpState);
}

}  // namespace falcon::prefs

namespace {

constexpr net::NetworkTrafficAnnotationTag kBingTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("falcon_ntp_bing_wallpaper", R"(
      semantics {
        sender: "Falcon new tab page"
        description: "Fetches the metadata of Bing's picture of the day to "
                     "use as the new tab wallpaper when the user picked "
                     "'Bing daily' as background."
        trigger: "Opening a new tab with the Bing background enabled "
                 "(cached for the day)."
        data: "None beyond the request itself."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "New tab page settings > Background."
        policy_exception_justification: "Personal build."
      })");

// One Bing lookup per browser session per day.
struct BingCache {
  base::Time fetched;
  base::DictValue image;
};
BingCache& GetBingCache() {
  static base::NoDestructor<BingCache> cache;
  return *cache;
}

class FalconNewTabMessageHandler : public content::WebUIMessageHandler {
 public:
  FalconNewTabMessageHandler() = default;
  ~FalconNewTabMessageHandler() override = default;

 private:
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.getState",
        base::BindRepeating(&FalconNewTabMessageHandler::GetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.setState",
        base::BindRepeating(&FalconNewTabMessageHandler::SetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.getTopSites",
        base::BindRepeating(&FalconNewTabMessageHandler::GetTopSites,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.getBingImage",
        base::BindRepeating(&FalconNewTabMessageHandler::GetBingImage,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.search",
        base::BindRepeating(&FalconNewTabMessageHandler::Search,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "falcon_newtab.open",
        base::BindRepeating(&FalconNewTabMessageHandler::Open,
                            base::Unretained(this)));
  }

  Profile* profile() { return Profile::FromWebUI(web_ui()); }

  // args: [callbackId] -> the stored dict (may be empty)
  void GetState(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    ResolveJavascriptCallback(args[0],
                              profile()->GetPrefs()->GetDict(falcon::prefs::kNtpState));
  }

  // args: [dict] - replaces the stored state (capped so a runaway page cannot
  // bloat the prefs file).
  void SetState(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    if (!args[0].is_dict()) {
      return;
    }
    const base::DictValue& d = args[0].GetDict();
    const base::ListValue* links = d.FindList("links");
    if (links && links->size() > 200) {
      return;
    }
    profile()->GetPrefs()->SetDict(falcon::prefs::kNtpState, d.Clone());
  }

  // args: [callbackId] -> [{title, url}] most visited
  void GetTopSites(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    scoped_refptr<history::TopSites> top_sites =
        TopSitesFactory::GetForProfile(profile());
    if (!top_sites) {
      ResolveJavascriptCallback(args[0], base::ListValue());
      return;
    }
    top_sites->GetMostVisitedURLs(
        base::BindOnce(&FalconNewTabMessageHandler::OnTopSites,
                       weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void OnTopSites(base::Value callback_id,
                  const history::MostVisitedURLList& urls) {
    base::ListValue list;
    for (const history::MostVisitedURL& mv : urls) {
      if (!mv.url.SchemeIsHTTPOrHTTPS()) continue;
      base::DictValue d;
      d.Set("title", mv.title.empty() ? base::UTF8ToUTF16(mv.url.host())
                                      : mv.title);
      d.Set("url", mv.url.spec());
      list.Append(std::move(d));
      if (list.size() >= 12) break;
    }
    if (IsJavascriptAllowed()) {
      ResolveJavascriptCallback(callback_id, list);
    }
  }

  // args: [callbackId] -> {url, title, copyright} or {error}
  void GetBingImage(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    AllowJavascript();
    BingCache& cache = GetBingCache();
    if (!cache.image.empty() &&
        base::Time::Now() - cache.fetched < base::Hours(6)) {
      ResolveJavascriptCallback(args[0], cache.image);
      return;
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(
        "https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=en-US");
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                   kBingTrafficAnnotation);
    auto* raw = loader.get();
    raw->DownloadToString(
        profile()->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess()
            .get(),
        base::BindOnce(&FalconNewTabMessageHandler::OnBingImage,
                       weak_factory_.GetWeakPtr(), args[0].Clone(),
                       std::move(loader)),
        64 * 1024);
  }

  void OnBingImage(base::Value callback_id,
                   std::unique_ptr<network::SimpleURLLoader> loader,
                   std::optional<std::string> body) {
    base::DictValue result;
    std::optional<base::Value> json =
        body ? base::JSONReader::Read(*body, base::JSON_PARSE_RFC)
             : std::nullopt;
    const base::ListValue* images =
        json && json->is_dict() ? json->GetDict().FindList("images") : nullptr;
    if (images && !images->empty() && (*images)[0].is_dict()) {
      const base::DictValue& img = (*images)[0].GetDict();
      const std::string* url = img.FindString("url");
      if (url) {
        result.Set("url", "https://www.bing.com" + *url);
        if (const std::string* t = img.FindString("title")) result.Set("title", *t);
        if (const std::string* c = img.FindString("copyright")) {
          result.Set("copyright", *c);
        }
        if (const std::string* l = img.FindString("copyrightlink")) {
          result.Set("link", *l);
        }
        BingCache& cache = GetBingCache();
        cache.fetched = base::Time::Now();
        cache.image = result.Clone();
      }
    }
    if (result.empty()) {
      result.Set("error", "Bing image unavailable");
    }
    if (IsJavascriptAllowed()) {
      ResolveJavascriptCallback(callback_id, result);
    }
  }

  // args: [query] - default search engine, in this tab.
  void Search(const base::ListValue& args) {
    CHECK_EQ(1U, args.size());
    const std::u16string terms =
        base::UTF8ToUTF16(base::TrimWhitespaceASCII(args[0].GetString(),
                                                    base::TRIM_ALL));
    if (terms.empty()) {
      return;
    }
    auto* service = TemplateURLServiceFactory::GetForProfile(profile());
    GURL url = service ? service->GenerateSearchURLForDefaultSearchProvider(terms)
                       : GURL();
    if (!url.is_valid()) {
      url = GURL("https://www.google.com/search?q=" +
                 base::EscapeQueryParamValue(base::UTF16ToUTF8(terms), true));
    }
    Navigate(url, false);
  }

  // args: [url, newTab]
  void Open(const base::ListValue& args) {
    CHECK_EQ(2U, args.size());
    const GURL url(args[0].GetString());
    if (!url.is_valid() ||
        !(url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("chrome") ||
          url.SchemeIs("falcon") || url.SchemeIs("file"))) {
      return;
    }
    Navigate(url, args[1].GetBool());
  }

  void Navigate(const GURL& url, bool new_tab) {
    content::WebContents* contents = web_ui()->GetWebContents();
    if (!contents) {
      return;
    }
    if (new_tab) {
      content::OpenURLParams params(url, content::Referrer(),
                                    WindowOpenDisposition::NEW_BACKGROUND_TAB,
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
      contents->OpenURL(params, /*navigation_handle_callback=*/{});
      return;
    }
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
    contents->GetController().LoadURLWithParams(params);
  }

  base::WeakPtrFactory<FalconNewTabMessageHandler> weak_factory_{this};
};

}  // namespace

FalconNewTabUI::FalconNewTabUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = CreateAndAddWebUIDataSource(
      web_ui, chrome::kChromeUINewTabHost, kFalconNewtabGenerated,
      IDR_FALCON_NEWTAB_HTML);
  // Wallpapers come from the web (Bing / user URLs); favicons for quick links
  // from chrome://favicon2.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' chrome://resources chrome://theme chrome://favicon2 "
      "data: blob: https:;");
  // Weather widget (Open-Meteo, keyless) and live video wallpapers.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://api.open-meteo.com "
      "https://geocoding-api.open-meteo.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::MediaSrc,
      "media-src 'self' https: blob:;");
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
  web_ui->AddMessageHandler(std::make_unique<FalconNewTabMessageHandler>());
}

FalconNewTabUI::~FalconNewTabUI() = default;
