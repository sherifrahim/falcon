// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_DOWNLOAD_ARIA2_SERVICE_H_
#define BRAVE_BROWSER_FALCON_DOWNLOAD_ARIA2_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_handle.h"
#endif

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace falcon {

class ClipboardMonitor;
class DownloadHistory;
class DownloadNotifier;
class DownloadScheduler;
class DownloadSecurity;
class DownloadTracker;

// Owns the aria2c sidecar process for the whole browser and speaks JSON-RPC to
// it over 127.0.0.1. UI-thread only. One instance per browser process.
//
// The WebUI (falcon://downloader) talks to aria2 directly over WebSocket using
// the same port/secret; this class launches/supervises the process, submits
// downloads handed over from Chromium, and (via DownloadTracker) turns aria2
// state into events for the toolbar badge, notifications and auto-retry.
class Aria2Service {
 public:
  static Aria2Service* Get();

  Aria2Service(const Aria2Service&) = delete;
  Aria2Service& operator=(const Aria2Service&) = delete;

  // Chooses port/secret synchronously (so rpc_*() are valid on return) and
  // launches aria2c in the background if it is not already running.
  void EnsureRunning();

  bool IsLaunched() const { return launched_; }
  bool IsReady() const { return ready_; }
  int rpc_port() const { return rpc_port_; }
  const std::string& rpc_secret() const { return rpc_secret_; }
  std::string rpc_http_url() const;
  std::string rpc_ws_url() const;

  DownloadTracker* tracker() { return tracker_.get(); }
  DownloadSecurity* security() { return security_.get(); }
  DownloadHistory* history() { return history_.get(); }
  DownloadScheduler* scheduler() { return scheduler_.get(); }

  // Builds aria2 options for a download taken over from the browser.
  static base::DictValue BuildOptions(const std::string& referer,
                                      const std::string& user_agent,
                                      const std::string& cookie_header,
                                      const std::string& out_filename,
                                      const base::FilePath& download_dir);

  // Submits |url| (http(s), ftp, magnet, ...) with aria2 |options|. Queued
  // until the RPC endpoint answers.
  void AddUri(const GURL& url, base::DictValue options);

  // Generic RPC call; |params| excludes the secret token (prepended here).
  using RpcCallback = base::OnceCallback<void(std::optional<base::Value>)>;
  void Call(const std::string& method,
            base::ListValue params,
            RpcCallback callback);
  // Same, but launches the engine if needed and waits for the endpoint.
  void CallWhenReady(const std::string& method,
                     base::ListValue params,
                     RpcCallback callback);

  // Push the engine prefs (connections, concurrency, limits, seeding) to the
  // running aria2 instance.
  void ApplyEnginePrefs();

  void Shutdown();

  // Result of launching the sidecar on a blocking sequence.
  struct LaunchResult;

 private:
  friend class base::NoDestructor<Aria2Service>;

  Aria2Service();
  ~Aria2Service();

  void PickEndpoint();
  void Launch();
  void OnLaunched(std::unique_ptr<LaunchResult> result);
  void ProbeReady();
  void OnProbeResult(std::optional<base::Value> result);
  void FlushQueue();
  void OnRpcResponse(network::SimpleURLLoader* loader,
                     RpcCallback callback,
                     std::optional<std::string> body);
  base::DictValue EngineOptionsFromPrefs() const;

  base::FilePath SessionFile() const;

  bool launched_ = false;
  bool ready_ = false;
  int launch_attempts_ = 0;
  int rpc_port_ = 0;
  std::string rpc_secret_;
  base::Process process_;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle job_;
#endif
  std::vector<base::OnceClosure> queued_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::unique_ptr<DownloadTracker> tracker_;
  std::unique_ptr<DownloadNotifier> notifier_;
  std::unique_ptr<DownloadSecurity> security_;
  std::unique_ptr<DownloadHistory> history_;
  std::unique_ptr<DownloadScheduler> scheduler_;
  std::unique_ptr<ClipboardMonitor> clipboard_monitor_;
  PrefChangeRegistrar pref_change_registrar_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<Aria2Service> weak_factory_{this};
};

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_DOWNLOAD_ARIA2_SERVICE_H_
