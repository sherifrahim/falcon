// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/falcon/media/media_sniffer_tab_helper.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/download/download_categories.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"

namespace falcon {

namespace {

constexpr size_t kMaxCandidates = 40;

// Registrable domains yt-dlp has first-class extractors for. Raw chunks from
// these players are useless on their own, so the page itself is offered.
constexpr std::string_view kExtractorSites[] = {
    "youtube.com",     "youtu.be",      "vimeo.com",       "twitter.com",
    "x.com",           "tiktok.com",    "instagram.com",   "facebook.com",
    "fb.watch",        "dailymotion.com", "twitch.tv",     "reddit.com",
    "soundcloud.com",  "bilibili.com",  "ok.ru",           "vk.com",
    "rumble.com",      "odysee.com",    "streamable.com",  "bandcamp.com",
    "bitchute.com",    "ted.com",       "9gag.com",        "imgur.com",
    "pinterest.com",   "linkedin.com",  "threads.net",     "kick.com",
};

// Hosts whose media requests are chunked/tokenised and never worth listing.
constexpr std::string_view kChunkHosts[] = {
    "googlevideo.com", "fbcdn.net", "tiktokcdn.com", "twimg.com",
    "cdninstagram.com", "ttvnw.net",
};

bool HostMatches(const GURL& url, base::span<const std::string_view> list) {
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string host(url.host());
  for (std::string_view site : list) {
    if (domain == site || host == site ||
        base::EndsWith(host, std::string(".") + std::string(site))) {
      return true;
    }
  }
  return false;
}

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

MediaSnifferTabHelper::MediaSnifferTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<MediaSnifferTabHelper>(*contents) {}

MediaSnifferTabHelper::~MediaSnifferTabHelper() = default;

// static
bool MediaSnifferTabHelper::IsExtractorSite(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() && HostMatches(url, kExtractorSites);
}

// static
MediaSnifferTabHelper::Kind MediaSnifferTabHelper::Classify(
    const GURL& url,
    const std::string& mime_type,
    bool* media) {
  *media = false;
  const std::string mime = base::ToLowerASCII(mime_type);
  const std::string ext = Extension(url);
  if (mime == "application/vnd.apple.mpegurl" ||
      mime == "application/x-mpegurl" || mime == "audio/mpegurl" ||
      mime == "audio/x-mpegurl" || mime == "application/dash+xml" ||
      ext == "m3u8" || ext == "mpd") {
    *media = true;
    return Kind::kPlaylist;
  }
  // Segments and fragments of adaptive streams: never useful alone.
  if (ext == "ts" || ext == "m4s" || ext == "m4f" || ext == "cmfv" ||
      ext == "cmfa" || ext == "init" || ext == "key") {
    return Kind::kPage;
  }
  static constexpr std::string_view kFileExts[] = {
      "mp4", "m4v", "webm", "mkv", "mov", "avi", "flv", "wmv", "mpg", "mpeg",
      "3gp", "ogv", "mp3", "m4a", "aac", "ogg", "oga", "opus", "flac", "wav",
      "wma",
  };
  const bool mime_media = base::StartsWith(mime, "video/") ||
                          (base::StartsWith(mime, "audio/") &&
                           mime != "audio/mpegurl");
  const bool ext_media =
      std::find(std::begin(kFileExts), std::end(kFileExts), ext) !=
      std::end(kFileExts);
  if (mime_media || ext_media) {
    *media = true;
    return Kind::kFile;
  }
  return Kind::kPage;
}

void MediaSnifferTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted() ||
      handle->IsSameDocument()) {
    return;
  }
  candidates_.clear();
  const GURL& url = handle->GetURL();
  if (IsExtractorSite(url)) {
    Candidate page;
    page.kind = Kind::kPage;
    page.url = url;
    page.name = base::UTF16ToUTF8(web_contents()->GetTitle());
    if (page.name.empty()) page.name = url.host();
    candidates_.push_back(std::move(page));
  }
  Notify();
}

void MediaSnifferTabHelper::TitleWasSet(content::NavigationEntry* entry) {
  // Keep the page candidate's label in sync with the title.
  for (Candidate& c : candidates_) {
    if (c.kind == Kind::kPage && c.url == web_contents()->GetLastCommittedURL()) {
      const std::string title = base::UTF16ToUTF8(web_contents()->GetTitle());
      if (!title.empty() && title != c.name) {
        c.name = title;
        Notify();
      }
      return;
    }
  }
}

void MediaSnifferTabHelper::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const GURL& original_url,
    const blink::mojom::ResourceLoadInfo& info) {
  const GURL& url = info.final_url.is_valid() ? info.final_url : original_url;
  if (!url.SchemeIsHTTPOrHTTPS() || info.net_error != 0) {
    return;
  }
  if (info.request_destination == network::mojom::RequestDestination::kImage ||
      info.request_destination == network::mojom::RequestDestination::kScript ||
      info.request_destination == network::mojom::RequestDestination::kStyle ||
      info.request_destination == network::mojom::RequestDestination::kFont) {
    return;
  }
  bool media = false;
  const Kind kind = Classify(url, info.mime_type, &media);
  if (!media) {
    return;
  }
  if (kind == Kind::kFile && HostMatches(url, kChunkHosts)) {
    return;
  }
  // Tiny "video" responses are usually probes/ads beacons.
  const int64_t bytes = static_cast<int64_t>(info.total_received_bytes.InBytes());
  if (kind == Kind::kFile && bytes > 0 && bytes < 64 * 1024) {
    return;
  }

  Candidate c;
  c.kind = kind;
  c.url = url;
  c.mime_type = info.mime_type;
  c.size = bytes;
  c.name = GuessFilename(url, std::string());
  if (c.name.empty()) c.name = url.host();
  Add(std::move(c));
}

void MediaSnifferTabHelper::Add(Candidate candidate) {
  // Same resource fetched in ranges shows up many times; keep one entry keyed
  // on host+path (the first query string wins, tokens included).
  const std::string key =
      base::StrCat({candidate.url.host(), candidate.url.path()});
  for (Candidate& existing : candidates_) {
    if (existing.kind == candidate.kind &&
        base::StrCat({existing.url.host(), existing.url.path()}) ==
            key) {
      existing.size = std::max(existing.size, candidate.size);
      return;
    }
  }
  if (candidates_.size() >= kMaxCandidates) {
    return;
  }
  candidates_.push_back(std::move(candidate));
  Notify();
}

void MediaSnifferTabHelper::Notify() {
  for (Observer& o : observers_) {
    o.OnMediaCandidatesChanged(web_contents());
  }
}

void MediaSnifferTabHelper::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MediaSnifferTabHelper::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::ListValue MediaSnifferTabHelper::CandidatesAsList() const {
  base::ListValue list;
  for (const Candidate& c : candidates_) {
    base::DictValue d;
    d.Set("kind", c.kind == Kind::kFile       ? "file"
                  : c.kind == Kind::kPlaylist ? "playlist"
                                              : "page");
    d.Set("url", c.url.spec());
    d.Set("name", c.name);
    d.Set("mime", c.mime_type);
    d.Set("size", static_cast<double>(c.size));
    list.Append(std::move(d));
  }
  return list;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MediaSnifferTabHelper);

}  // namespace falcon
