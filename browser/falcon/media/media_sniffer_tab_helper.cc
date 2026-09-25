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
#include "base/functional/callback_helpers.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/falcon/download/download_categories.h"
#include "brave/browser/falcon/download/download_interceptor.h"
#include "brave/browser/falcon/download/pref_names.h"
#include "brave/browser/falcon/media/media_classify.h"
#include "brave/browser/falcon/media/media_service.h"
#include "brave/browser/falcon/media/video_pill_script.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
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
  switch (ClassifyMedia(url, mime_type)) {
    case MediaKind::kHls:
    case MediaKind::kDash:
      *media = true;
      return Kind::kPlaylist;
    case MediaKind::kFile:
      *media = true;
      return Kind::kFile;
    case MediaKind::kNone:
      break;
  }
  *media = false;
  return Kind::kPage;
}

void MediaSnifferTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted() ||
      handle->IsSameDocument()) {
    return;
  }
  candidates_.clear();
  pill_nonce_.clear();
  const GURL& url = handle->GetURL();
  if (IsExtractorSite(url)) {
    Candidate page;
    page.kind = Kind::kPage;
    page.url = url;
    page.name = base::UTF16ToUTF8(web_contents()->GetTitle());
    if (page.name.empty()) page.name = url.host();
    candidates_.push_back(std::move(page));
    MaybeInjectPill();
  }
  Notify();
}

void MediaSnifferTabHelper::MaybeInjectPill() {
  if (!pill_nonce_.empty()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord() ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadVideoPill) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDownloadEngineEnabled)) {
    return;
  }
  content::RenderFrameHost* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() ||
      !web_contents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  pill_nonce_ = base::HexEncode(base::RandBytesAsVector(8));
  std::string script(kVideoPillScript);
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "%NONCE%", pill_nonce_);
  rfh->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script),
                                        base::NullCallback(),
                                        ISOLATED_WORLD_ID_BRAVE_INTERNAL);
}

void MediaSnifferTabHelper::OnDidAddMessageToConsole(
    content::RenderFrameHost* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const std::optional<std::u16string>& untrusted_stack_trace) {
  if (pill_nonce_.empty() || source_frame != web_contents()->GetPrimaryMainFrame()) {
    return;
  }
  const std::string prefix = "FALCON_DL:" + pill_nonce_ + ":";
  const std::string text = base::UTF16ToUTF8(message);
  if (!base::StartsWith(text, prefix)) {
    return;
  }
  OnPillClicked(text.substr(prefix.size()));
}

void MediaSnifferTabHelper::OnPillClicked(const std::string& target) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  const GURL page = web_contents()->GetLastCommittedURL();
  if (!profile) {
    return;
  }
  const GURL src(target);
  bool is_media = false;
  const Kind kind = src.is_valid()
                        ? Classify(src, std::string(), &is_media)
                        : Kind::kPage;
  // A plain file source (mp4/webm...) goes to the engine; MSE/blob players
  // and playlists go through yt-dlp using the page URL, which knows how to
  // reassemble them.
  if (src.SchemeIsHTTPOrHTTPS() && is_media && kind == Kind::kFile) {
    StartEngineDownload(profile, src, page);
    return;
  }
  const GURL stream_or_page =
      (src.SchemeIsHTTPOrHTTPS() && is_media) ? src : page;
  MediaService::Get()->Start(profile, stream_or_page, page, "best");
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
  if (kind == Kind::kFile && IsChunkHost(url)) {
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
  MaybeInjectPill();
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
