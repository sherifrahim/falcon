/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ANDROID_FALCON_MEDIA_CAPTURE_TAB_HELPER_H_
#define BRAVE_BROWSER_ANDROID_FALCON_MEDIA_CAPTURE_TAB_HELPER_H_

#include <set>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "net/cookies/canonical_cookie.h"

class GURL;

namespace falcon {

// Android media grabber (1DM-style "capture audio/video"): watches what a tab
// loads and hands every audio/video file, HLS playlist and DASH manifest to
// the Java FalconMediaBridge, together with the referer, user agent and the
// cookies the page would send, so the grabber can download it the same way
// the page played it. Java owns the per-tab list, the per-site switch and the
// UI; this side only observes.
class FalconMediaCaptureTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<FalconMediaCaptureTabHelper> {
 public:
  ~FalconMediaCaptureTabHelper() override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void ResourceLoadComplete(
      content::RenderFrameHost* render_frame_host,
      const content::GlobalRequestID& request_id,
      const GURL& original_url,
      const blink::mojom::ResourceLoadInfo& resource_load_info) override;

 private:
  friend class content::WebContentsUserData<FalconMediaCaptureTabHelper>;
  explicit FalconMediaCaptureTabHelper(content::WebContents* contents);

  struct Capture;
  void OnCookies(Capture capture,
                 const net::CookieAccessResultList& cookies,
                 const net::CookieAccessResultList& excluded);
  std::string UserAgent();

  // host+path of everything already reported for this document: the same
  // file fetched in ranges arrives many times.
  std::set<std::string> seen_;
  base::WeakPtrFactory<FalconMediaCaptureTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_ANDROID_FALCON_MEDIA_CAPTURE_TAB_HELPER_H_
