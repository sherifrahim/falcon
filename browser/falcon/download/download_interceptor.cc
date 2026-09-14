// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_interceptor.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "brave/browser/falcon/download/download_categories.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/components/constants/url_constants.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "net/http/http_content_disposition.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace falcon {

namespace {

void OnCookiesForDownload(const GURL& url,
                          const std::string& referer,
                          const std::string& user_agent,
                          const std::string& out_filename,
                          const base::FilePath& download_dir,
                          const net::CookieAccessResultList& cookies,
                          const net::CookieAccessResultList& excluded) {
  std::string cookie_header;
  for (const auto& cookie_with_access_result : cookies) {
    const net::CanonicalCookie& cookie = cookie_with_access_result.cookie;
    if (!cookie_header.empty()) {
      cookie_header += "; ";
    }
    cookie_header += cookie.Name() + "=" + cookie.Value();
  }
  Aria2Service::Get()->AddUri(
      url, Aria2Service::BuildOptions(referer, user_agent, cookie_header,
                                      out_filename, download_dir));
}

}  // namespace

namespace {

// User rules ("ext = Folder" per line) beat the built-in categories. Folder
// may be an absolute path.
std::optional<base::FilePath> RuleFolder(Profile* profile,
                                         const base::FilePath& base_dir,
                                         const std::string& filename) {
  const std::string rules =
      profile->GetPrefs()->GetString(prefs::kDownloadCategoryRules);
  if (rules.empty()) {
    return std::nullopt;
  }
  const size_t dot = filename.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= filename.size()) {
    return std::nullopt;
  }
  const std::string ext = base::ToLowerASCII(filename.substr(dot + 1));
  for (std::string_view line : base::SplitStringPiece(
           rules, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    std::string_view key = base::TrimWhitespaceASCII(line.substr(0, eq),
                                                     base::TRIM_ALL);
    std::string_view folder = base::TrimWhitespaceASCII(line.substr(eq + 1),
                                                        base::TRIM_ALL);
    if (key.empty() || folder.empty()) continue;
    if (key.front() == '.') key.remove_prefix(1);
    if (base::ToLowerASCII(key) != ext) continue;
    const base::FilePath f = base::FilePath::FromUTF8Unsafe(folder);
    if (f.ReferencesParent()) continue;
    return f.IsAbsolute() ? f : base_dir.Append(f);
  }
  return std::nullopt;
}

}  // namespace

base::FilePath TargetDirectory(Profile* profile, const std::string& filename) {
  base::FilePath dir;
  if (auto* download_prefs = DownloadPrefs::FromBrowserContext(profile)) {
    dir = download_prefs->DownloadPath();
  }
  if (dir.empty()) {
    return dir;
  }
  if (std::optional<base::FilePath> rule = RuleFolder(profile, dir, filename)) {
    return *rule;
  }
  if (!profile->GetPrefs()->GetBoolean(prefs::kDownloadCategoriesEnabled)) {
    return dir;
  }
  const std::string category = CategoryForFilename(filename);
  return category.empty() ? dir : dir.AppendASCII(category);
}

bool MaybeInterceptDownload(Profile* profile,
                            const GURL& url,
                            const std::string& user_agent,
                            const std::string& content_disposition,
                            const std::string& mime_type,
                            int64_t content_length,
                            bool is_transient,
                            bool is_content_initiated,
                            content::WebContents* web_contents) {
  if (!profile ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadEngineEnabled)) {
    return false;
  }
  // Only plain web downloads. Never bypass Tor/private-window isolation by
  // fetching through a separate process.
  if (!url.SchemeIsHTTPOrHTTPS() || is_transient ||
      profile->IsOffTheRecord()) {
    return false;
  }
  const net::HttpContentDisposition disposition(content_disposition,
                                                std::string());
  const std::string filename = GuessFilename(url, disposition.filename());
  // .torrent files always go to the engine (aria2 follows them), whatever
  // their size.
  const bool is_torrent =
      base::StartsWith(mime_type, "application/x-bittorrent") ||
      base::EndsWith(base::ToLowerASCII(filename), ".torrent");
  // Small files: Chromium's downloader is fine and keeps the download bubble.
  const int64_t min_bytes =
      profile->GetPrefs()->GetInt64(prefs::kDownloadMinInterceptBytes);
  if (!is_torrent && content_length >= 0 && content_length < min_bytes) {
    return false;
  }
  // HTML served inline is a page, not a file, unless the site says attachment.
  if (base::StartsWith(mime_type, "text/html") && !disposition.is_attachment()) {
    return false;
  }

  std::string referer;
  if (web_contents) {
    const GURL& page = web_contents->GetLastCommittedURL();
    if (page.SchemeIsHTTPOrHTTPS()) {
      referer = page.spec();
    }
  }

  const base::FilePath download_dir = TargetDirectory(profile, filename);

  auto* cookie_manager = profile->GetDefaultStoragePartition()
                             ->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookiesForDownload(url, referer, user_agent, disposition.filename(),
                         download_dir, {}, {});
    return true;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(&OnCookiesForDownload, url, referer, user_agent,
                     disposition.filename(), download_dir));
  VLOG(1) << "Falcon: intercepting download " << url.spec();
  return true;
}

void StartEngineDownload(Profile* profile,
                         const GURL& url,
                         const GURL& referrer) {
  if (!profile || !url.is_valid() ||
      !(url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("ftp") ||
        url.SchemeIs(kMagnetScheme))) {
    return;
  }
  if (url.SchemeIs(kMagnetScheme)) {
    MaybeHandleMagnet(profile, url);
    return;
  }
  const std::string filename = GuessFilename(url, std::string());
  const base::FilePath download_dir = TargetDirectory(profile, filename);
  const std::string referer =
      referrer.SchemeIsHTTPOrHTTPS() ? referrer.spec() : std::string();
  auto* cookie_manager = profile->GetDefaultStoragePartition()
                             ->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookiesForDownload(url, referer, std::string(), std::string(),
                         download_dir, {}, {});
    return;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(&OnCookiesForDownload, url, referer, std::string(),
                     std::string(), download_dir));
}

namespace {

constexpr size_t kMaxPageDownloads = 300;

void OnPageUrlsCollected(base::WeakPtr<content::WebContents> web_contents,
                         bool images,
                         base::Value result) {
  if (!web_contents || !result.is_list()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  const GURL page = web_contents->GetLastCommittedURL();
  size_t started = 0;
  for (const base::Value& v : result.GetList()) {
    if (!v.is_string()) continue;
    const GURL url(v.GetString());
    if (!url.SchemeIsHTTPOrHTTPS()) continue;
    if (!images && CategoryForFilename(GuessFilename(url, std::string()))
                       .empty()) {
      continue;  // links: only file-looking URLs
    }
    StartEngineDownload(profile, url, page);
    if (++started >= kMaxPageDownloads) break;
  }
  VLOG(1) << "Falcon: started " << started << " page downloads";
}

}  // namespace

void DownloadAllFromPage(Profile* profile,
                         content::WebContents* web_contents,
                         bool images) {
  if (!profile || !web_contents) {
    return;
  }
  content::RenderFrameHost* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    return;
  }
  const char* script =
      images
          ? "Array.from(new Set(Array.from(document.images)"
            ".map(i => i.currentSrc || i.src).filter(Boolean)))"
          : "Array.from(new Set(Array.from(document.links)"
            ".map(a => a.href).filter(Boolean)))";
  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::BindOnce(&OnPageUrlsCollected, web_contents->GetWeakPtr(), images),
      ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

bool MaybeHandleMagnet(Profile* profile, const GURL& url) {
  if (!profile || !url.SchemeIs(kMagnetScheme) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadEngineEnabled) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadMagnetEnabled) ||
      profile->IsOffTheRecord()) {
    return false;
  }
  base::DictValue options;
  const base::FilePath dir = TargetDirectory(profile, "x.torrent");
  if (!dir.empty()) {
    options.Set("dir", dir.AsUTF8Unsafe());
  }
  VLOG(1) << "Falcon: magnet link to engine";
  Aria2Service::Get()->AddUri(url, std::move(options));
  return true;
}

}  // namespace falcon
