// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/download/download_interceptor.h"

#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "brave/browser/falcon/download/aria2_service.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "components/pref_registry/pref_registry_syncable.h"
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

constexpr int64_t kDefaultMinInterceptBytes = 1024 * 1024;  // 1 MiB

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
  Aria2Service::Get()->AddUri(url, referer, user_agent, cookie_header,
                              out_filename, download_dir);
}

}  // namespace

void RegisterDownloadProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(prefs::kDownloadEngineEnabled, true);
  registry->RegisterInt64Pref(prefs::kDownloadMinInterceptBytes,
                              kDefaultMinInterceptBytes);
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
  if (!profile || !profile->GetPrefs()->GetBoolean(
                      prefs::kDownloadEngineEnabled)) {
    return false;
  }
  // Only plain web downloads. Never bypass Tor/private-window isolation by
  // fetching through a separate process.
  if (!url.SchemeIsHTTPOrHTTPS() || is_transient ||
      profile->IsOffTheRecord()) {
    return false;
  }
  // Small files: Chromium's downloader is fine and keeps the download bubble.
  const int64_t min_bytes =
      profile->GetPrefs()->GetInt64(prefs::kDownloadMinInterceptBytes);
  if (content_length >= 0 && content_length < min_bytes) {
    return false;
  }
  // HTML served inline is a page, not a file, unless the site says attachment.
  const net::HttpContentDisposition disposition(content_disposition,
                                                std::string());
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

  base::FilePath download_dir;
  if (auto* download_prefs = DownloadPrefs::FromBrowserContext(profile)) {
    download_dir = download_prefs->DownloadPath();
  }

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

}  // namespace falcon
