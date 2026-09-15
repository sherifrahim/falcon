// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/page_grabber.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
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

constexpr size_t kMaxHtmlBytes = 5 * 1024 * 1024;  // SimpleURLLoader cap
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

std::vector<GURL> ExtractPageLinks(const std::string& html, const GURL& page) {
  std::vector<GURL> out;
  std::set<std::string> seen;
  GURL base = page;
  std::string base_href;
  if (RE2::PartialMatch(html, BaseRe(), &base_href)) {
    const GURL resolved = page.Resolve(base_href);
    if (resolved.is_valid()) base = resolved;
  }
  re2::StringPiece input(html);
  std::string value;
  while (out.size() < 200 && RE2::FindAndConsume(&input, AttrRe(), &value)) {
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
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
        url.host() != page.host()) {
      continue;
    }
    // Pages only: nothing that already looks like a file.
    const std::string name = GuessFilename(url, std::string());
    if (!CategoryForFilename(name).empty()) {
      continue;
    }
    const std::string lower = base::ToLowerASCII(url.path());
    if (base::EndsWith(lower, ".css") || base::EndsWith(lower, ".js") ||
        base::EndsWith(lower, ".json") || base::EndsWith(lower, ".xml") ||
        base::EndsWith(lower, ".svg") || base::EndsWith(lower, ".ico") ||
        base::EndsWith(lower, ".png") || base::EndsWith(lower, ".jpg") ||
        base::EndsWith(lower, ".webp") || base::EndsWith(lower, ".woff2")) {
      continue;
    }
    GURL::Replacements strip_ref;
    strip_ref.ClearRef();
    const GURL key = url.ReplaceComponents(strip_ref);
    if (key == page || !seen.insert(key.spec()).second) {
      continue;
    }
    out.push_back(key);
  }
  return out;
}

namespace {

constexpr size_t kMaxSubPages = 40;
constexpr int kMaxInFlight = 4;

std::unique_ptr<network::SimpleURLLoader> MakePageLoader(const GURL& page) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = page;
  request->site_for_cookies = net::SiteForCookies::FromUrl(page);
  request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "text/html,application/xhtml+xml");
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  return loader;
}

// One grab: the root page, then (depth 2) a bounded crawl of same-site pages.
// Owns itself until the callback has run.
class Crawl {
 public:
  Crawl(Profile* profile, const GURL& root, int depth, GrabCallback callback)
      : profile_(profile),
        root_(root),
        depth_(depth),
        callback_(std::move(callback)) {}

  void Start() {
    Fetch(root_, /*is_root=*/true);
  }

 private:
  void Fetch(const GURL& page, bool is_root) {
    auto loader = MakePageLoader(page);
    auto* raw = loader.get();
    ++in_flight_;
    raw->DownloadToString(
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess()
            .get(),
        base::BindOnce(&Crawl::OnFetched, base::Unretained(this), page,
                       is_root, std::move(loader)),
        kMaxHtmlBytes);
  }

  void OnFetched(GURL page,
                 bool is_root,
                 std::unique_ptr<network::SimpleURLLoader> loader,
                 std::optional<std::string> body) {
    --in_flight_;
    if (is_root && !body) {
      std::string error = "The page could not be fetched";
      if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
        error += " (HTTP " +
                 base::NumberToString(
                     loader->ResponseInfo()->headers->response_code()) +
                 ")";
      }
      base::DictValue result;
      result.Set("error", error);
      Finish(std::move(result));
      return;
    }
    if (body) {
      for (base::Value& v : ExtractLinks(*body, page)) {
        base::DictValue* d = v.GetIfDict();
        const std::string* url = d ? d->FindString("url") : nullptr;
        if (!url || !seen_files_.insert(*url).second) {
          continue;
        }
        if (!is_root) {
          d->Set("from", page.spec());
        }
        links_.Append(std::move(v));
      }
      if (is_root && depth_ >= 2) {
        for (const GURL& sub : ExtractPageLinks(*body, page)) {
          if (queue_.size() >= kMaxSubPages) {
            break;
          }
          if (seen_pages_.insert(sub.spec()).second) {
            queue_.push_back(sub);
          }
        }
      }
    }
    if (!is_root) {
      ++pages_scanned_;
    }
    Pump();
  }

  void Pump() {
    while (in_flight_ < kMaxInFlight && !queue_.empty()) {
      GURL next = queue_.front();
      queue_.erase(queue_.begin());
      Fetch(next, /*is_root=*/false);
    }
    if (in_flight_ == 0 && queue_.empty()) {
      base::DictValue result;
      result.Set("links", std::move(links_));
      result.Set("pagesScanned", pages_scanned_ + 1);
      Finish(std::move(result));
    }
  }

  void Finish(base::DictValue result) {
    std::move(callback_).Run(std::move(result));
    delete this;
  }

  raw_ptr<Profile> profile_;
  const GURL root_;
  const int depth_;
  GrabCallback callback_;
  int in_flight_ = 0;
  int pages_scanned_ = 0;
  std::vector<GURL> queue_;
  std::set<std::string> seen_pages_;
  std::set<std::string> seen_files_;
  base::ListValue links_;
};

}  // namespace

void GrabPageLinks(Profile* profile,
                   const GURL& page,
                   int depth,
                   GrabCallback callback) {
  base::DictValue err;
  if (!profile || !page.SchemeIsHTTPOrHTTPS()) {
    err.Set("error", "Enter an http(s) page URL");
    std::move(callback).Run(std::move(err));
    return;
  }
  // Self-deleting once the callback has run.
  (new Crawl(profile, page, depth, std::move(callback)))->Start();
}

}  // namespace falcon
