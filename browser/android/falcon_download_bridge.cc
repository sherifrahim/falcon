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
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/download/android/download_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "net/http/http_content_disposition.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

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

void StartWithCookies(Profile* profile,
                      const GURL& url,
                      const std::string& referer,
                      const std::string& user_agent,
                      const std::string& filename,
                      const std::string& mime_type,
                      int64_t content_length) {
  auto* cookie_manager = profile->GetDefaultStoragePartition()
                             ->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookies(url, referer, user_agent, filename, mime_type, content_length, {},
              {});
    return;
  }
  cookie_manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(&OnCookies, url, referer, user_agent, filename, mime_type,
                     content_length));
}

// Watches a blob:/data: download Chromium writes itself (only the page can read
// those URLs) and, once it completes, lists it in Falcon's downloads and drops
// Chromium's entry, so every download lives in one list. Deletes itself.
class AdoptWhenComplete : public download::DownloadItem::Observer {
 public:
  explicit AdoptWhenComplete(download::DownloadItem* item) {
    item->AddObserver(this);
  }

  void OnDownloadUpdated(download::DownloadItem* item) override {
    switch (item->GetState()) {
      case download::DownloadItem::COMPLETE:
        Adopt(item);
        [[fallthrough]];
      case download::DownloadItem::CANCELLED:
        item->RemoveObserver(this);
        delete this;
        return;
      default:
        return;
    }
  }

  void OnDownloadDestroyed(download::DownloadItem* item) override {
    item->RemoveObserver(this);
    delete this;
  }

 private:
  static void Adopt(download::DownloadItem* item) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_FalconDownloadBridge_adopt(
        env,
        base::android::ConvertUTF8ToJavaString(
            env, item->GetTargetFilePath().value()),
        base::android::ConvertUTF8ToJavaString(env, item->GetURL().spec()),
        base::android::ConvertUTF8ToJavaString(
            env, item->GetFileNameToReportUser().value()),
        base::android::ConvertUTF8ToJavaString(env, item->GetMimeType()),
        item->GetReceivedBytes());
    // Drop Chromium's entry once this update has been delivered. A finished
    // file stays on disk (Remove() only discards partial state).
    content::BrowserContext* context =
        content::DownloadItemUtils::GetBrowserContext(item);
    if (!context) {
      return;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<Profile> profile, std::string guid) {
              if (!profile) {
                return;
              }
              if (download::DownloadItem* done =
                      profile->GetDownloadManager()->GetDownloadByGuid(guid)) {
                done->Remove();
              }
            },
            Profile::FromBrowserContext(context)->GetWeakPtr(),
            item->GetGuid()));
  }
};

}  // namespace

bool MaybeTakeOverDownloadItemAndroid(Profile* profile,
                                      download::DownloadItem* item) {
  if (!profile || !item || item->IsTransient() ||
      item->IsSavePackageDownload()) {
    return false;
  }
  const GURL& url = item->GetURL();
  if (url.SchemeIs(url::kBlobScheme) || url.SchemeIs(url::kDataScheme)) {
    // Chromium writes these; Falcon lists the result.
    JNIEnv* env = base::android::AttachCurrentThread();
    if (Java_FalconDownloadBridge_shouldIntercept(
            env, base::android::ConvertUTF8ToJavaString(env, url.spec()),
            base::android::ConvertUTF8ToJavaString(env, std::string()),
            base::android::ConvertUTF8ToJavaString(env, item->GetMimeType()),
            item->GetTotalBytes())) {
      new AdoptWhenComplete(item);
    }
    return false;
  }
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const net::HttpContentDisposition disposition(item->GetContentDisposition(),
                                                std::string());
  const std::string filename =
      item->GetSuggestedFilename().empty()
          ? GuessFilename(url, disposition.filename())
          : item->GetSuggestedFilename();
  JNIEnv* env = base::android::AttachCurrentThread();
  if (!Java_FalconDownloadBridge_shouldIntercept(
          env, base::android::ConvertUTF8ToJavaString(env, url.spec()),
          base::android::ConvertUTF8ToJavaString(env, filename),
          base::android::ConvertUTF8ToJavaString(env, item->GetMimeType()),
          item->GetTotalBytes())) {
    return false;
  }
  // The item already carries the referrer Chromium's policy chose.
  StartWithCookies(profile, url, item->GetReferrerUrl().spec(),
                   embedder_support::GetUserAgent(), filename,
                   item->GetMimeType(), item->GetTotalBytes());
  VLOG(1) << "Falcon: taking over download " << url.spec();
  return true;
}

bool MaybeInterceptDownloadAndroid(Profile* profile,
                                   const GURL& url,
                                   const std::string& user_agent,
                                   const std::string& content_disposition,
                                   const std::string& mime_type,
                                   int64_t content_length,
                                   bool is_transient,
                                   bool is_content_initiated,
                                   content::WebContents* web_contents) {
  if (!profile || is_transient || !url.SchemeIsHTTPOrHTTPS()) {
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

  // Chromium passes no user agent for downloads a page didn't start; the
  // fetchers then announce themselves ("Dalvik", "aria2"), which hosts that
  // block bots refuse. Send the browser's own.
  const std::string ua =
      user_agent.empty() ? embedder_support::GetUserAgent() : user_agent;

  // The referer Chrome itself would send (strict-origin-when-cross-origin):
  // none for typed/external navigations or an https -> http downgrade, the
  // page for same-origin links, only its origin cross-site. Sending whatever
  // the tab last showed got downloads refused as hotlinks (HTTP 403).
  std::string referer;
  if (web_contents && is_content_initiated) {
    const GURL& page = web_contents->GetLastCommittedURL();
    if (page.SchemeIsHTTPOrHTTPS() &&
        !(page.SchemeIsCryptographic() && !url.SchemeIsCryptographic())) {
      referer = url::Origin::Create(page).IsSameOriginWith(url)
                    ? page.GetWithoutRef().spec()
                    : page.DeprecatedGetOriginAsURL().spec();
    }
  }
  StartWithCookies(profile, url, referer, ua, filename, mime_type,
                   content_length);
  // A tab opened only for this download stays blank and asks to download
  // again whenever the session is restored; close it as Chrome does.
  if (web_contents) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<content::WebContents> contents) {
                         if (contents) {
                           DownloadController::CloseTabIfEmpty(contents.get(),
                                                               nullptr);
                         }
                       },
                       web_contents->GetWeakPtr()));
  }
  VLOG(1) << "Falcon: intercepting download " << url.spec();
  return true;
}

}  // namespace falcon
