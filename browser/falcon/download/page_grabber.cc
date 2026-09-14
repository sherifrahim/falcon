// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/page_grabber.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "brave/browser/falcon/download/download_categories.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr size_t kMaxHtmlBytes = 8 * 1024 * 1024;
constexpr size_t kMaxLinks = 2000;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("falcon_page_grabber", R"(
      semantics {
        sender: "Falcon site grabber"
        description: "Fetches a page the user asked to scan for downloadable "
                     "files and lists the links found in it."
        trigger: "The user enters a page URL in falcon://downloader > Grab."
        data: "The page URL; cookies for that site so member-only pages work."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: YES
        setting: "Only on explicit user action."
        policy_exception_justification: "Personal build."
      })");

// Attribute values that can carry a file URL.
const RE2& AttrRe() {
  static const base::NoDestructor<RE2> re(
      R"((?i)\b(?:href|src|data-src|data-href|data-url|content)\s*=\s*["']([^"'<>\s]+)["'])");
  return *re;
}

const RE2& BaseRe() {
  static const base::NoDestructor<RE2> re(
      R"((?i)<base[^>]*\bhref\s*=\s*["']([^"']+)["'])");
  return *re;
}

std::string Unescape(std::string s) {
  base::ReplaceSubstringsAfterOffset(&s, 0, "&amp;", "&");
  base::ReplaceSubstringsAfterOffset(&s, 0, "&#38;", "&");
  base::ReplaceSubstringsAfterOffset(&s, 0, "&#x26;", "&");
  return s;
}

}  // namespace

base::ListValue ExtractLinks(const std::string& html, const GURL& page) {
  base::ListValue out;
  std::set<std::string> seen;

  // Honour <base href>.
  GURL base = page;
  std::string base_href;
  if (RE2::PartialMatch(html, BaseRe(), &base_href)) {
    const GURL resolved = page.Resolve(base_href);
    if (resolved.is_valid()) base = resolved;
  }

  re2::StringPiece input(html);
  std::string value;
  while (out.size() < kMaxLinks &&
         RE2::FindAndConsume(&input, AttrRe(), &value)) {
    const std::string raw = Unescape(value);
    if (raw.empty() || base::StartsWith(raw, "#") ||
        base::StartsWith(raw, "javascript:",
                         base::CompareCase::INSENSITIVE_ASCII) ||
        base::StartsWith(raw, "data:", base::CompareCase::INSENSITIVE_ASCII) ||
        base::StartsWith(raw, "mailto:",
                         base::CompareCase::INSENSITIVE_ASCII)) {
      continue;
    }
    const GURL url = base.Resolve(raw);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    const std::string name = GuessFilename(url, std::string());
    // Only URLs that look like files (have a known category) or the odd
    // extension-less download link with "download" in it.
    const std::string category = CategoryForFilename(name);
    const bool download_hint =
        url.query().find("download") != std::string_view::npos ||
        url.path().find("/download") != std::string_view::npos;
    if (category.empty() && !download_hint) {
      continue;
    }
    GURL::Replacements strip_ref;
    strip_ref.ClearRef();
    const std::string key = url.ReplaceComponents(strip_ref).spec();
    if (!seen.insert(key).second) {
      continue;
    }
    base::DictValue d;
    d.Set("url", key);
    d.Set("name", name.empty() ? std::string(url.host()) : name);
    d.Set("category", category.empty() ? "Other" : category);
    out.Append(std::move(d));
  }
  return out;
}

namespace {

void OnPageFetched(GrabCallback callback,
                   GURL page,
                   std::unique_ptr<network::SimpleURLLoader> loader,
                   std::optional<std::string> body) {
  base::DictValue result;
  if (!body) {
    std::string error = "The page could not be fetched";
    if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
      error += " (HTTP " +
               base::NumberToString(
                   loader->ResponseInfo()->headers->response_code()) +
               ")";
    }
    result.Set("error", error);
    std::move(callback).Run(std::move(result));
    return;
  }
  result.Set("links", ExtractLinks(*body, page));
  std::move(callback).Run(std::move(result));
}

}  // namespace

void GrabPageLinks(Profile* profile, const GURL& page, GrabCallback callback) {
  base::DictValue err;
  if (!profile || !page.SchemeIsHTTPOrHTTPS()) {
    err.Set("error", "Enter an http(s) page URL");
    std::move(callback).Run(std::move(err));
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = page;
  request->site_for_cookies = net::SiteForCookies::FromUrl(page);
  request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "text/html,application/xhtml+xml");
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  auto* raw = loader.get();
  raw->DownloadToString(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&OnPageFetched, std::move(callback), page,
                     std::move(loader)),
      kMaxHtmlBytes);
}

}  // namespace falcon
