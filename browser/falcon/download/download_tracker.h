// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_TRACKER_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_TRACKER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "net/base/network_change_notifier.h"

namespace falcon {

class Aria2Service;

// Polls aria2 and turns its state into events: aggregate progress for the
// toolbar badge, and per-download finished/failed for notifications and the
// single-connection retry. UI thread only.
class DownloadTracker
    : public net::NetworkChangeNotifier::NetworkChangeObserver {
 public:
  struct Snapshot {
    int active = 0;
    int waiting = 0;
    int64_t total_bytes = 0;      // sum over active downloads (known sizes)
    int64_t completed_bytes = 0;  // sum over active downloads
    int64_t download_speed = 0;   // bytes/s
  };

  struct Finished {
    std::string gid;
    std::string name;
    std::string path;                // first file, may be empty
    std::vector<std::string> paths;  // every completed file (torrents)
    std::string source_url;          // first URI, may be empty (torrent)
    bool is_torrent = false;
    // Browser-internal transfer (sidecar install): not for history/notifications.
    bool internal = false;
    bool success = false;
    int error_code = 0;
    std::string error_message;
    int64_t total_bytes = 0;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnDownloadsChanged(const Snapshot& snapshot) {}
    virtual void OnDownloadFinished(const Finished& finished) {}
  };

  explicit DownloadTracker(Aria2Service* service);
  DownloadTracker(const DownloadTracker&) = delete;
  DownloadTracker& operator=(const DownloadTracker&) = delete;
  ~DownloadTracker() override;

  // net::NetworkChangeNotifier::NetworkChangeObserver:
  void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Something changed (a download was added); poll soon.
  void Poke();
  void Stop();

  // Lets other engines (yt-dlp media jobs) feed the same observers:
  // notifications, history, security scan.
  void NotifyExternalFinished(const Finished& finished);

  const Snapshot& snapshot() const { return snapshot_; }

  // Parsed status of one download, shared with notifications/retry.
  struct Status {
    std::string gid;
    std::string status;
    std::string name;
    std::string path;
    std::string dir;
    int64_t total = 0;
    int64_t completed = 0;
    int64_t speed = 0;
    int error_code = 0;
    std::string error_message;
    std::string first_uri;
    std::vector<std::string> paths;
    bool is_torrent = false;
  };
  static Status ParseStatus(const base::DictValue& dict);

 private:
  void ScheduleNext(int delay_ms);
  void Poll();
  void OnActive(std::optional<base::Value> result);
  void OnGlobalStat(std::optional<base::Value> result);
  void OnFinishedStatus(const std::string& gid,
                        std::optional<base::Value> result);
  // Every poll: diff aria2's stopped list so completions that never showed up
  // as "active" (small files, fast servers) are still reported once.
  void OnStopped(std::optional<base::Value> result);
  void HandleStopped(const Status& status);
  void MaybeRetrySingleConnection(const Status& status);
  void RetryNetworkFailures();
  void OnStoppedForRetry(std::optional<base::Value> result);

  const raw_ptr<Aria2Service> service_;
  base::ObserverList<Observer> observers_;
  base::OneShotTimer timer_;
  bool polling_ = false;
  Snapshot snapshot_;
  std::set<std::string> known_active_;
  std::map<std::string, Status> last_status_;  // by gid, for retry options
  std::set<std::string> retried_;              // gids already retried
  std::set<std::string> reported_;             // stopped gids already reported
  bool was_offline_ = false;
  base::WeakPtrFactory<DownloadTracker> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_TRACKER_H_
