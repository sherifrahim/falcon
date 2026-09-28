/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/android/falcon_media_capture_tab_helper.h"

#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/media/media_classify.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "url/gurl.h"

#include "chrome/android/chrome_jni_headers/FalconMediaBridge_jni.h"

namespace falcon {

namespace {

constexpr size_t kMaxCapturesPerPage = 60;
// A "video" response this small is a probe, a beacon or an ad pixel.
constexpr int64_t kMinFileBytes = 64 * 1024;

int JavaKind(MediaKind kind) {
  // Matches CapturedMedia.Kind in Java.
  switch (kind) {
    case MediaKind::kFile:
      return 0;
    case MediaKind::kHls:
      return 1;
    case MediaKind::kDash:
      return 2;
    case MediaKind::kNone:
      break;
  }
  return -1;
}

std::string CookieHeader(const net::CookieAccessResultList& cookies) {
  std::string header;
  for (const auto& with_result : cookies) {
    if (!header.empty()) {
      header += "; ";
    }
    header += with_result.cookie.Name() + "=" + with_result.cookie.Value();
  }
  return header;
}

}  // namespace

struct FalconMediaCaptureTabHelper::Capture {
  MediaKind kind = MediaKind::kNone;
  GURL url;
  std::string mime_type;
  int64_t size = 0;
  GURL page_url;
  std::u16string page_title;
  std::string referer;
  std::string user_agent;
};

FalconMediaCaptureTabHelper::FalconMediaCaptureTabHelper(
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<FalconMediaCaptureTabHelper>(*contents) {}

FalconMediaCaptureTabHelper::~FalconMediaCaptureTabHelper() = default;

void FalconMediaCaptureTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted() ||
      handle->IsSameDocument()) {
    return;
  }
  seen_.clear();
  weak_factory_.InvalidateWeakPtrs();  // cookie lookups for the old page
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_FalconMediaBridge_onPageChanged(env,
                                       web_contents()->GetJavaWebContents());
}

void FalconMediaCaptureTabHelper::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const GURL& original_url,
    const blink::mojom::ResourceLoadInfo& info) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  // Private tabs keep nothing: the downloads list is persisted.
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  const GURL& url = info.final_url.is_valid() ? info.final_url : original_url;
  if (!url.SchemeIsHTTPOrHTTPS() || info.net_error != 0) {
    return;
  }
  using network::mojom::RequestDestination;
  if (info.request_destination == RequestDestination::kImage ||
      info.request_destination == RequestDestination::kScript ||
      info.request_destination == RequestDestination::kStyle ||
      info.request_destination == RequestDestination::kFont ||
      info.request_destination == RequestDestination::kDocument) {
    return;
  }
  const MediaKind kind = ClassifyMedia(url, info.mime_type);
  if (kind == MediaKind::kNone) {
    return;
  }
  const int64_t bytes =
      static_cast<int64_t>(info.total_received_bytes.InBytes());
  if (kind == MediaKind::kFile && bytes > 0 && bytes < kMinFileBytes) {
    return;
  }
  Report(render_frame_host, url, kind, info.mime_type, bytes);
}

void FalconMediaCaptureTabHelper::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  ScanMediaElements(render_frame_host);
}

void FalconMediaCaptureTabHelper::MediaStartedPlaying(
    const MediaPlayerInfo& video_type,
    const content::MediaPlayerId& id) {
  ScanMediaElements(content::RenderFrameHost::FromID(id.frame_routing_id));
}

void FalconMediaCaptureTabHelper::ScanMediaElements(
    content::RenderFrameHost* frame) {
  if (!frame || !frame->IsRenderFrameLive() ||
      !frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  // Direct http(s) sources only: blob: players are MSE, caught by their
  // playlist/manifest requests instead.
  static constexpr char16_t kScript[] =
      u"(() => Array.from(document.querySelectorAll('video,audio'))"
      u".map(m => [m.currentSrc || m.src ||"
      u" ((m.querySelector('source') || {}).src || ''), m.tagName])"
      u".filter(x => /^https?:/i.test(x[0])).slice(0, 20))()";
  frame->ExecuteJavaScriptInIsolatedWorld(
      kScript,
      base::BindOnce(&FalconMediaCaptureTabHelper::OnMediaElements,
                     weak_factory_.GetWeakPtr(), frame->GetGlobalId()),
      ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

void FalconMediaCaptureTabHelper::OnMediaElements(
    content::GlobalRenderFrameHostId frame_id,
    base::Value result) {
  content::RenderFrameHost* frame =
      content::RenderFrameHost::FromID(frame_id);
  if (!frame || !result.is_list()) {
    return;
  }
  for (const base::Value& entry : result.GetList()) {
    const base::ListValue* pair = entry.GetIfList();
    if (!pair || pair->size() != 2 || !(*pair)[0].is_string()) {
      continue;
    }
    const GURL url((*pair)[0].GetString());
    if (!url.SchemeIsHTTPOrHTTPS()) {
      continue;
    }
    MediaKind kind = ClassifyMedia(url, std::string());
    if (kind == MediaKind::kNone) {
      // An element's source is media whatever its URL looks like.
      kind = MediaKind::kFile;
    }
    Report(frame, url, kind, std::string(), 0);
  }
}

void FalconMediaCaptureTabHelper::Report(content::RenderFrameHost* frame,
                                         const GURL& url,
                                         MediaKind kind,
                                         const std::string& mime_type,
                                         int64_t size) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  if (kind == MediaKind::kFile && IsChunkHost(url)) {
    return;
  }
  if (seen_.size() >= kMaxCapturesPerPage ||
      !seen_.insert(base::StrCat({url.host(), url.path()})).second) {
    return;
  }

  Capture capture;
  capture.kind = kind;
  capture.url = url;
  capture.mime_type = mime_type;
  capture.size = size;
  capture.page_url = web_contents()->GetLastCommittedURL();
  capture.page_title = web_contents()->GetTitle();
  // Embedded players check the referer of their own frame, not the page's.
  const GURL frame_url =
      frame ? frame->GetLastCommittedURL() : capture.page_url;
  if (frame_url.SchemeIsHTTPOrHTTPS()) {
    capture.referer = frame_url.spec();
  }
  capture.user_agent = UserAgent();

  auto* cookie_manager = profile->GetDefaultStoragePartition()
                             ->GetCookieManagerForBrowserProcess();
  if (!cookie_manager) {
    OnCookies(std::move(capture), {}, {});
    return;
  }
  const GURL cookie_url = capture.url;
  cookie_manager->GetCookieList(
      cookie_url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection::ContainsAll(),
      base::BindOnce(&FalconMediaCaptureTabHelper::OnCookies,
                     weak_factory_.GetWeakPtr(), std::move(capture)));
}

void FalconMediaCaptureTabHelper::OnCookies(
    Capture capture,
    const net::CookieAccessResultList& cookies,
    const net::CookieAccessResultList& excluded) {
  JNIEnv* env = base::android::AttachCurrentThread();
  using base::android::ConvertUTF16ToJavaString;
  using base::android::ConvertUTF8ToJavaString;
  Java_FalconMediaBridge_onMediaCaptured(
      env, web_contents()->GetJavaWebContents(), JavaKind(capture.kind),
      ConvertUTF8ToJavaString(env, capture.url.spec()),
      ConvertUTF8ToJavaString(env, capture.mime_type), capture.size,
      ConvertUTF8ToJavaString(env, capture.page_url.spec()),
      ConvertUTF16ToJavaString(env, capture.page_title),
      ConvertUTF8ToJavaString(env, capture.referer),
      ConvertUTF8ToJavaString(env, capture.user_agent),
      ConvertUTF8ToJavaString(env, CookieHeader(cookies)));
}

std::string FalconMediaCaptureTabHelper::UserAgent() {
  // "Desktop site" swaps the UA, and some sites serve different streams to it.
  content::NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (entry && entry->GetIsOverridingUserAgent()) {
    const std::string& override_ua =
        web_contents()->GetUserAgentOverride().ua_string_override;
    if (!override_ua.empty()) {
      return override_ua;
    }
  }
  return embedder_support::GetUserAgent();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(FalconMediaCaptureTabHelper);

}  // namespace falcon
