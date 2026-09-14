// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SECURITY_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SECURITY_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "brave/browser/falcon/download/download_tracker.h"

class PrefRegistrySimple;

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace falcon {

namespace prefs {
// Local state.
inline constexpr char kSecurityVtApiKey[] = "falcon.security.vt_api_key";
inline constexpr char kSecurityVtEnabled[] = "falcon.security.vt_enabled";
inline constexpr char kSecurityQuarantineFlagged[] =
    "falcon.security.quarantine_flagged";
inline constexpr char kSecuritySandboxNetworking[] =
    "falcon.security.sandbox_networking";
void RegisterSecurityLocalStatePrefs(PrefRegistrySimple* registry);
}  // namespace prefs

// Post-download security pipeline, per completed file:
//   1. Mark-of-the-Web + SmartScreen/Defender check via Chromium's
//      quarantine service (exactly what Chrome does for its own downloads).
//   2. SHA-256 of the file.
//   3. Optional VirusTotal hash lookup (hash only, never the file) with the
//      user's own API key.
//   4. Files flagged by VT are moved to <download dir>/Quarantine.
// Results are kept per aria2 gid for the downloader page.
class DownloadSecurity : public DownloadTracker::Observer {
 public:
  struct FileResult {
    std::string path;
    std::string sha256;
    // "clean", "infected" (removed by AV), "failed", "skipped"
    std::string av = "skipped";
    // "clean", "flagged", "unknown", "nokey", "error", "pending", "skipped"
    std::string vt = "skipped";
    int vt_malicious = 0;
    int vt_suspicious = 0;
    int vt_total = 0;
    std::string quarantined_to;
  };
  struct Result {
    std::string gid;
    std::string name;
    bool done = false;
    std::vector<FileResult> files;
  };

  explicit DownloadSecurity(DownloadTracker* tracker);
  DownloadSecurity(const DownloadSecurity&) = delete;
  DownloadSecurity& operator=(const DownloadSecurity&) = delete;
  ~DownloadSecurity() override;

  // DownloadTracker::Observer:
  void OnDownloadFinished(const DownloadTracker::Finished& finished) override;

  // gid -> result, as JSON for the WebUI.
  base::DictValue ResultsAsDict() const;
  void Rescan(const std::string& gid);

  // Windows Sandbox: launches a throwaway VM with the file's folder mapped
  // read-only. Available only when the optional Windows feature is on.
  static bool IsSandboxAvailable();
  static void ProbeSandboxAvailability();
  static void OpenInSandbox(const base::FilePath& file, bool networking);

  struct LocalScan;  // result of the blocking quarantine+hash step

 private:
  void StartScan(const DownloadTracker::Finished& finished);
  void OnLocalScan(const std::string& gid,
                   size_t index,
                   std::unique_ptr<LocalScan> scan);
  void LookupVirusTotal(const std::string& gid, size_t index);
  void OnVirusTotal(const std::string& gid,
                    size_t index,
                    network::SimpleURLLoader* loader,
                    std::optional<std::string> body);
  void QuarantineFlagged(const std::string& gid, size_t index);
  void OnQuarantined(const std::string& gid,
                     size_t index,
                     base::FilePath moved_to);
  void MaybeFinish(const std::string& gid);
  void Notify(const std::string& title, const std::string& message);

  std::map<std::string, Result> results_;
  std::map<std::string, DownloadTracker::Finished> pending_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  base::ScopedObservation<DownloadTracker, DownloadTracker::Observer>
      observation_{this};
  base::WeakPtrFactory<DownloadSecurity> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_DOWNLOAD_SECURITY_H_
