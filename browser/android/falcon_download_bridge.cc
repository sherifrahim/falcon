/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/android/falcon_download_bridge.h"

#include <string>
#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "net/http/http_content_disposition.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

#include "chrome/android/chrome_jni_headers/FalconDownloadBridge_jni.h"

namespace falcon {
namespace {

std::string CookieHeader(const net::CookieAccessResultList& cookies) {
  std::string header;
  for (const auto& with_result : cookies) {
    const net::CanonicalCookie& cookie = with_result.cookie;
    if (!header.empty()) {
      header += "; ";
    }
    header += cookie.Name() + "=" + cookie.Value();
  }
  return header;
}

std::string GuessFilename(const GURL& url, const std::string& disposition) {
  if (!disposition.empty()) {
    return disposition;
  }
  std::string last = url.ExtractFileName();
  return last.empty() ? "download" : last;
}

void OnCookies(const GURL& url,
               std::string referer,
               std::string user_agent,
               std::string filename,
               std::string mime_type,
               int64_t content_length,
               const net::CookieAccessResultList& cookies,
               const net::CookieAccessResultList& excluded) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_FalconDownloadBridge_start(
      env, base::android::ConvertUTF8ToJavaString(env, url.spec()),
      base::android::ConvertUTF8ToJavaString(env, referer),
      base::android::ConvertUTF8ToJavaString(env, user_agent),
      base::android::ConvertUTF8ToJavaString(env, filename),
      base::android::ConvertUTF8ToJavaString(env, mime_type), content_length,
      base::android::ConvertUTF8ToJavaString(env, CookieHeader(cookies)));
}

}  // namespace

bool MaybeInterceptDownloadAndroid(Profile* profile,
                                   const GURL& url,
                                   const std::string& user_agent,
                                   const std::string& content_disposition,
                                   const std::string& mime_type,
                                   int64_t content_length,
                                   bool is_transient,
                                   bool is_content_initiated,
                                   content::WebContents* web_contents) {
  if (!profile || profile->IsOffTheRecord() || is_transient ||
      !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const net::HttpContentDisposition disposition(content_disposition,
                                                std::string());
  const std::string filename = GuessFilename(url, disposition.filename());
  // HTML served inline is a page, not a file.
  if (base::StartsWith(mime_type, "text/html") && !disposition.is_attachment()) {
    return false;
  }

  JNIEnv* env = base::android::AttachCurrentThread();
  if (!Java_FalconDownloadBridge_shouldIntercept(
          env, base::android::ConvertUTF8ToJavaString(env, url.spec()),
          base::android::ConvertUTF8ToJavaString(env, filename),
          base::android::ConvertUTF8ToJavaString(env, mime_type),
          content_length)) {
    return false;
  }

  std::string referer;
  if (web_contents) {
    const GURL& page = web_contents->GetLastCommittedURL();
    if (page.SchemeIsHTTPOrHTTPS()) {
      referer = page.spec();
    }
  }
  auto* cookie_manager = profile->GetDefaultStoragePartition()
                             ->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookies(url, referer, user_agent, filename, mime_type, content_length,
              {}, {});
    return true;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(&OnCookies, url, referer, user_agent, filename, mime_type,
                     content_length));
  VLOG(1) << "Falcon: intercepting download " << url.spec();
  return true;
}

}  // namespace falcon
