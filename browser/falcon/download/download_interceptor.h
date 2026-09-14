// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_INTERCEPTOR_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_INTERCEPTOR_H_

#include <cstdint>
#include <string>

#include "base/files/file_path.h"

class GURL;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace falcon {

// Download directory for |profile|, with the category sub-folder applied when
// categories are enabled. |filename| may be empty.
base::FilePath TargetDirectory(Profile* profile, const std::string& filename);

// Called from BraveDownloadManagerDelegate::InterceptDownloadIfApplicable.
// Returns true when Falcon's engine takes the download; Chromium then drops
// its own request. Cookie lookup and the aria2 hand-off happen asynchronously.
bool MaybeInterceptDownload(Profile* profile,
                            const GURL& url,
                            const std::string& user_agent,
                            const std::string& content_disposition,
                            const std::string& mime_type,
                            int64_t content_length,
                            bool is_transient,
                            bool is_content_initiated,
                            content::WebContents* web_contents);

// Called from BraveContentBrowserClient::HandleExternalProtocol. Returns true
// when a magnet: link was routed to the engine.
bool MaybeHandleMagnet(Profile* profile, const GURL& url);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_INTERCEPTOR_H_
