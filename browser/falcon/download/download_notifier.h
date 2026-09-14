// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_NOTIFIER_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_NOTIFIER_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "brave/browser/falcon/download/download_tracker.h"

namespace falcon {

// Desktop notifications for finished/failed downloads; click opens the file
// (or the downloader page on failure).
class DownloadNotifier : public DownloadTracker::Observer {
 public:
  explicit DownloadNotifier(DownloadTracker* tracker);
  DownloadNotifier(const DownloadNotifier&) = delete;
  DownloadNotifier& operator=(const DownloadNotifier&) = delete;
  ~DownloadNotifier() override;

  // DownloadTracker::Observer:
  void OnDownloadFinished(const DownloadTracker::Finished& finished) override;

 private:
  base::ScopedObservation<DownloadTracker, DownloadTracker::Observer>
      observation_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_NOTIFIER_H_
