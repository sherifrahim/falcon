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

// "Refresh URL" for an expired/forbidden link (IDM-style): re-opens |referer|
// in a tab and arms a one-shot capture; the next intercepted download whose
// name matches |old_path|'s file name is re-added on top of the partial file
// (aria2 `continue`), and the old errored entry |gid| is dropped. Returns
// false when there is nothing to open.
bool ArmUrlRefresh(Profile* profile,
                   const std::string& gid,
                   const GURL& referer,
                   const std::string& old_path);

// Explicit "Download with Falcon": fetches the profile's cookies for |url|
// and hands it to the engine with |referrer| and the category folder.
void StartEngineDownload(Profile* profile,
                         const GURL& url,
                         const GURL& referrer);

// "Download all links/images with Falcon": collects candidate URLs from the
// page's main frame (links whose file name has a known category, or every
// image) and starts them.
void DownloadAllFromPage(Profile* profile,
                         content::WebContents* web_contents,
                         bool images);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_INTERCEPTOR_H_
