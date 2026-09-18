/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_ANDROID_FALCON_DOWNLOAD_BRIDGE_H_
#define BRAVE_BROWSER_ANDROID_FALCON_DOWNLOAD_BRIDGE_H_

#include <cstdint>
#include <string>

class GURL;
class Profile;

namespace content {
class WebContents;
}

namespace falcon {

// Android counterpart of //brave/browser/falcon/download/download_interceptor:
// asks the Java FalconDownloadManager whether it wants this download and, if
// so, hands it over together with the page's cookies. Returns true when the
// download has been taken over (Chromium must not start its own).
bool MaybeInterceptDownloadAndroid(Profile* profile,
                                   const GURL& url,
                                   const std::string& user_agent,
                                   const std::string& content_disposition,
                                   const std::string& mime_type,
                                   int64_t content_length,
                                   bool is_transient,
                                   bool is_content_initiated,
                                   content::WebContents* web_contents);

}  // namespace falcon

#endif  // BRAVE_BROWSER_ANDROID_FALCON_DOWNLOAD_BRIDGE_H_
