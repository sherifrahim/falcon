// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_MEDIA_MEDIA_CLASSIFY_H_
#define BRAVE_BROWSER_FALCON_MEDIA_MEDIA_CLASSIFY_H_

#include <string_view>

class GURL;

namespace falcon {

// What a loaded resource is, as far as the media grabber cares. Shared by the
// desktop sniffer and the Android capture helper.
enum class MediaKind {
  kNone,      // not media, or a fragment that is useless on its own
  kFile,      // a whole audio/video file
  kHls,       // an HLS playlist (.m3u8)
  kDash,      // a DASH manifest (.mpd)
};

// Classifies by MIME type and file extension.
MediaKind ClassifyMedia(const GURL& url, std::string_view mime_type);

// Hosts whose media requests are chunked/tokenised and never worth listing.
bool IsChunkHost(const GURL& url);

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_MEDIA_MEDIA_CLASSIFY_H_
