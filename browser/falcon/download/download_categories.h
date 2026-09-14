// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_CATEGORIES_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_CATEGORIES_H_

#include <string>
#include <string_view>

class GURL;

namespace falcon {

// IDM-style categories. The returned name doubles as the sub-folder under the
// download directory. Empty string = no category (root download dir).
std::string CategoryForFilename(std::string_view filename);

// Best-effort filename for a download: Content-Disposition name if present,
// else the last path segment of |url| (decoded). May be empty.
std::string GuessFilename(const GURL& url,
                          const std::string& content_disposition_filename);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_CATEGORIES_H_
