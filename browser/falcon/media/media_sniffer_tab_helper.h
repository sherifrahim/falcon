// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SNIFFER_TAB_HELPER_H_
#define BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SNIFFER_TAB_HELPER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace falcon {

// IDM-style media sniffer: watches the resources a tab loads and remembers
// video/audio files and HLS/DASH playlists so the toolbar can offer them.
// Pages on sites yt-dlp knows well (YouTube, Vimeo, ...) get a "page"
// candidate so the whole player can be grabbed at the chosen quality.
class MediaSnifferTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MediaSnifferTabHelper> {
 public:
  enum class Kind { kFile, kPlaylist, kPage };

  struct Candidate {
    Kind kind = Kind::kFile;
    GURL url;
    std::string name;       // file name or page title
    std::string mime_type;  // may be empty
    int64_t size = 0;       // bytes received for this URL (files only)
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnMediaCandidatesChanged(content::WebContents* contents) {}
  };

  ~MediaSnifferTabHelper() override;

  const std::vector<Candidate>& candidates() const { return candidates_; }
  base::ListValue CandidatesAsList() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // True when |url| is a page yt-dlp has a dedicated extractor for.
  static bool IsExtractorSite(const GURL& url);
  // Classifies by MIME type and file extension; Kind::kPage means "not media".
  static Kind Classify(const GURL& url, const std::string& mime_type, bool* media);

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void ResourceLoadComplete(
      content::RenderFrameHost* render_frame_host,
      const content::GlobalRequestID& request_id,
      const GURL& original_url,
      const blink::mojom::ResourceLoadInfo& resource_load_info) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override;

 private:
  friend class content::WebContentsUserData<MediaSnifferTabHelper>;
  explicit MediaSnifferTabHelper(content::WebContents* contents);

  void Add(Candidate candidate);
  void Notify();
  // IDM-style hover pill over <video> elements (falcon.download.video_pill).
  void MaybeInjectPill();
  void OnPillClicked(const std::string& target);

  std::vector<Candidate> candidates_;
  base::ObserverList<Observer> observers_;
  std::string pill_nonce_;  // empty until injected for this document

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SNIFFER_TAB_HELPER_H_
