// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_PAGE_GRABBER_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_PAGE_GRABBER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

class GURL;
class Profile;

namespace falcon {

// IDM-style site grabber, single level: fetches |page| with the profile's
// cookies and returns every http(s) link/src/video source found in the
// markup as [{url, name, category}], deduplicated, capped. Reports
// {error} on failure.
using GrabCallback = base::OnceCallback<void(base::DictValue)>;
// Scans |page| for file links. With |depth| == 2 it also follows same-site
// HTML links found on the page (bounded: 40 pages, 4 in flight) and merges
// the files found there, each tagged with the page it came from.
void GrabPageLinks(Profile* profile,
                   const GURL& page,
                   int depth,
                   GrabCallback callback);

// Exposed for tests: extracts candidate URLs from |html| relative to |base|.
base::ListValue ExtractLinks(const std::string& html, const GURL& base);
// Same-site links that look like HTML pages (for depth-2 crawls).
std::vector<GURL> ExtractPageLinks(const std::string& html, const GURL& base);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_PAGE_GRABBER_H_
