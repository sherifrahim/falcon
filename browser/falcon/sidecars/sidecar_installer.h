/* Copyright (c) 2026 The Falcon Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FALCON_SIDECARS_SIDECAR_INSTALLER_H_
#define BRAVE_BROWSER_FALCON_SIDECARS_SIDECAR_INSTALLER_H_

#include <map>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "brave/browser/falcon/download/download_tracker.h"

namespace falcon {

// One pinned release asset from sidecars.json.
struct SidecarInfo {
  std::string name;     // "yt-dlp", "ffmpeg", "bitwarden-cli"
  std::string version;  // release tag
  std::string url;
  std::string sha256;   // lowercase hex of the asset
  int64_t size = 0;
  bool zip = false;                 // false: the asset is the executable
  std::vector<std::string> files;   // zip entries to keep (basename globs ok)
};

// Sidecars are fetched on demand into the profile instead of shipping in the
// installer (yt-dlp 18 MB, ffmpeg 190 MB, Bitwarden CLI 127 MB). The manifest
// (sidecars.json, a bundled resource) pins the release asset and its SHA-256;
// aria2 does the transfer and the checksum, the file lands in
//   <user data>/Falcon Sidecars/<name>/<version>/
// A build that still bundles a sidecar next to the browser (dev out/ dirs)
// wins over the installed copy.
class SidecarInstaller : public DownloadTracker::Observer {
 public:
  struct Status {
    std::string name;
    std::string version;  // manifest version
    int64_t size = 0;
    bool bundled = false;    // next to the browser
    bool installed = false;  // in the profile, at the manifest version
    bool installing = false;
    double progress = 0;     // 0..1 while installing
    std::string error;
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSidecarsChanged() {}
  };

  static SidecarInstaller* Get();

  SidecarInstaller(const SidecarInstaller&) = delete;
  SidecarInstaller& operator=(const SidecarInstaller&) = delete;

  std::vector<Status> GetStatuses() const;
  const SidecarInfo* Find(const std::string& name) const;

  // The directory holding |name|'s files: the bundled one next to the browser
  // if present, else the installed one. Empty when neither exists.
  base::FilePath DirFor(const std::string& name) const;
  // Path of |exe| for |name|, or empty when the sidecar is not available.
  base::FilePath ExePath(const std::string& name, const std::string& exe) const;
  bool IsAvailable(const std::string& name) const;

  using InstallCallback =
      base::OnceCallback<void(bool ok, const std::string& error)>;
  void Install(const std::string& name, InstallCallback callback);

  // Re-reads which sidecars are on disk, off the UI thread; observers are
  // notified if anything changed.
  void RefreshPresence();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // DownloadTracker::Observer:
  void OnDownloadsChanged(const DownloadTracker::Snapshot& snapshot) override;
  void OnDownloadFinished(const DownloadTracker::Finished& finished) override;

 private:
  friend class base::NoDestructor<SidecarInstaller>;
  SidecarInstaller();
  ~SidecarInstaller() override;

  struct Pending {
    std::string name;
    std::string gid;
    base::FilePath asset;  // where aria2 writes it
    double progress = 0;
    std::vector<InstallCallback> callbacks;
  };

  void LoadManifest();
  base::FilePath RootDir() const;
  base::FilePath InstallDir(const SidecarInfo& info) const;
  base::FilePath BundledDir() const;
  void OnAdded(const std::string& name, std::optional<base::Value> result);
  void OnStaged(const std::string& name, const std::string& error);
  void Finish(const std::string& name, bool ok, const std::string& error);
  void NotifyChanged();

  // Whether each sidecar's files are on disk. GetStatuses() is polled by the
  // control panel while it is open, so it reads this cache instead of the
  // disk; the cache is refilled on a blocking-allowed sequence.
  struct Presence {
    bool bundled = false;
    bool installed = false;
    friend bool operator==(const Presence&, const Presence&) = default;
  };
  // Blocking; runs on a thread pool sequence.
  static std::map<std::string, Presence> ComputePresence(
      std::vector<SidecarInfo> manifest,
      base::FilePath bundled_root,
      std::map<std::string, base::FilePath> install_dirs);
  void OnPresenceRefreshed(std::map<std::string, Presence> presence);

  std::vector<SidecarInfo> manifest_;
  std::map<std::string, Pending> pending_;  // by name
  std::map<std::string, std::string> errors_;  // by name, last failure
  std::map<std::string, Presence> presence_;  // by name, refreshed off-thread
  bool observing_ = false;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<SidecarInstaller> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_SIDECARS_SIDECAR_INSTALLER_H_
