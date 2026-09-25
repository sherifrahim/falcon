// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/media/media_classify.h"

#include <algorithm>
#include <iterator>
#include <string>

#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace falcon {

namespace {

constexpr std::string_view kChunkHosts[] = {
    "googlevideo.com", "fbcdn.net", "tiktokcdn.com", "twimg.com",
    "cdninstagram.com", "ttvnw.net",
};

constexpr std::string_view kFileExts[] = {
    "mp4", "m4v", "webm", "mkv", "mov", "avi", "flv", "wmv", "mpg", "mpeg",
    "3gp", "ogv", "mp3", "m4a", "aac", "ogg", "oga", "opus", "flac", "wav",
    "wma",
};

std::string Extension(const GURL& url) {
  const std::string_view path = url.path();
  const size_t slash = path.find_last_of('/');
  const std::string_view name =
      slash == std::string_view::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos) {
    return std::string();
  }
  return base::ToLowerASCII(name.substr(dot + 1));
}

}  // namespace

MediaKind ClassifyMedia(const GURL& url, std::string_view mime_type) {
  const std::string mime = base::ToLowerASCII(mime_type);
  const std::string ext = Extension(url);
  if (mime == "application/vnd.apple.mpegurl" ||
      mime == "application/x-mpegurl" || mime == "audio/mpegurl" ||
      mime == "audio/x-mpegurl" || ext == "m3u8") {
    return MediaKind::kHls;
  }
  if (mime == "application/dash+xml" || ext == "mpd") {
    return MediaKind::kDash;
  }
  // Segments and fragments of adaptive streams: never useful alone.
  if (ext == "ts" || ext == "m4s" || ext == "m4f" || ext == "cmfv" ||
      ext == "cmfa" || ext == "init" || ext == "key") {
    return MediaKind::kNone;
  }
  const bool mime_media = base::StartsWith(mime, "video/") ||
                          base::StartsWith(mime, "audio/");
  const bool ext_media =
      std::find(std::begin(kFileExts), std::end(kFileExts), ext) !=
      std::end(kFileExts);
  return (mime_media || ext_media) ? MediaKind::kFile : MediaKind::kNone;
}

bool IsChunkHost(const GURL& url) {
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string host(url.host());
  for (std::string_view site : kChunkHosts) {
    if (domain == site || host == site ||
        base::EndsWith(host, std::string(".") + std::string(site))) {
      return true;
    }
  }
  return false;
}

}  // namespace falcon
