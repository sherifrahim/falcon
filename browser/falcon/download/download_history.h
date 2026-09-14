// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_HISTORY_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_HISTORY_H_

#include <string>

#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "brave/browser/falcon/download/download_tracker.h"

class PrefRegistrySimple;

namespace falcon {

namespace prefs {
// Local state.
inline constexpr char kHistoryEntries[] = "falcon.history.entries";
inline constexpr char kHistoryKeepDays[] = "falcon.history.keep_days";
inline constexpr char kStatsTotalBytes[] = "falcon.stats.total_bytes";
inline constexpr char kStatsTotalFiles[] = "falcon.stats.total_files";
void RegisterHistoryLocalStatePrefs(PrefRegistrySimple* registry);
}  // namespace prefs

// Persistent list of finished downloads (aria2 forgets stopped entries on
// restart) plus all-time statistics. Entries older than |kHistoryKeepDays|
// are pruned daily; 0 keeps them forever.
class DownloadHistory : public DownloadTracker::Observer {
 public:
  static constexpr size_t kMaxEntries = 2000;

  explicit DownloadHistory(DownloadTracker* tracker);
  DownloadHistory(const DownloadHistory&) = delete;
  DownloadHistory& operator=(const DownloadHistory&) = delete;
  ~DownloadHistory() override;

  // DownloadTracker::Observer:
  void OnDownloadFinished(const DownloadTracker::Finished& finished) override;

  // Newest first.
  base::ListValue Entries() const;
  base::DictValue Stats() const;
  void Remove(const std::string& gid);
  void UpdatePath(const std::string& gid,
                  const std::string& name,
                  const std::string& path);
  void Clear();
  void Prune();

 private:
  base::RepeatingTimer prune_timer_;
  base::ScopedObservation<DownloadTracker, DownloadTracker::Observer>
      observation_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_HISTORY_H_
